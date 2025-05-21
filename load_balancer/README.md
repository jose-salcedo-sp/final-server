# LOAD BALANCER
---
## `FRONTEND <-> LOGIC` REVERSE PROXY
### SETUP

1. Make sure you have cargo tool installed
2. Create a `load_balancer_config.json` file in the root of your project that has the following shape:

```json
{
    "mode": "fl", // frontend to logic reverse proxy
    "frontend_tcp_addr": "0.0.0.0:3001", // tcp addr to which the frontend can connect to
    "backend_heartbeat_udp_addr": "0.0.0.0:5000", // udp addr to which your logic server will be sending status updates
    "backend_addrs": [ // <udp> <tcp> listening addrs for each of the logical servers
        "0.0.0.0:7000 0.0.0.0:7001",
        "0.0.0.0:7002 0.0.0.0:7003"
    ]
}
```

3. Build the project using `cargo build`
4. Run the tcp server using you config file
```bash
./target/debug/load_balancer --config-path ./load_balancer_config.json
```
5. Success! You should see a message like this:
```
ℹ️ Running FE <-> LOGIC load balancer
🔌 FL Listening on 0.0.0.0:3001
```

### TEST USAGE
Attached to this repo comes a `test_server.py` file which you should be able to run a mock logical server by running the following command:

```bash
python3 test_server.py \
  --proxy-udp-port 5000 \
  --proxy-tcp-port 3000 \
  --backend-tcp-port 7000 \
  --backend-udp-port 7001
```

You should be able to test it using a Netcat client
```bash
nc 127.0.0.1 3001
Hello from frontend
```

Your message should have passed through the proxy and sent to your backend:
Proxy terminal:
```
ℹ️ Running FE <-> LOGIC load balancer
🔌 FL Listening on 0.0.0.0:3001
🫡 Received ID from 127.0.0.1:7001 — matched server 0.0.0.0:7001, responded with OK
✅ Server 127.0.0.1:7001 is back online!
ℹ️ FL Client 127.0.0.1:50811 connected
🔁 Forwarding traffic between client and backend 0.0.0.0:7000
📊 Connection closed: client→backend=20B, backend→client=20B
```
Backend terminal:
```
📡 Listening for LB commands on UDP 0.0.0.0:7001
🧑‍💻 Type messages to send to proxy at 0.0.0.0:3000
📝> 🟢 Backend TCP server listening on 0.0.0.0:7000
📨 Received from proxy (('127.0.0.1', 5000)): OK
📥 Received connection from proxy: ('127.0.0.1', 50812)
💬 Message from client via proxy: Hello from frontend
```
And your connection from the client should be terminated.
