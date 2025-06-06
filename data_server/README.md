# 📡 Data Server

A lightweight C-based server that handles database operations for a messaging application. It manages user authentication, chat creation, message exchange, and data retrieval via a JSON-based API, interfacing with a MySQL backend.

---

## 📁 Table of Contents

* [Prerequisites](#prerequisites)
* [Installation](#installation)

  * [1. Clone & Setup](#1-clone--setup)
  * [2. MySQL Database Initialization](#2-mysql-database-initialization)
  * [3. Environment Configuration](#3-environment-configuration)
  * [4. Dependencies](#4-dependencies)
  * [5. Build & Run](#5-build--run)
* [API Reference](#api-reference)
* [Data Structures](#data-structures)
* [Testing](#testing)
* [Error Handling](#error-handling)
* [Troubleshooting](#troubleshooting)
* [Limitations](#limitations)
* [License](#license)

---

## 📦 Prerequisites

Ensure the following dependencies are installed on your **Linux** system:

* **MySQL Server (Community Edition)**
* **GCC (C Compiler)**
* **Make**
* **MySQL C Connector library (`libmysqlclient-dev`)**

---

## ⚒️ Installation

### 1. Clone & Setup

```bash
# Clone this repository
$ git clone [https://github.com/your_org/data-server.git](https://github.com/jose-salcedo-sp/final-server.git)
$ cd data-server
```

### 2. MySQL Database Initialization

```bash
# Log into MySQL as root
$ mysql -u root -p

# Inside the MySQL shell:
mysql> CREATE DATABASE messengerdatabase;
mysql> CREATE USER 'db_admin'@'%' IDENTIFIED BY 'your_password';
mysql> GRANT ALL PRIVILEGES ON messengerdatabase.* TO 'db_admin'@'%';
mysql> FLUSH PRIVILEGES;
```

Create the tables by running the SQL schema (see schema in next section or in `schema.sql` file).
Execute $ mysql -u db_admin -p messengerdatabase < schema.sql to load everything at once.

### 3. Environment Configuration

Create a `.env` file inside the `data_server/` folder:

```env
DB_HOST=localhost
DB_USER=db_admin
DB_PASS=your_password
DB_NAME=messengerdatabase
```

### 4. Dependencies

```bash
# Install dependencies
$ sudo apt update
$ sudo apt install build-essential libmysqlclient-dev make
```

If using **WSL**, implement port forwarding from an admin CMD for your TCP and UDP ports (e.g., 5000 and 5001):

```powershell
netsh interface portproxy add v4tov4 listenport=5000 listenaddress=0.0.0.0 connectport=5000 connectaddress=<WSL_IP>
netsh interface portproxy add v4tov4 listenport=5001 listenaddress=0.0.0.0 connectport=5001 connectaddress=<WSL_IP>
```

### 5. Build & Run

```bash
# Add your load balancers to config
$ cat load_balancers.json
[
  {"ip": "10.7.27.134", "udp_port": 5002, "tcp_port": 3001},
  {"ip": "10.7.27.135", "udp_port": 5002, "tcp_port": 3001}
]

# Compile
$ make

# Run the server
$ ./server
```

---

## 📡 API Reference

All requests are JSON objects with an `"action"` field.

---

### Action `0` — Validate User

**Request:**

```json
{ "action": 0, "key": "username_or_email" }
```

**Response:**

```json
{ "response_code": 200, "response_text": "...", "password_hash": "..." }
```

---

### Action `2` — Create User

**Request:**

```json
{
  "action": 2,
  "username": "new_user",
  "email": "user@example.com",
  "password": "hashed_password"
}
```

**Response:**

```json
{ "response_code": 200, "response_text": "User new_user with email user@example.com has been stored in the database" }
```

---

### Action `3` — Get User Info

**Request:**

```json
{ "action": 3, "key": "username_or_email" }
```

**Response:**

```json
{
  "response_code": 200,
  "response_text": "User username was found with the ID: 1",
  "user_id": 1,
  "username": "username",
  "email": "user@example.com"
}
```

---

### Action `4` — Create Chat

**Request:**

```json
{
  "action": 4,
  "is_group": true,
  "chat_name": "Group Chat",
  "created_by": 1,
  "participant_ids": [2, 3]
}
```

**Response:**

```json
{ "response_code": 200, "response_text": "Chat Group Chat was succesfully created with 3 users" }
```

---

### Action `5` — Add to Group Chat

**Request:**

```json
{
  "action": 5,
  "chat_id": 1,
  "added_by": 1,
  "participant_ids": [4, 5]
}
```

**Response:**

```json
{ "response_code": 200, "response_text": "Chat 1 has succesfully added 2 users" }
```

---

### Action `6` — Send Message

**Request:**

```json
{
  "action": 6,
  "chat_id": 1,
  "sender_id": 1,
  "content": "Hello World!",
  "message_type": "text"
}
```

**Response:**

```json
{ "response_code": 200, "response_text": "Message from 1 was succesfully sent to chat 1" }
```

---

### Action `7` — Get Chats

**Request:**

```json
{ "action": 7, "user_id": 1, "last_update_timestamp": "2023-01-01 00:00:00" }
```

**Response:**

```json
{
  "response_code": 200,
  "response_text": "2 chats succesfully retreived",
  "chats_array": [
    {
      "chat_id": 1,
      "chat_name": "Group Chat",
      "last_message_content": "Hi",
      "last_message_type": "text",
      "last_message_timestamp": "2023-06-01 12:34:56",
      "last_message_sender": "user1"
    }
  ]
}
```

---

### Action `8` — Get Chat Messages

**Request:**

```json
{ "action": 8, "chat_id": 1, "last_update_timestamp": "2023-01-01 00:00:00" }
```

**Response:**

```json
{
  "response_code": 200,
  "response_text": "3 messages succesfully retreived",
  "messages_array": [
    {
      "message_id": 1,
      "sender_id": 2,
      "sender_username": "user2",
      "content": "Hello",
      "message_type": "text",
      "created_at": "2023-06-01 12:00:00"
    }
  ]
}
```

---

### Action `9` — Get Chat Info

**Request:**

```json
{ "action": 9, "chat_id": 1 }
```

**Response:**

```json
{
  "response_code": 200,
  "response_text": "Chat info for ID 1 retrieved successfully",
  "chat_id": 1,
  "chat_name": "Group Chat",
  "is_group": true,
  "participants": [
    { "user_id": 1, "username": "admin", "is_admin": 1 },
    { "user_id": 2, "username": "user2", "is_admin": 0 }
  ]
}
```

---

### Action `10` — Remove From Chat

**Request:**

```json
{
  "action": 10,
  "chat_id": 1,
  "removed_by": 1,
  "participant_ids": [3]
}
```

**Response:**

```json
{ "response_code": 200, "response_text": "Removed 1 out of 1 participants from chat 1" }
```

---

### Action `11` — Exit Chat

**Request:**

```json
{
  "action": 11,
  "chat_id": 1,
  "user_id": 3
}
```

**Response:**

```json
{ "response_code": 200, "response_text": "User 3 exited chat 1 successfully" }
```

---

## 📀 Data Structures

### `User`

```c
typedef struct {
    char* username;
    char* email;
    char* hash_password;
    int id;
    int is_admin;
} User;
```

### `Chat`

```c
typedef struct {
    int id;
    char *chat_name;
    int is_group;
    int created_by;
    char *last_message_content;
    char *last_message_type;
    char *last_message_timestamp;
    char *last_message_by;
} Chat;
```

### `Message`

```c
typedef struct {
    int message_id;
    int chat_id;
    int sender_id;
    char *created_at;
    char *sender_username;
    char *content;
    char *message_type;
} Message;
```
---
## 🧪 Testing
In the root there is a test_client.py script (Python 3) that sends several sample JSON requests:

Adjusts at startup: SERVER_IP = '127.0.0.1'

SERVER_PORT = 5000

Execute: $ python3 test_client.py

The script will display each request and its response. You can edit the test_cases list to add or modify tests.

---

## ❗ Error Handling

All API responses include:

| Code | Meaning                |
| ---- | ---------------------- |
| 200  | Success                |
| 202  | Success but empty      |
| 400  | Bad request            |
| 403  | Forbidden (e.g. perms) |
| 404  | Unknown action         |
| 500  | Internal server error  |

---

## 🛠 Troubleshooting
cJSON.h: No such file or directory”.
Verify that lib/cjson/cJSON.c and cJSON.h exist.
If not, clone cJSON: 
$ git clone https://github.com/DaveGamble/cJSON.git lib/cjson

Connection refused” when connecting MySQL
Make sure the MySQL server is running (systemctl status mysql or sudo service mysql status).
Verify that the credentials in .env match the user created in MySQL.
No heartbeat log appears
Check that load_balancers.json exists and has at least one valid entry (IP and port).
If it is malformed, the UDP thread terminates without printing anything.
Duplicate entry when creating user
It means that an identical username or email already exists in users. Use a different value or delete the duplicate in the DB.

---

## ⚠️ Limitations

* **Max participants per chat:** 10 (`MAX_PARTICIPANTS`)
* **Max chats retrieved at once:** 100 (`MAX_CHATS`)
* **Max messages retrieved at once:** 200 (`MAX_MESSAGES`)

---
