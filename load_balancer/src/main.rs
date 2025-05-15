use clap::Parser;
use std::net::SocketAddr;
use tokio::io::AsyncReadExt;
use tokio::net::TcpListener;

mod client_messages;
mod proxy;

use client_messages::ClientMessage;
use proxy::ProxyServer;

#[derive(Parser, Debug)]
#[command(name = "Chat Load Balancer")]
struct Args {
    /// TCP listening port for users
    #[arg(long, default_value = "0.0.0.0:3030")]
    frontend_tcp_addr: SocketAddr,
    /// UDP port to receive backend heartbeats
    #[arg(long, default_value = "0.0.0.0:5001")]
    backend_heartbeat_addr: SocketAddr,
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let args = Args::parse();

    let proxy = ProxyServer::new(vec![
        "127.0.0.1:6000".parse().unwrap(),
        "127.0.0.1:6001".parse().unwrap(),
    ]);

    proxy.clone().check_backends_availability("0.0.0.0:5001".parse().unwrap()).await;

    let listener = TcpListener::bind(args.frontend_tcp_addr).await?;
    println!("Listening on {}", args.frontend_tcp_addr);

    loop {
        let (mut socket, addr) = listener.accept().await?;
        let proxy = proxy.clone();

        tokio::spawn(async move {
            let mut buf = vec![0u8; 1024];
            match socket.read(&mut buf).await {
                Ok(n) if n == 0 => return,
                Ok(n) => {
                    let raw = String::from_utf8_lossy(&buf[..n]);
                    match serde_json::from_str::<ClientMessage>(&raw) {
                        Ok(ClientMessage::ChatJoin(join)) => {
                            proxy.join_chat(join, addr).await;
                        }
                        Ok(ClientMessage::MsgSend(msg)) => {
                            proxy.send_message_to_chat(msg).await;
                        }
                        Ok(ClientMessage::Login(_)) => {
                            println!("Login not implemented yet");
                        }
                        Err(e) => {
                            eprintln!("Invalid client message: {}", e);
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Connection error: {}", e);
                }
            }
        });
    }
}
