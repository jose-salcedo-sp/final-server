use std::fs;
use std::io::Write;
use std::net::{SocketAddr, TcpStream};
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, UdpSocket};
use tokio::sync::Mutex;

use crate::config::Config;

#[derive(Debug, Clone)]
pub struct Server {
    pub addr_identifier: Option<SocketAddr>,
    pub udp_addr: SocketAddr,
    pub tcp_addr: SocketAddr,
    pub last_heartbeat: Option<Instant>,
    pub is_up: bool,
}

pub enum ReverseProxy {
    Fl(Arc<ReverseProxyFl>),
    Ld(Arc<ReverseProxyLd>),
}

pub struct ReverseProxyFl {
    pub frontend_tcp_addr: SocketAddr,
    pub backend_tcp_addr: SocketAddr,
    pub backend_heartbeat_udp_addr: SocketAddr,
    pub backends: Mutex<Vec<Server>>,
    pub next_index: AtomicUsize,
}

pub struct ReverseProxyLd {
    pub frontend_tcp_addr: SocketAddr,
    pub backend_tcp_addr: SocketAddr,
    pub backend_heartbeat_udp_addr: SocketAddr,
    pub frontend_heartbeat_udp_addr: SocketAddr,
    pub backends: Arc<Mutex<Vec<Server>>>,
    pub frontends: Arc<Mutex<Vec<Server>>>,
    pub next_index: AtomicUsize,
}

impl ReverseProxy {
    pub fn from_config_file(path: &Path) -> Arc<Self> {
        let contents = fs::read_to_string(path).expect("Config file not found");
        let config: Config = serde_json::from_str(&contents).expect("Failed to parse config");

        match config {
            Config::Fl {
                backend_tcp_addr,
                backend_heartbeat_udp_addr,
                frontend_tcp_addr,
                backend_addrs,
            } => {
                let backend_servers = Self::parse_addr_pairs(&backend_addrs);
                Arc::new(Self::Fl(Arc::new(ReverseProxyFl {
                    frontend_tcp_addr,
                    backend_tcp_addr,
                    backend_heartbeat_udp_addr,
                    backends: Mutex::new(backend_servers),
                    next_index: AtomicUsize::new(0),
                })))
            }

            Config::Ld {
                backend_tcp_addr,
                backend_heartbeat_udp_addr,
                frontend_tcp_addr,
                frontend_heartbeat_udp_addr,
                backend_addrs,
                frontend_addrs,
            } => {
                let backend_servers = Self::parse_addr_pairs(&backend_addrs);
                let frontend_servers = Self::parse_addr_pairs(&frontend_addrs);
                Arc::new(Self::Ld(Arc::new(ReverseProxyLd {
                    frontend_tcp_addr,
                    backend_tcp_addr,
                    backend_heartbeat_udp_addr,
                    frontend_heartbeat_udp_addr,
                    backends: Arc::new(Mutex::new(backend_servers)),
                    frontends: Arc::new(Mutex::new(frontend_servers)),
                    next_index: AtomicUsize::new(0),
                })))
            }
        }
    }

    fn parse_addr_pairs(raw: &[String]) -> Vec<Server> {
        raw.iter()
            .filter_map(|line| {
                let mut parts = line.split_whitespace();
                let tcp = parts.next()?.parse().ok()?;
                let udp = parts.next()?.parse().ok()?;
                Some(Server {
                    addr_identifier: None,
                    tcp_addr: tcp,
                    udp_addr: udp,
                    last_heartbeat: None,
                    is_up: false,
                })
            })
            .collect()
    }
}

impl ReverseProxyFl {
    pub async fn run(self: Arc<Self>) -> std::io::Result<()> {
        self.clone().check_backends_availability().await;
        let listener = TcpListener::bind(self.frontend_tcp_addr).await?;
        println!("🔌 FL Listening on {}", self.frontend_tcp_addr);

        loop {
            let (mut client, addr) = listener.accept().await?;
            let proxy = self.clone();
            tokio::spawn(async move {
                println!("ℹ️ FL Client {} connected", addr);
                let mut buf = vec![0u8; 1024];
                loop {
                    match client.read(&mut buf).await {
                        Ok(0) => return,
                        Ok(n) => {
                            if let Some(backend_addr) = proxy.get_available_backend().await {
                                if let Ok(mut backend_stream) = TcpStream::connect(backend_addr) {
                                    let _ = backend_stream.write_all(&buf[..n]);
                                } else {
                                    let _ =
                                        client.write_all(b"Could not connect to backend\n").await;
                                }
                            } else {
                                let _ = client.write_all(b"No available backend\n").await;
                            }
                        }
                        Err(_) => return,
                    }
                }
            });
        }
    }

