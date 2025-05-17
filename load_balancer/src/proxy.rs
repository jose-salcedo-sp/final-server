use std::net::SocketAddr;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::UdpSocket;
use tokio::sync::Mutex;

#[derive(Debug, Clone)]
pub struct Server {
    pub udp_addr: SocketAddr,
    pub tcp_addr: SocketAddr,
    pub last_heartbeat: Option<Instant>,
    pub is_up: bool,
}

#[derive(Debug)]
pub struct ReverseProxy {
    pub backends: Mutex<Vec<Server>>,
    next_index: AtomicUsize,
}

impl ReverseProxy {
    pub fn new(backend_addresses: Vec<(SocketAddr, SocketAddr)>) -> Arc<Self> {
        let backends = backend_addresses
            .into_iter()
            .map(|addrs| Server {
                tcp_addr: addrs.0,
                udp_addr: addrs.1,
                last_heartbeat: None,
                is_up: false,
            })
            .collect();

        return Arc::new(Self {
            backends: Mutex::new(backends),
            next_index: AtomicUsize::new(0),
        });
    }

    pub async fn check_backends_availability(self: Arc<Self>, bind_addr: SocketAddr) {
        // Start a single heartbeat listener bound to the provided address
        let self_clone = self.clone();
        tokio::spawn(async move {
            self_clone.listen_for_heartbeats(bind_addr).await;
        });

        // Start the heartbeat expiry checker
        let self_clone = self.clone();
        tokio::spawn(async move {
            self_clone.check_heartbeat_expiry().await;
        });
    }

    pub async fn get_available_backend(&self) -> Option<SocketAddr> {
        let backends = self.backends.lock().await;
        let available: Vec<_> = backends.iter().filter(|s| s.is_up).collect();
        if available.is_empty() {
            return None;
        }

        let index = self.next_index.fetch_add(1, Ordering::Relaxed) % available.len();
        return Some(available[index].tcp_addr);
    }

    async fn listen_for_heartbeats(self: Arc<Self>, bind_addr: SocketAddr) {
        let socket = UdpSocket::bind(bind_addr)
            .await
            .expect("Failed to bind UDP socket");
        let mut buf = [0u8; 128]; // increase size to receive full message

        loop {
            if let Ok((n, from)) = socket.recv_from(&mut buf).await {
                let msg = &buf[..n];
                let message = String::from_utf8_lossy(msg);

                let mut backends = self.backends.lock().await;
                if let Some(server) = backends.iter_mut().find(|s| s.udp_addr == from) {
                    server.last_heartbeat = Some(Instant::now());

                    if !server.is_up {
                        println!("✅ Server {} is back online!", from);
                    }

                    server.is_up = true;

                    if let Ok(tcp_addr) = message.parse::<SocketAddr>() {
                        server.tcp_addr = tcp_addr;
                    } else {
                        eprintln!("⚠️ Invalid TCP address from backend: {}", message);
                    }
                } else {
                    let tcp_addr = message.parse::<SocketAddr>().unwrap();
                    println!("🆕 New backend: UDP={} TCP={:?}", from, tcp_addr);

                    backends.push(Server {
                        udp_addr: from,
                        tcp_addr,
                        last_heartbeat: Some(Instant::now()),
                        is_up: true,
                    });
                }
            }
        }
    }

    async fn check_heartbeat_expiry(self: Arc<Self>) {
        loop {
            {
                let mut backends = self.backends.lock().await;
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
}
