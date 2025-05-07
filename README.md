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
    // other actions...
} ACTIONS;
```

### Login
#### Request
```json
{
    "action": 0,
    "username": "string",
    "password": "string"
}
```
#### Response [SUCCESSFUL]
```json
{
    "success": true,
    "token": "string"
}
```
#### Response [UNSUCCESFUL]
```json
{
    "success": false,
    "error": "Invalid username or password"
}
```
