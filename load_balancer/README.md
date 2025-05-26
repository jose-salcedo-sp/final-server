# LOAD BALANCER

---

## `FRONTEND <-> LOGIC` REVERSE PROXY

### SETUP

1. Make sure you have cargo tool installed
2. Create a `frontend_logic_rp_config.json` file in the root of your project that has the following shape:

```json
{
    "mode": "fl", // frontend to logic reverse proxy
    "client_tcp_listening_addr": "0.0.0.0:3001", // tcp addr to which the frontend can connect to
    "logic_heartbeat_udp_addr": "0.0.0.0:5000", // udp addr to which your logic server will be sending status updates
    "logic_servers": [
        // <udp> <tcp> listening addrs for each of the logical servers
        "0.0.0.0:7000 0.0.0.0:7001",
        "0.0.0.0:7002 0.0.0.0:7003"
    ]
}
```

3. Build the project using `cargo build`
4. Run the tcp server using you config file

```bash
./target/debug/load_balancer --config-path ./frontend_logic_rp_config.json
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
🫡 Received ID from 127.0.0.1:7001 — matched logic server 0.0.0.0:7001, responded with OK
✅ Logic Server 127.0.0.1:7001 is back online!
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

## `LOGIC <-> DATA` REVERSE PROXY

### SETUP

1. Make sure you have cargo tool installed
2. Create a `logic_data_rp_config.json` file in the root of your project that has the following shape:

```json
{
    "mode": "ld",
    "logic_servers_tcp_listening_addr": "0.0.0.0:3001",
    "logic_servers_heartbeat_udp_addr": "0.0.0.0:5001",
    "data_servers_tcp_listening_addr": "0.0.0.0:3000",
    "data_servers_hearbeat_udp_addr": "0.0.0.0:5000",
    "logic_servers": [
        "0.0.0.0:7002 0.0.0.0:7003"
    ],
    "data_servers": [
        "0.0.0.0:7000 0.0.0.0:7001"
    ]
}
```

3. Build the project using `cargo build`
4. Run the tcp server using you config file

```bash
./target/debug/load_balancer --config-path ./logic_data_rp_config.json
```

5. Success! You should see a message like this:

```
ℹ️ Running LOGIC <-> DATA load balancer
🔌 Logic TCP listening on 0.0.0.0:3001
🔌 Data TCP listening on 0.0.0.0:3000
```

### TEST USAGE

Attached to this repo comes a `test_server.py` file which you should be able to run a mock logical server by running the following commands:

`Logic Server`:

```bash
python3 test_server.py \
  --proxy-udp-port 5001 \
  --proxy-tcp-port 3001 \
  --backend-tcp-port 7002 \
  --backend-udp-port 7003
```

`Data Server`

```bash
python3 test_server.py \
  --proxy-udp-port 5000 \
  --proxy-tcp-port 3000 \
  --backend-tcp-port 7000 \
  --backend-udp-port 7001
```

You should see this on your load balancer terminal:

```
ℹ️ Running LOGIC <-> DATA load balancer
🔌 Logic TCP listening on 0.0.0.0:3001
🔌 Data TCP listening on 0.0.0.0:3000
🫡 Received ID from 127.0.0.1:7003 — matched Logic Server 0.0.0.0:7003, responded with OK
✅ Logic Server 127.0.0.1:7003 is back online!
🫡 Received ID from 127.0.0.1:7001 — matched Data Server 0.0.0.0:7001, responded with OK
✅ Data Server 127.0.0.1:7001 is back online!
```

Now you should be able to send messages between both servers through the proxy in a full duplex non-blocking communication through the terminal.
