use clap::Parser;
use std::io::Write;
use std::net::{SocketAddr, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;

mod proxy;

use proxy::ReverseProxy;

#[derive(Parser, Debug)]
#[command(name = "Chat Load Balancer")]
struct Args {
    /// TCP listening port for users
    #[arg(long, default_value = "0.0.0.0:3030")]
    frontend_tcp_addr: SocketAddr,
    /// TCP listening port for users
    #[arg(long, default_value = "0.0.0.0:3031")]
    backend_tcp_addr: SocketAddr,
    /// UDP port to receive backend heartbeats
    #[arg(long, default_value = "0.0.0.0:5001")]
    backend_heartbeat_addr: SocketAddr,
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let args = Args::parse();
    let proxy = ReverseProxy::new(vec![]);

    proxy
        .clone()
        .check_backends_availability(args.backend_heartbeat_addr)
        .await;

    let udp_servers_listener = TcpListener::bind(args.frontend_tcp_addr).await?;
    println!("Listening on {}", args.frontend_tcp_addr);

    loop {
        let (mut socket, addr) = udp_servers_listener.accept().await?;
        let proxy = proxy.clone();

        tokio::spawn(async move {
            println!("ℹ️ Client {} connected to TCP stream!", addr);

            loop {
                let mut buf = vec![0u8; 1024];
                match socket.read(&mut buf).await {
                    Ok(n) if n == 0 => {
                        println!("ℹ️ Client {} disconnected", addr);
                        return;
                    }
                    Ok(n) => {
                        if let Some(backend_addr) = proxy.get_available_backend().await {
                            match TcpStream::connect(backend_addr) {
                                Ok(mut backend_stream) => {
                                    if let Err(_) = backend_stream.write_all(&buf[..n]) {
                                        let _ =
                                            socket.write_all(b"Failed to write to backend\n").await;
                                    }
                                }
                                Err(_) => {
                                    let _ =
                                        socket.write_all(b"Could not connect to backend\n").await;
                                }
                            }
                        } else {
                            let _ = socket.write_all(b"No available backend\n").await;
                        }
                    }
                    Err(e) => {
                        eprintln!("❌ Client socket error: {:?}", e);
                        return;
                    }
                }
            }
        });
    }
}
