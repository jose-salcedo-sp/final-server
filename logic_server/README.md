# Final Project - Logical Server

**Course::** Cómputo Distribuido  
**Professor:** Juan Carlos Pimentel  
**Submission Date:** 05 de Junio de 2025

## Team Members

-   Demian Velasco Gómez Llanos (0253139@up.edu.mx)
-   Hector Emiliano Flores Castellano (0254398@up.edu.mx)
-   Diego Amin Hernandez Pallares (0250146@up.edu.mx)

## Implemented Milestones

### 1. Concurrent TCP Architecture

-   Master socket implementation on TCP port  (`TCP_PORT`).
-   ach incoming connection triggers a `fork()` creating a dedicated child process for that client.
-   Enables concurrent handling of multiple sessions without blocking between them.
-   Complete isolation: if one client fails or terminates, others remain unaffected.
-   Uses `poll()` to multiplex events between client and backend TCP within the same process.

### 2. Communication Protocol (JSON)

-   All client-server communication is JSON-based.
-   Requires an integer `"action"` field specifying the operation type (e.g., `PING`, `CREATE_USER`, `SEND_MESSAGE`).
-   Basic actions like `PING` are handled locally by the logical server.
-   Complex actions (messaging, user validation, chat creation) are forwarded to the database backend.
-   Malformed JSON or missing required fields triggers a JSON error response.

### 3. Backend Communication

-   Each child process establishes an additional TCP socket to the backend at `127.0.0.1:6060`
-   The logical server acts as middleware between client and backend:
    -   Validates and preprocesses JSON.
    -   Forwards requests directly.
    -   Listens for backend responses and relays them to clients.
-   Modifies backend responses only in specific cases (e.g., injecting JWT tokens after successful`CREATE_USER` ).

### 4. UDP Daemon (Load Balancing)

-   A dedicated child process runs `fork()` for UDP heartbeats.
-   Periodic heartbeats broadcast service availability (IP and ports of the logical server).
-   Enables service discovery and future load balancing.

### 5. Signal Handling

-   `SIGINT`  enables graceful shutdown: closes TCP (`sd`) and UDP (`udp_sd`) sockets before termination.
-   `SIGCHLD` prevents zombie processes of  `fork()` via `waitpid(-1, ..., WNOHANG)`.
-   Both signals are managed using `signal()` in `main()` to ensure parent process stability.

### 6. JWT Token Authentication

-   Sensitive actions like `SEND_MESSAGE`, `GET_USER_INFO` or `CREATE_CHAT` require JWT authentication.
-   The logical server validates tokens locally before forwarding requests.
-   Invalid/missing tokens trigger direct (`unauthorized`) errors..
-   User creation generates a simulated token (`create_token`) injected into the response.
-   Designed for easy integration with real JWT in future iterations.

### 7. Robust Error Handling

-   Verifies critical steps: socket creation, backend connections, JSON formatting, required fields.
-   Errors are logged (`log_err`, `log_warn`, `log_info`) and returned as JSON responses.
-   Prevents crashes through proper resource cleanup and process isolation.

### 8. Network Integration Testing

-   Successfully tested direct client-to-data-server connections.
-   Verified client-to-data-server routing through the Load Balancer.
-   Confirmed logical server can query database backend and return results to clients.
-   Tested client-to-logical-server communication via UDP load balancer. **via UDP balancer**
-   Validated correct responses for `PING`, `CREATE_USER`, and `LOGIN`
-   Resolved initial IP configuration challenges between servers and load balancer.

## 📡 API Reference

All requests must be JSON objects containing an `"action"` field with a numeric value corresponding to the desired operation.
Certain actions may require an additional `"token"` field for authentication.

During user authentication (VALIDATE_USER), after successful validation, we perform a GET_USER_INFO query to retrieve the user's ID, then generate and return both the token and ID to the client. For subsequent requests, the client only needs to provide the token—the server automatically injects the user ID (extracted from the token) into the request to the database. Note that the ID may be mapped to different fields depending on the context (e.g., created_by, sender_id, etc.), so ensure consistent key naming across endpoints to avoid mismatches.

### Action `0` – Validate User


**Request:**

```json
{ "action": 0, "key": "username_or_email", "password": "..." }
```

**Response:**

```json
{
    "response_code": 200,
    "response_text": "...",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
}
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
{
    "response_code": 200,
    "response_text": "...",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
}
```

---

### Action `3` – Get User Info

**Request:**

```json
{
    "action": 3,
    "key": "username_or_email",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
}
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
    "participant_ids": [2, 3, 4],
    "token": "..."
}
```

**Response:**

```json
{
    "response_code": 200,
    "response_text": "Chat Group Chat was succesfully created with 4 users"
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
    "participant_ids": [5, 6],
    "token": "..."
}
```

**Response:**

```json
{
    "response_code": 200,
    "response_text": "Chat 1 has succesfully added 2 users"
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
    "message_type": "text",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
}
```

**Response:**

```json
{
    "response_code": 200,
    "response_text": "Message from 1 was succesfully sent to chat 1"
}
```

---

### Action `7` – Get Chats

**Request:**

```json
{
    "action": 7,
    "user_id": 1,
    "last_update_timestamp": "2023-01-01 00:00:00",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
}
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
            "last_message_content": "Hello everyone!",
            "last_message_type": "text",
            "last_message_timestamp": "2023-01-01 12:00:00",
            "last_message_sender": "username"
        }
    ]
}
```

---

### Action `8` – Get Chat Messages

**Request:**

```json
{
    "action": 8,
    "chat_id": 1,
    "last_update_timestamp": "2023-01-01 00:00:00",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
}
```

**Response:**

```json
{
    "response_code": 200,
    "response_text": "5 messages succesfully retreived",
    "messages_array": [
        {
            "message_id": 1,
            "sender_id": 1,
            "sender_username": "username",
            "content": "Hello everyone!",
            "message_type": "text",
            "created_at": "2023-01-01 12:00:00"
        }
    ]
}
```

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

| Code | Meaning                  |
| ---- | ------------------------ |
| 200  | Success                  |
| 202  | Success but empty        |
| 400  | Bad request              |
| 404  | Unknown action           |
| 503  | Service Unavailable (DB) |

---

## ⚠️ Limitations

-   **Max participants per chat:** 10 (`MAX_PARTICIPANTS`)
-   **Max chats retrieved at once:** 100 (`MAX_CHATS`)
-   **Max messages retrieved at once:** 200 (`MAX_MESSAGES`)

---
