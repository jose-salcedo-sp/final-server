use serde::Deserialize;
use std::net::SocketAddr;

#[derive(Debug, Deserialize)]
#[serde(tag = "mode", rename_all = "lowercase")] // deserialize on "mode": "fl"/"ld"
pub enum Config {
    Fl {
        backend_tcp_addr: SocketAddr,
        backend_heartbeat_udp_addr: SocketAddr,
        frontend_tcp_addr: SocketAddr,
        backend_addrs: Vec<String>,
    },
    Ld {
        backend_tcp_addr: SocketAddr,
        backend_heartbeat_udp_addr: SocketAddr,
        frontend_tcp_addr: SocketAddr,
        frontend_heartbeat_udp_addr: SocketAddr,
        backend_addrs: Vec<String>,
        frontend_addrs: Vec<String>,
    },
}
