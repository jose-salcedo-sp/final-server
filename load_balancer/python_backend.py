import socket
import threading
import time

# === CONFIGURATION ===
PROXY_UDP_ADDR = ("127.0.0.1", 5001)  # Where to send heartbeats
BACKEND_TCP_IP = "127.0.0.1"
BACKEND_TCP_PORT = 7000               # Port this backend listens on
HEARTBEAT_INTERVAL = 1                # Seconds between heartbeats

# === HEARTBEAT SENDER ===
def send_heartbeat():
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tcp_addr_string = f"{BACKEND_TCP_IP}:{BACKEND_TCP_PORT}".encode("utf-8")
    while True:
        udp_sock.sendto(tcp_addr_string, PROXY_UDP_ADDR)
        time.sleep(HEARTBEAT_INTERVAL)

# === TCP SERVER ===
def tcp_server():
    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp_sock.bind((BACKEND_TCP_IP, BACKEND_TCP_PORT))
    tcp_sock.listen()

    print(f"🟢 Backend TCP server listening on {BACKEND_TCP_IP}:{BACKEND_TCP_PORT}")

    while True:
        conn, addr = tcp_sock.accept()
        print(f"📥 Received connection from proxy: {addr}")
        data = conn.recv(1024)
        if data:
            print(f"💬 Message from client via proxy: {data.decode('utf-8', errors='ignore')}")
            conn.sendall(b"Received by backend\n")
        conn.close()

# === MAIN ===
if __name__ == "__main__":
    threading.Thread(target=send_heartbeat, daemon=True).start()
    tcp_server()
