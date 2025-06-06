use serde::{Deserialize, Serialize};
use std::fs;
use std::net::SocketAddr;
use std::path::Path;
use std::str;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::io::AsyncWriteExt;
use tokio::net::{TcpListener, TcpStream, UdpSocket};
use tokio::sync::Mutex;

use crate::config::Config;

const TIMEOUT: u64 = 2;

#[derive(Debug, Clone)]
pub struct Server {
    pub addr_identifier: Option<SocketAddr>,
    pub udp_addr: SocketAddr,
    pub tcp_addr: SocketAddr,
    pub last_heartbeat: Option<Instant>,
    pub is_up: bool,
}

pub enum ReverseProxy {
    ClientLogic(ReverseProxyFl),
    LogicData(ReverseProxyLd),
}

pub struct ReverseProxyFl {
    client_tcp_listening_addr: SocketAddr,
    logic_heartbeat_udp_addr: SocketAddr,
    logic_servers: Mutex<Vec<Server>>,
    next_index: AtomicUsize,
}

pub struct ReverseProxyLd {
    logic_servers_tcp_listening_addr: SocketAddr,
    logic_servers_heartbeat_udp_addr: SocketAddr,
    logic_servers: Arc<Mutex<Vec<Server>>>,
    data_servers_tcp_listening_addr: SocketAddr,
    data_servers_hearbeat_udp_addr: SocketAddr,
    data_servers: Arc<Mutex<Vec<Server>>>,
    next_index: AtomicUsize,
}

#[derive(Serialize, Deserialize)]
pub struct ErrorMsg {
    response_code: u16,
    response_text: String,
}

impl ReverseProxy {
    pub fn from_config_file(path: &Path) -> Self {
        let contents = fs::read_to_string(path).expect("Config file not found");
        let config: Config = serde_json::from_str(&contents).expect("Failed to parse config");

        match config {
            Config::Fl {
                client_tcp_listening_addr,
                logic_heartbeat_udp_addr,
                logic_servers,
            } => {
                let backend_servers = Self::parse_addr_pairs(&logic_servers);
                return Self::ClientLogic(ReverseProxyFl {
                    client_tcp_listening_addr,
                    logic_heartbeat_udp_addr,
                    logic_servers: Mutex::new(backend_servers),
                    next_index: AtomicUsize::new(0),
                });
            }

            Config::Ld {
                data_servers_tcp_listening_addr,
                data_servers_hearbeat_udp_addr,
                logic_servers_tcp_listening_addr,
                logic_servers_heartbeat_udp_addr,
                data_servers,
                logic_servers,
            } => {
                let backend_servers = Self::parse_addr_pairs(&data_servers);
                let frontend_servers = Self::parse_addr_pairs(&logic_servers);
                return Self::LogicData(ReverseProxyLd {
                    logic_servers_tcp_listening_addr,
                    data_servers_tcp_listening_addr,
                    data_servers_hearbeat_udp_addr,
                    logic_servers_heartbeat_udp_addr,
                    data_servers: Arc::new(Mutex::new(backend_servers)),
                    logic_servers: Arc::new(Mutex::new(frontend_servers)),
                    next_index: AtomicUsize::new(0),
                });
            }
        }
    }

    pub async fn run(self) -> std::io::Result<()> {
        match self {
            ReverseProxy::ClientLogic(fl) => Arc::new(fl).run().await,
            ReverseProxy::LogicData(ld) => Arc::new(ld).run().await,
        }
    }

    fn parse_addr_pairs(raw: &[String]) -> Vec<Server> {
        let servers = raw
            .iter()
            .filter_map(|line| {
                let mut parts = line.split_whitespace();
                let tcp = parts.next()?.parse().ok()?;
                let udp = parts.next()?.parse().ok()?;
                return Some(Server {
                    addr_identifier: None,
                    tcp_addr: tcp,
                    udp_addr: udp,
                    last_heartbeat: None,
                    is_up: false,
                });
            })
            .collect();

        println!("servers {:#?}", servers);
        return servers;
    }
}

