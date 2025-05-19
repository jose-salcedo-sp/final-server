import socket
import threading
import time

# === CONFIGURATION ===
PROXY_UDP_ADDR = ("127.0.0.1", 5000)  # Load balancer's heartbeat UDP address
BACKEND_TCP_IP = "0.0.0.0"
BACKEND_TCP_PORT = 7002
BACKEND_UDP_IP = "0.0.0.0"
BACKEND_UDP_PORT = 7003
HEARTBEAT_INTERVAL = 1  # Seconds

state_lock = threading.Lock()
send_mode = "ADDR"  # Initial state: send "<udp> <tcp>"

# === HEARTBEAT SENDER ===
def send_heartbeat(udp_sock):
    global send_mode
    while True:
        with state_lock:
            if send_mode == "ADDR":
                msg = f"{BACKEND_UDP_IP}:{BACKEND_UDP_PORT} {BACKEND_TCP_IP}:{BACKEND_TCP_PORT}".encode()
            elif send_mode == "OK":
                msg = b"OK"
            else:
                msg = b"UNKNOWN"
        udp_sock.sendto(msg, PROXY_UDP_ADDR)
        time.sleep(HEARTBEAT_INTERVAL)


# === HEARTBEAT RECEIVER ===
def listen_for_proxy_messages(udp_sock):
    global send_mode
    print(f"📡 Listening for LB commands on UDP {BACKEND_UDP_IP}:{BACKEND_UDP_PORT}")
    while True:
        data, addr = udp_sock.recvfrom(128)
        message = data.decode().strip()
        print(f"📨 Received from proxy ({addr}): {message}")
        with state_lock:
            if message == "OK":
                send_mode = "OK"
            elif message == "AUTH":
                send_mode = "ADDR"

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
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind((BACKEND_UDP_IP, BACKEND_UDP_PORT))

    threading.Thread(target=send_heartbeat, args=(udp_sock,), daemon=True).start()
    threading.Thread(target=listen_for_proxy_messages, args=(udp_sock,), daemon=True).start()
    tcp_server()
