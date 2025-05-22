# Avance Proyecto Final - Servidor Lógico

**Materia:** Cómputo Distribuido  
**Profesor:** Juan Carlos Pimentel  
**Fecha de entrega:** 05 de Junio de 2025

## Integrantes del equipo

-   Demian Velasco Gómez Llanos (0253139@up.edu.mx)
-   Hector Emiliano Flores Castellano (0254398@up.edu.mx)
-   Diego Amin Hernandez Pallares (0250146@up.edu.mx)

## Hitos implementados

### 1. Arquitectura TCP concurrente

-   Socket TCP maestro en puerto configurado
-   Fork por cada conexión de cliente
-   Sesiones aisladas y concurrentes

### 2. Procesamiento JSON

-   Protocolo basado en JSON
-   Campo `action` para operaciones (LOGIN, PING, MSGSEND)
-   Ejemplo PING: `{"response": "pong"}`

### 3. Comunicación con Backend

-   Socket TCP adicional para base de datos
-   Reenvío de solicitudes al backend
-   Retorno de respuestas al cliente

### 4. Daemon UDP (Load Balancing)

-   Proceso hijo para UDP
-   Heartbeats periódicos
-   Información de servicio (IP:TCP_PORT, IP:UDP_PORT)

### 5. Manejo de señales

-   SIGINT: Cierre ordenado de todos los sockets TCP y UDP
-   SIGCHLD: Limpieza de procesos, recolección de hijos zombi tras cada fork
-   Liberación de recursos

## 📡 API Reference

All requests are JSON objects with an `"action"` field.

### Action `0` – Validate User

**Request:**

```json
{ "action": 0, "key": "username_or_email", "password": "..." }
```

**Response:**

```json
{ "response_code": 200, "response_text": "...", "token": "..." }
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
{ "action": 3, "key": "username_or_email", "token": "..." }
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
    "token": "..."
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
    "token": "..."
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
    "token": "..."
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

| Code | Meaning           |
| ---- | ----------------- |
| 200  | Success           |
| 202  | Success but empty |
| 400  | Bad request       |
| 404  | Unknown action    |

---

## ⚠️ Limitations

-   **Max participants per chat:** 10 (`MAX_PARTICIPANTS`)
-   **Max chats retrieved at once:** 100 (`MAX_CHATS`)
-   **Max messages retrieved at once:** 200 (`MAX_MESSAGES`)

---
