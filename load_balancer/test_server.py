import socket
import threading
import time
import argparse

# === CONFIGURATION FROM ARGS ===
parser = argparse.ArgumentParser(description="Backend with heartbeat and TCP server")
parser.add_argument('--proxy-udp-ip', default='0.0.0.0')
parser.add_argument('--proxy-udp-port', type=int, required=True)
parser.add_argument('--proxy-tcp-ip', default='0.0.0.0')
parser.add_argument('--proxy-tcp-port', type=int, required=True)
parser.add_argument('--backend-tcp-ip', default='0.0.0.0')
parser.add_argument('--backend-tcp-port', type=int, required=True)
parser.add_argument('--backend-udp-ip', default='0.0.0.0')
parser.add_argument('--backend-udp-port', type=int, required=True)
parser.add_argument('--heartbeat-interval', type=float, default=1.0)

args = parser.parse_args()

PROXY_UDP_ADDR = (args.proxy_udp_ip, args.proxy_udp_port)
PROXY_TCP_ADDR = (args.proxy_tcp_ip, args.proxy_tcp_port)
BACKEND_TCP_IP = args.backend_tcp_ip
BACKEND_TCP_PORT = args.backend_tcp_port
BACKEND_UDP_IP = args.backend_udp_ip
BACKEND_UDP_PORT = args.backend_udp_port
HEARTBEAT_INTERVAL = args.heartbeat_interval

# === GLOBAL STATE ===
state_lock = threading.Lock()
send_mode = "ADDR"

# === HEARTBEAT SENDER ===
def send_heartbeat(udp_sock):
    global send_mode
    while True:
        with state_lock:
            if send_mode == "ADDR":
                msg = f"{args.backend_tcp_ip}:{args.backend_tcp_port} {args.backend_udp_ip}:{args.backend_udp_port}".encode()
            elif send_mode == "OK":
                msg = b"OK"
            else:
                msg = b"UNKNOWN"
        udp_sock.sendto(msg, PROXY_UDP_ADDR)
        time.sleep(HEARTBEAT_INTERVAL)

# === HEARTBEAT RECEIVER ===
def listen_for_proxy_messages(udp_sock):
    global send_mode
    print(f"📡 Listening for LB commands on UDP {args.backend_udp_ip}:{args.backend_udp_port}")
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

# === INTERACTIVE TERMINAL CLIENT ===
def send_tcp_message():
    print(f"🧑‍💻 Type messages to send to proxy at {PROXY_TCP_ADDR[0]}:{PROXY_TCP_ADDR[1]}")
    while True:
        try:
            message = input("📝> ").strip()
            if not message:
                continue

            with socket.create_connection(PROXY_TCP_ADDR) as sock:
                sock.sendall(message.encode())
                response = sock.recv(1024)
                print(f"✅ Response: {response.decode(errors='ignore')}")
        except Exception as e:
            print(f"❌ Error sending message: {e}")
        print("-" * 40)

# === MAIN ===
if __name__ == "__main__":
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind((BACKEND_UDP_IP, BACKEND_UDP_PORT))

    threading.Thread(target=send_heartbeat, args=(udp_sock,), daemon=True).start()
    threading.Thread(target=listen_for_proxy_messages, args=(udp_sock,), daemon=True).start()
    threading.Thread(target=send_tcp_message, daemon=True).start()

    tcp_server()
