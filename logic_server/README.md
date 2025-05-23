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

-   Implementación de socket maestro en puerto TCP (`TCP_PORT`).
-   Por cada conexión entrante, se ejecuta un `fork()` que crea un proceso hijo exclusivo para ese cliente.
-   Esto permite manejo concurrente de múltiples sesiones sin bloqueo entre ellas.
-   Aislamiento total: si un cliente falla o termina, no afecta a los demás.
-   Se utiliza `poll()` para multiplexar eventos entre el cliente y el backend TCP desde el mismo proceso.

### 2. Protocolo de comunicación (JSON)

-   Toda la comunicación entre cliente y servidor está basada en el formato JSON.
-   Se exige el campo `"action"` como entero, que representa el tipo de operación solicitada (ej. `PING`, `CREATE_USER`, `SEND_MESSAGE`).
-   Acciones básicas como `PING` se responden localmente en el servidor lógico.
-   Acciones más complejas (mensajes, validación de usuario, creación de chats, etc.) se reenvían al backend de base de datos.
-   Si el JSON está mal formado o falta un campo esencial, se responde con un mensaje de error también en formato JSON.

### 3. Comunicación con Backend

-   Se establece un socket TCP adicional hacia el backend en `127.0.0.1:6060` desde cada proceso hijo.
-   El servidor lógico actúa como intermediario entre cliente y backend:
    - Valida y preprocesa el JSON.
    - Reenvía solicitudes directamente.
    - Escucha respuestas del backend y las entrega al cliente.
-   Se modifica la respuesta del backend solo en casos particulares, como al generar tokens JWT tras un `CREATE_USER` exitoso.

### 4. Daemon UDP (Load Balancing)

-   Se lanza un proceso hijo con `fork()` exclusivo para el demonio UDP.
-   Este proceso ejecuta `udp_lb_daemon()` que emite "heartbeats" periódicos vía UDP.
-   Los heartbeats contienen información del servicio disponible: IP y puertos del servidor lógico.
-   Permite que otros servicios descubran servidores activos y se implemente balanceo de carga en futuras etapas.

### 5. Manejo de señales

-   `SIGINT` permite un cierre ordenado del servidor: cierra los sockets TCP (`sd`) y UDP (`udp_sd`) y termina el programa limpiamente.
-   `SIGCHLD` evita procesos zombi tras el `fork()` al recolectar procesos hijos muertos usando `waitpid(-1, ..., WNOHANG)`.
-   Se manejan ambas señales usando `signal()` en `main()` para garantizar estabilidad del proceso padre.

### 6. Autenticación con Tokens (JWT)

-   Acciones sensibles como `SEND_MESSAGE`, `GET_USER_INFO` o `CREATE_CHAT` requieren autenticación con JWT.
-   El servidor lógico valida localmente los tokens antes de reenviar cualquier solicitud al backend.
-   Si el token es inválido o está ausente, se responde directamente con un error (`unauthorized`).
-   Al crear un nuevo usuario, el servidor genera un token simulado (`create_token`) y lo añade automáticamente a la respuesta.
-   Este esquema simula un flujo de autenticación real y puede integrarse fácilmente con JWT reales a futuro.

### 7. Manejo de errores robusto

-   Se verifican todos los pasos críticos: apertura de sockets, conexión al backend, formato JSON, existencia de campos obligatorios.
-   Los errores se reportan con logs (`log_err`, `log_warn`, `log_info`) y se responden con mensajes JSON adecuados al cliente.
-   El servidor evita caídas inesperadas cerrando recursos correctamente y aislando errores por proceso.

### 8. Pruebas de integración de red

- Se probó satisfactoriamente la conexión entre el cliente y el servidor de datos de forma directa.
- Se probó satisfactoriamente la conexión entre el cliente y el servidor de datos a través del Load Balancer
- El servidor lógico pudo conectarse con el servidor de base de datos y retornar resultados al cliente
- También se probó la conexión desde el cliente al servidor lógico **a través del balanceador UDP**
- Se confirmaron respuestas correctas para `PING`, `CREATE_USER`, y `LOGIN`
- Hubo algunas complicaciones al configurar IP's entre servidores y el load balancer pero al final se solucionó

## 📡 API Reference

Todas las peticiones son objetos JSON que deben incluir un campo `"action"` con un valor numérico correspondiente a la operación deseada.  
Dependiendo del tipo de acción, puede requerirse también el campo `"token"` para autenticación.


### Action `0` – Validate User

**Request:**

```json
{ "action": 0, "key": "username_or_email", "password": "..." }
```

**Response:**

```json
{   "response_code": 200, 
    "response_text": "...", 
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."  }
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
{   "response_code": 200,
    "response_text": "...",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..." }
```

---

### Action `3` – Get User Info

**Request:**

```json
{ "action": 3, "key": "username_or_email", "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..." }
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

## Error Responses

### Invalid JSON Format

**Response:**

```json
{
    "error": "invalid JSON"
}
```

### Missing or Invalid Action

**Response:**

```json
{
    "error": "missing or invalid action"
}
```

### Token Validation Errors

**Response:**

```json
{
    "error": "unauthorized: token missing"
}
```

### Invalid/Expired Token

**Response:**

```json
{
    "error": "unauthorized: invalid token"
}
```

### Database Forwarding Errors

**Response:**

```json
{
    "error": "backend service unavailable",
    "code": 503
}
```
### Action-Specific Errors

**Response:**

```json
{
    "action": 2,
    "status": "error",
    "message": "username already exists"
}
```
### Chat Creation Failed

**Response:**

```json
{
    "action": 4,
    "status": "error",
    "message": "participant not found"
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