    async fn check_backends_availability(self: Arc<Self>) {
        let self_clone = self.clone();
        let udp_heartbeat_addr = self_clone.backend_heartbeat_udp_addr;
        tokio::spawn(async move {
            Self::listen_for_heartbeats(self_clone, udp_heartbeat_addr).await;
        });

        let self_clone = self.clone();
        tokio::spawn(async move {
            Self::check_heartbeat_expiry(self_clone).await;
        });
    }

    async fn listen_for_heartbeats(self_: Arc<Self>, bind_addr: SocketAddr) {
        let socket = UdpSocket::bind(bind_addr)
            .await
            .expect("Failed to bind UDP socket");
        let mut buf = [0u8; 128];

        loop {
            if let Ok((n, from)) = socket.recv_from(&mut buf).await {
                let msg = &buf[..n];
                let message = String::from_utf8_lossy(msg).trim().to_string();
                let mut backends = self_.backends.lock().await;

                match message.as_str() {
                    "OK" => {
                        if let Some(server) = backends
                            .iter_mut()
                            .filter(|s| s.addr_identifier.is_some())
                            .find(|s| s.addr_identifier.unwrap() == from)
                        {
                            server.last_heartbeat = Some(Instant::now());
                            if !server.is_up {
                                println!("✅ Server {} is back online!", from);
                            }
                            server.is_up = true;
                        } else {
                            println!("❓ OK from unknown sender {}", from);
                            let _ = socket.send_to(b"AUTH", from).await;
                        }
                    }

                    _ if message.contains(' ') => {
                        let parts: Vec<&str> = message.split_whitespace().collect();
                        if parts.len() == 2 {
                            let parsed_udp = parts[0].parse::<SocketAddr>();
                            let parsed_tcp = parts[1].parse::<SocketAddr>();

                            match (parsed_udp, parsed_tcp) {
                                (Ok(udp_addr), Ok(tcp_addr)) => {
                                    if let Some(server) = backends
                                        .iter_mut()
                                        .find(|s| s.udp_addr == udp_addr && s.tcp_addr == tcp_addr)
                                    {
                                        let _ = socket.send_to(b"OK", from).await;
                                        server.addr_identifier = Some(from);
                                        println!("🫡 Received ID from {} — matched server {}, responded with OK", from, udp_addr);
                                    } else {
                                        println!(
                                            "❌ Address pair {} {} not recognized",
                                            parts[0], parts[1]
                                        );
                                    }
                                }
                                _ => {
                                    println!(
                                        "❌ Failed to parse heartbeat message: \"{}\"",
                                        message
                                    );
                                }
                            }
                        } else {
                            println!("❌ Malformed heartbeat message: \"{}\"", message);
                        }
                    }

                    _ => {
                        println!("⚠️ Unexpected message: \"{}\" from {}", message, from);
                    }
                }
            }
        }
    }

    async fn check_heartbeat_expiry(self_: Arc<Self>) {
        loop {
            {
                let mut backends = self_.backends.lock().await;
                for backend in backends.iter_mut() {
                    if let Some(last) = backend.last_heartbeat {
                        if last.elapsed() > Duration::from_secs(1) {
                            if backend.is_up {
                                println!("⚠️ Backend {} timed out", backend.udp_addr);
                            }
                            backend.is_up = false;
                        }
                    } else {
                        backend.is_up = false;
                    }
                }
            }
            tokio::time::sleep(Duration::from_millis(2000)).await;
        }
    }

    async fn get_available_backend(&self) -> Option<SocketAddr> {
        let backends = self.backends.lock().await;
        let available: Vec<_> = backends.iter().filter(|s| s.is_up).collect();
        if available.is_empty() {
            return None;
        }
        let index = self.next_index.fetch_add(1, Ordering::Relaxed) % available.len();
        Some(available[index].tcp_addr)
    }
}

