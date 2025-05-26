use serde::Deserialize;
use std::net::SocketAddr;

#[derive(Debug, Deserialize)]
#[serde(tag = "mode", rename_all = "lowercase")] // deserialize on "mode": "fl"/"ld"
pub enum Config {
    Fl {
        client_tcp_listening_addr: SocketAddr,
        logic_heartbeat_udp_addr: SocketAddr,
        logic_servers: Vec<String>,
    },
    Ld {
        logic_servers_tcp_listening_addr: SocketAddr,
        logic_servers_heartbeat_udp_addr: SocketAddr,
        logic_servers: Vec<String>,
        data_servers_tcp_listening_addr: SocketAddr,
        data_servers_hearbeat_udp_addr: SocketAddr,
        data_servers: Vec<String>,
    },
}