impl ReverseProxyFl {
    pub async fn run(self: Arc<Self>) -> std::io::Result<()> {
        self.clone().check_backends_availability().await;
        let listener = TcpListener::bind(self.client_tcp_listening_addr).await?;
        println!("🔌 FL Listening on {}", self.client_tcp_listening_addr);

        loop {
            let (mut client, addr) = listener.accept().await?;
            let proxy = self.clone();
            tokio::spawn(async move {
                println!("ℹ️ FL Client {} connected", addr);

                if let Some(backend_addr) = proxy.get_available_backend().await {
                    match TcpStream::connect(backend_addr).await {
                        Ok(mut backend_stream) => {
                            println!(
                                "🔁 Forwarding traffic between client and backend {}",
                                backend_addr
                            );

                            match tokio::io::copy_bidirectional(&mut client, &mut backend_stream)
                                .await
                            {
                                Ok((c2b, b2c)) => {
                                    println!("📊 Connection closed: client→backend={}B, backend→client={}B", c2b, b2c);
                                }
                                Err(e) => {
                                    eprintln!("❌ Copy error: {}", e);
                                }
                            }
                        }
                        Err(e) => {
                            eprintln!("❌ Could not connect to backend {}: {}", backend_addr, e);

                            let _ = client
                                .write_all(
                                    &serde_json::to_vec(&ErrorMsg {
                                        response_code: 503,
                                        response_text: "Could not connect to backend".to_string(),
                                    })
                                    .unwrap(),
                                )
                                .await;
                        }
                    }
                } else {
                    eprintln!("❌ No backend available");
                    let _ = client
                        .write_all(
                            &serde_json::to_vec(&ErrorMsg {
                                response_code: 503,
                                response_text: "No available backend".to_string(),
                            })
                            .unwrap(),
                        )
                        .await;
                }
            });
        }
    }