impl ReverseProxyLd {
    pub async fn run(self: Arc<Self>) -> std::io::Result<()> {
        self.clone().check_availability().await;
        let listener = TcpListener::bind(self.frontend_tcp_addr).await?;
        println!("🔌 LD Listening on {}", self.frontend_tcp_addr);

        loop {
            let (mut client, addr) = listener.accept().await?;
            let proxy = self.clone();
            tokio::spawn(async move {
                println!("ℹ️ LD Client {} connected", addr);
                let mut buf = vec![0u8; 1024];
                loop {
                    match client.read(&mut buf).await {
                        Ok(0) => return,
                        Ok(n) => {
                            if let Some(backend_addr) = proxy.get_available_backend().await {
                                if let Ok(mut backend_stream) = TcpStream::connect(backend_addr) {
                                    let _ = backend_stream.write_all(&buf[..n]);
                                } else {
                                    let _ =
                                        client.write_all(b"Could not connect to backend\n").await;
                                }
                            } else {
                                let _ = client.write_all(b"No available backend\n").await;
                            }
                        }
                        Err(_) => return,
                    }
                }
            });
        }
    }

    async fn check_availability(self: Arc<Self>) {
        let backend_clone = self.clone();
        let frontend_clone = self.clone();
        let backend_udp = backend_clone.backend_heartbeat_udp_addr;
        let frontend_udp = frontend_clone.frontend_heartbeat_udp_addr;

        tokio::spawn(async move {
            Self::listen_for_heartbeats(backend_clone.backends.clone(), backend_udp, "backend")
                .await;
        });

        tokio::spawn(async move {
            Self::listen_for_heartbeats(frontend_clone.frontends.clone(), frontend_udp, "frontend")
                .await;
        });

        let expiry_clone = self.clone();
        tokio::spawn(async move {
            Self::check_heartbeat_expiry(expiry_clone).await;
        });
    }

    async fn listen_for_heartbeats(
        servers: Arc<Mutex<Vec<Server>>>,
        bind_addr: SocketAddr,
        role: &str,
    ) {
        let socket = UdpSocket::bind(bind_addr)
            .await
            .expect("Failed to bind UDP socket");
        let mut buf = [0u8; 128];

        loop {
            if let Ok((n, from)) = socket.recv_from(&mut buf).await {
                let msg = &buf[..n];
                let message = String::from_utf8_lossy(msg);

                let mut servers = servers.lock().await;
                if let Some(server) = servers.iter_mut().find(|s| s.udp_addr == from) {
                    if message == "OK" {
                        server.last_heartbeat = Some(Instant::now());
                        if !server.is_up {
                            println!("✅ {} server {} is back online!", role, from);
                        }
                        server.is_up = true;
                    }
                }
            }
        }
    }

    async fn check_heartbeat_expiry(self_: Arc<Self>) {
        loop {
            {
                let mut backends = self_.backends.lock().await;
                for server in backends.iter_mut() {
                    if let Some(last) = server.last_heartbeat {
                        if last.elapsed() > Duration::from_secs(1) {
                            if server.is_up {
                                println!("⚠️ Backend {} timed out", server.udp_addr);
                            }
                            server.is_up = false;
                        }
                    } else {
                        server.is_up = false;
                    }
                }

                let mut frontends = self_.frontends.lock().await;
                for server in frontends.iter_mut() {
                    if let Some(last) = server.last_heartbeat {
                        if last.elapsed() > Duration::from_secs(1) {
                            if server.is_up {
                                println!("⚠️ Frontend {} timed out", server.udp_addr);
                            }
                            server.is_up = false;
                        }
                    } else {
                        server.is_up = false;
                    }
                }
            }
            tokio::time::sleep(Duration::from_millis(2000)).await;
        }
    }

    async fn get_available_backend(&self) -> Option<SocketAddr> {
        let backends = self.backends.lock().await;
        let available: Vec<_> = backends.iter().filter(|s| s.is_up).collect();
        if available.is_empty() {
            return None;
        }
        let index = self.next_index.fetch_add(1, Ordering::Relaxed) % available.len();
        Some(available[index].tcp_addr)
    }

    async fn get_available_frontend(&self) -> Option<SocketAddr> {
        let frontends = self.frontends.lock().await;
        let available: Vec<_> = frontends.iter().filter(|s| s.is_up).collect();
        if available.is_empty() {
            return None;
        }
        let index = self.next_index.fetch_add(1, Ordering::Relaxed) % available.len();
        Some(available[index].tcp_addr)
    }
}
