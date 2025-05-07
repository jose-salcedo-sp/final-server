# Backend - Logic Server

## Objective

Handle client requests and connect to the data server/servers accordingly.

---

## Frontend -> server.c

### Actions

```c
typedef enum {
    LOGIN = 0,
    LOGOUT = 1,
    REGISTER = 2,
    MSG_SEND = 3,
    CHAT_JOIN = 5
    // other actions...
} ACTIONS;
````

---

## Action: `LOGIN`

### Request

#### Type

```c
typedef struct {
    int action;
    char username[64];
    char password[64];
} LOGIN_REQ;
```

#### Example

```json
{
    "action": 0,
    "username": "user1",
    "password": "pass"
}
```

### Response \[SUCCESSFUL]

```json
{
    "success": true,
    "token": "string" // TBD
}
```

### Response \[UNSUCCESSFUL]

```json
{
    "success": false,
    "error": "Invalid username or password"
}
```

---

## Action: `MSG_SEND`

### Request

#### Type

```c
typedef struct {
    int action;
    char token[128]; // Authentication token
    char msg[256];   // Message content
    char chat[64];   // Chat UUID
} MSG_SEND_REQ;
```

#### Example

```json
{
    "action": 3,
    "token": "abc123token",
    "msg": "Hello, world!",
    "chat": "chat-uuid-456"
}
```

### Response \[SUCCESSFUL]

```json
{
    "success": true,
    "message": "Message sent"
}
```

### Response \[UNSUCCESSFUL]

```json
{
    "success": false,
    "error": "Recipient not found or invalid token"
}
```

---

## Action: `CHAT_JOIN`

### Request

#### Type

```c
typedef struct {
    int action;
    char token[128];
    char chat[64]; // Chat UUID
} CHAT_JOIN_REQ;
```

#### Example

```json
{
    "action": 5,
    "token": "abc123token",
    "chat": "chat-uuid-456"
}
```

### Response \[SUCCESSFUL]

```json
{
    "success": true,
    "message": "Joined chat successfully"
}
```

### Response \[UNSUCCESSFUL]

```json
{
    "success": false,
    "error": "Chat not found or invalid token"
}
```

---

## Notes

* All requests and responses use **JSON**.
* Each request must include an `action` field matching the `ACTIONS` enum.
* For authenticated actions (`MSG_SEND`, `CHAT_JOIN`), a valid `token` is required.
* Response fields:

  * `success` (boolean): true if the action succeeded.
  * `error` (string): error message when `success` is false.
  * `token` (string): provided on successful login.