    async fn check_backends_availability(self: Arc<Self>) {
        let self_clone = self.clone();
        let udp_heartbeat_addr = self_clone.logic_heartbeat_udp_addr;
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
                let mut backends = self_.logic_servers.lock().await;

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
                            let parsed_tcp = parts[0].parse::<SocketAddr>();
                            let parsed_udp = parts[1].parse::<SocketAddr>();

                            match (parsed_tcp, parsed_udp) {
                                (Ok(tcp_addr), Ok(udp_addr)) => {
                                    if let Some(server) = backends
                                        .iter_mut()
                                        .find(|s| s.udp_addr == udp_addr && s.tcp_addr == tcp_addr)
                                    {
                                        server.addr_identifier = Some(from);
                                        server.is_up = true;
                                        println!("ℹ️ Server identified with addr: {}", from);
                                        let _ = socket.send_to(b"OK", from).await;
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
            let mut backends = self_.logic_servers.lock().await;
            for backend in backends.iter_mut() {
                if let Some(last) = backend.last_heartbeat {
                    if last.elapsed() > Duration::from_secs(TIMEOUT) {
                        if backend.is_up {
                            println!("⚠️ Backend {} timed out", backend.udp_addr);
                        }
                        backend.is_up = false;
                    }
                }
            }

            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    }

    async fn get_available_backend(&self) -> Option<SocketAddr> {
        let backends = self.logic_servers.lock().await;
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

        let frontend_tcp_listener =
            TcpListener::bind(self.logic_servers_tcp_listening_addr).await?;
        let backend_tcp_listener = TcpListener::bind(self.data_servers_tcp_listening_addr).await?;
        println!(
            "🔌 Logic TCP listening on {}",
            self.logic_servers_tcp_listening_addr
        );
        println!(
            "🔌 Data TCP listening on {}",
            self.data_servers_tcp_listening_addr
        );

        let proxy = self.clone();
        let frontend_task = tokio::spawn(async move {
            loop {
                let (mut frontend_stream, addr) = frontend_tcp_listener.accept().await.unwrap();
                let proxy = proxy.clone();
                tokio::spawn(async move {
                    println!("📥 Logic server connection from {}", addr);
                    if let Some(backend_addr) = proxy.get_available_backend().await {
                        match TcpStream::connect(backend_addr).await {
                            Ok(mut backend_stream) => {
                                match tokio::io::copy_bidirectional(
                                    &mut backend_stream,
                                    &mut frontend_stream,
                                )
                                .await
                                {
                                    Ok((c2b, b2c)) => {
                                        println!("📊 Connection closed: client→backend={}B, backend→client={}B", c2b, b2c);
                                    }
                                    Err(e) => {
                                        eprintln!("❌ Copy error: {}", e);
                                    }
                                }
                            }
                            Err(e) => {
                                eprintln!("❌ Could not connect to backend {}: {}", backend_addr, e);

                                let _ = frontend_stream
                                    .write_all(
                                        &serde_json::to_vec(&ErrorMsg {
                                            response_code: 503,
                                            response_text: "Failed to connect to backend"
                                                .to_string(),
                                        })
                                        .unwrap(),
                                    )
                                    .await;
                            }
                        }
                    } else {
                        eprintln!("❌ No backend available");
                        let _ = frontend_stream
                            .write_all(
                                &serde_json::to_vec(&ErrorMsg {
                                    response_code: 503,
                                    response_text: "No backend available".to_string(),
                                })
                                .unwrap(),
                            )
                            .await;
                    }
                });
            }
        });

        let proxy = self.clone();
        let backend_task = tokio::spawn(async move {
            loop {
                let (mut backend_stream, addr) = backend_tcp_listener.accept().await.unwrap();
                let proxy = proxy.clone();
                tokio::spawn(async move {
                    println!("📥 Data server connection from {}", addr);
                    if let Some(frontend_addr) = proxy.get_available_frontend().await {
                        match TcpStream::connect(frontend_addr).await {
                            Ok(mut frontend_stream) => {
                                match tokio::io::copy_bidirectional(
                                    &mut backend_stream,
                                    &mut frontend_stream,
                                )
                                .await
                                {
                                    Ok((c2b, b2c)) => {
                                        println!("📊 Connection closed: client→backend={}B, backend→client={}B", c2b, b2c);
                                    }
                                    Err(e) => {
                                        eprintln!("❌ Copy error: {}", e);
                                    }
                                }
                            }
                            Err(_) => {
                                let _ = backend_stream
                                    .write_all(
                                        &serde_json::to_vec(&ErrorMsg {
                                            response_code: 503,
                                            response_text: "Failed to connect to frontend"
                                                .to_string(),
                                        })
                                        .unwrap(),
                                    )
                                    .await;
                            }
                        }
                    } else {
                        let _ = backend_stream
                            .write_all(
                                &serde_json::to_vec(&ErrorMsg {
                                    response_code: 503,
                                    response_text: "No frontend available".to_string(),
                                })
                                .unwrap(),
                            )
                            .await;
                    }
                });
            }
        });

        let _ = tokio::try_join!(frontend_task, backend_task)?;

        return Ok(());
    }

    async fn check_availability(self: Arc<Self>) {
        let backend_clone = self.clone();
        let frontend_clone = self.clone();
        let backend_udp = backend_clone.data_servers_hearbeat_udp_addr;
        let frontend_udp = frontend_clone.logic_servers_heartbeat_udp_addr;

        tokio::spawn(async move {
            Self::listen_for_heartbeats(backend_clone.data_servers.clone(), backend_udp, "Data")
                .await;
        });

        tokio::spawn(async move {
            Self::listen_for_heartbeats(
                frontend_clone.logic_servers.clone(),
                frontend_udp,
                "Logic",
            )
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
                let message = String::from_utf8_lossy(msg).trim().to_string();
                let mut servers = servers.lock().await;

                match message.as_str() {
                    "OK" => {
                        if let Some(server) = servers
                            .iter_mut()
                            .filter(|s| s.addr_identifier.is_some())
                            .find(|s| s.addr_identifier.unwrap() == from)
                        {
                            server.last_heartbeat = Some(Instant::now());
                            if !server.is_up {
                                println!("✅ {} {} is back online!", role, from);
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
                            let parsed_tcp = parts[0].parse::<SocketAddr>();
                            let parsed_udp = parts[1].parse::<SocketAddr>();

                            match (parsed_tcp, parsed_udp) {
                                (Ok(tcp_addr), Ok(udp_addr)) => {
                                    if let Some(server) = servers
                                        .iter_mut()
                                        .find(|s| s.udp_addr == udp_addr && s.tcp_addr == tcp_addr)
                                    {
                                        let _ = socket.send_to(b"OK", from).await;
                                        server.addr_identifier = Some(from);
                                        println!(
                                            "ℹ️ {} server identified with addr: {}",
                                            role, from
                                        );
                                    } else {
                                        println!(
                                            "❌ Address pair {} {} not recognized",
                                            tcp_addr, udp_addr
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
            let mut backends = self_.data_servers.lock().await;
            for server in backends.iter_mut() {
                if let Some(last) = server.last_heartbeat {
                    if last.elapsed() > Duration::from_secs(TIMEOUT) {
                        if server.is_up {
                            println!("⚠️ Data server {} timed out", server.udp_addr);
                        }
                        server.is_up = false;
                    }
                }
            }

            let mut frontends = self_.logic_servers.lock().await;
            for server in frontends.iter_mut() {
                if let Some(last) = server.last_heartbeat {
                    if last.elapsed() > Duration::from_secs(TIMEOUT) {
                        if server.is_up {
                            println!("⚠️ Logic server {} timed out", server.udp_addr);
                        }
                        server.is_up = false;
                    }
                }
            }
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    }

    async fn get_available_backend(&self) -> Option<SocketAddr> {
        let backends = self.data_servers.lock().await;
        let available: Vec<_> = backends.iter().filter(|s| s.is_up).collect();
        if available.is_empty() {
            return None;
        }
        let index = self.next_index.fetch_add(1, Ordering::Relaxed) % available.len();
        Some(available[index].tcp_addr)
    }

    async fn get_available_frontend(&self) -> Option<SocketAddr> {
        let frontends = self.logic_servers.lock().await;
        let available: Vec<_> = frontends.iter().filter(|s| s.is_up).collect();
        if available.is_empty() {
            return None;
        }
        let index = self.next_index.fetch_add(1, Ordering::Relaxed) % available.len();
        Some(available[index].tcp_addr)
    }
}
