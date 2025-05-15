use std::collections::{HashMap, HashSet};
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::UdpSocket;
use tokio::sync::Mutex;

use crate::client_messages::{ChatJoin, MsgSend, UUIDv7};

#[derive(Debug)]
pub struct SubscribedChat {
    pub chat_id: UUIDv7,
    pub subscribed_users: HashSet<SocketAddr>,
}

#[derive(Debug)]
pub struct BackendServer {
    pub address: SocketAddr,
    pub last_heartbeat: Option<Instant>,
    pub is_up: bool,
}

#[derive(Debug)]
pub struct ProxyServer {
    pub chats: Mutex<HashMap<UUIDv7, SubscribedChat>>,
    pub backends: Mutex<Vec<BackendServer>>,
}

impl ProxyServer {
    pub fn new(backend_addresses: Vec<SocketAddr>) -> Arc<Self> {
        let backends = backend_addresses
            .into_iter()
            .map(|addr| BackendServer {
                address: addr,
                last_heartbeat: None,
                is_up: false,
            })
            .collect();

        Arc::new(Self {
            chats: Mutex::new(HashMap::new()),
            backends: Mutex::new(backends),
        })
    }

    pub async fn join_chat(&self, join: ChatJoin, user_addr: SocketAddr) {
        let mut chats = self.chats.lock().await;
        let chat = chats.entry(join.chat_id).or_insert_with(|| SubscribedChat {
            chat_id: join.chat_id,
            subscribed_users: HashSet::new(),
        });

        chat.subscribed_users.insert(user_addr);
    }

    pub async fn send_message_to_chat(&self, msg: MsgSend) {
        let chats = self.chats.lock().await;
        if let Some(chat) = chats.get(&msg.chat_id) {
            let payload = serde_json::to_string(&msg).unwrap();
            for user in &chat.subscribed_users {
                // if *user != msg.user_id {
                // TODO: send `payload` to user socket (via channel or socket lookup)
                println!("Would send to {}: {}", user, payload);
                // }
            }
        }
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

    async fn listen_for_heartbeats(self: Arc<Self>, bind_addr: SocketAddr) {
        let socket = UdpSocket::bind(bind_addr)
            .await
            .expect("Failed to bind UDP socket");
        let mut buf = [0u8; 16];

        loop {
            if let Ok((n, from)) = socket.recv_from(&mut buf).await {
                let msg = &buf[..n];
                if msg == b"OK" {
                    let mut backends = self.backends.lock().await;
                    if let Some(server) = backends.iter_mut().find(|s| s.address == from) {
                        server.last_heartbeat = Some(Instant::now());
                        server.is_up = true;
                    } else {
                       backends.push(BackendServer {
                           address: from,
                           last_heartbeat: None,
                           is_up: true,
                       });

                       println!("Added {} to the list of backends!", from);
                    }
                    println!("💗 Received heartbeat from {}", from);
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
                                println!("⚠️ Backend {} timed out", backend.address);
                            }
                            backend.is_up = false;
                        }
                    } else {
                        backend.is_up = false;
                    }
                }
            }

            tokio::time::sleep(Duration::from_millis(500)).await;
        }
    }
}
