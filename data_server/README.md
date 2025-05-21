# 📡 Data Server

A lightweight C-based server that handles database operations for a messaging application. It manages user authentication, chat creation, message exchange, and data retrieval via a JSON-based API, interfacing with a MySQL backend.

---

## 📑 Table of Contents

* [Prerequisites](#prerequisites)
* [Installation](#installation)

  * [1. Database Setup](#1-database-setup)
  * [2. Environment Variables](#2-environment-variables)
  * [3. Compilation](#3-compilation)
* [API Reference](#api-reference)
* [Data Structures](#data-structures)
* [Error Handling](#error-handling)
* [Limitations](#limitations)
* [License](#license)

---

## 📦 Prerequisites

Ensure the following dependencies are installed:

* **MySQL Server**
* **C Compiler** (e.g., GCC)
* **MySQL C Connector library**
* **cJSON library**

---

## 🛠️ Installation

### 1. Database Setup

Execute the following SQL to initialize the database schema:

<details>
<summary>Click to expand SQL schema</summary>

```sql
-- users table
CREATE TABLE users (
  user_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  username VARCHAR(50) NOT NULL,
  email VARCHAR(100) NOT NULL,
  password_hash TEXT NOT NULL,
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (user_id),
  UNIQUE KEY username (username),
  UNIQUE KEY email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- chats table
CREATE TABLE chats (
  chat_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  is_group TINYINT(1) DEFAULT '0',
  chat_name VARCHAR(100),
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  last_message_id BIGINT UNSIGNED DEFAULT NULL,
  PRIMARY KEY (chat_id),
  KEY fk_chats_last_message (last_message_id),
  CONSTRAINT fk_chats_last_message FOREIGN KEY (last_message_id)
    REFERENCES messages (message_id)
    ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- chat_participants table
CREATE TABLE chat_participants (
  chat_id BIGINT UNSIGNED NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  joined_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  is_admin TINYINT(1) DEFAULT '0',
  PRIMARY KEY (chat_id, user_id),
  CONSTRAINT fk_cp_chat FOREIGN KEY (chat_id)
    REFERENCES chats (chat_id) ON DELETE CASCADE,
  CONSTRAINT fk_cp_user FOREIGN KEY (user_id)
    REFERENCES users (user_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- messages table
CREATE TABLE messages (
  message_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  chat_id BIGINT UNSIGNED,
  sender_id BIGINT UNSIGNED,
  content TEXT,
  message_type VARCHAR(20) DEFAULT 'text',
  created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
  is_deleted TINYINT(1) DEFAULT '0',
  PRIMARY KEY (message_id),
  KEY fk_message_chat (chat_id),
  KEY fk_message_sender (sender_id),
  CONSTRAINT fk_message_chat FOREIGN KEY (chat_id)
    REFERENCES chats (chat_id) ON DELETE SET NULL,
  CONSTRAINT fk_message_sender FOREIGN KEY (sender_id)
    REFERENCES users (user_id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

</details>

---

### 2. Environment Variables

Set the following variables in your shell or `.env` file:

```bash
export DB_HOST="your_database_host"
export DB_USER="your_database_username"
export DB_PASS="your_database_password"
export DB_NAME="your_database_name"
```

---

### 3. Compilation

Compile using GCC:

```bash
gcc -o server server.c -lmysqlclient -lcjson
```
> Alternatively run command make to handle compilation

---

## 📡 API Reference

All requests are JSON objects with an `"action"` field.

### Action `0` – Validate User

**Request:**

```json
{ "action": 0, "key": "username_or_email" }
```

**Response:**

```json
{ "response_code": 200, "response_text": "...", "password_hash": "..." }
```

---

### Action `2` – Create User

**Request:**

```json
{
  "action": 2,
  "username": "new_user",
  "email": "user@example.com",
  "password": "password_hash"
}
```

**Response:**

```json
{ "response_code": 200, "response_text": "..." }
```

---

### Action `3` – Get User Info

**Request:**

```json
{ "action": 3, "key": "username_or_email" }
```

**Response:**

```json
{
  "response_code": 200,
  "user_id": 1,
  "username": "username",
  "email": "user@example.com"
}
```

---

### Action `4` – Create Chat

**Request:**

```json
{
  "action": 4,
  "is_group": true,
  "chat_name": "Group Chat",
  "created_by": 1,
  "participant_ids": [2, 3, 4]
}
```

---

### Action `5` – Add to Group Chat

**Request:**

```json
{
  "action": 5,
  "chat_id": 1,
  "added_by": 1,
  "participant_ids": [5, 6]
}
```

---

### Action `6` – Send Message

**Request:**

```json
{
  "action": 6,
  "chat_id": 1,
  "sender_id": 1,
  "content": "Hello everyone!",
  "message_type": "text"
}
```

---

### Action `7` – Get Chats

**Request:**

```json
{
  "action": 7,
  "user_id": 1,
  "last_update_timestamp": "2023-01-01 00:00:00"
}
```

---

### Action `8` – Get Chat Messages

**Request:**

```json
{
  "action": 8,
  "chat_id": 1,
  "last_update_timestamp": "2023-01-01 00:00:00"
}
```

---

## 💾 Data Structures

### `User`

```c
typedef struct {
    char* username;
    char* email;
    char* hash_password;
    int id;
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

## ❗ Error Handling

All API responses include a `response_code` and `response_text`:

| Code | Meaning          |
| ---- | --------------   |
| 200  | Success          |
| 202  | Success but empty|
| 400  | Bad request      |
| 404  | Unknown action   |

---

## ⚠️ Limitations

* **Max participants per chat:** 10 (`MAX_PARTICIPANTS`)
* **Max chats retrieved at once:** 100 (`MAX_CHATS`)
* **Max messages retrieved at once:** 200 (`MAX_MESSAGES`)

---
