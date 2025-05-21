# Avance Proyecto Final - Servidor Lógico

**Materia:** Cómputo Distribuido  
**Profesor:** Juan Carlos Pimentel  
**Fecha de entrega:** 05 de Junio de 2025  

## Integrantes del equipo
- Demian Velasco Gómez Llanos (0253139@up.edu.mx)
- Hector Emiliano Flores Castellano (0254398@up.edu.mx)
- Diego Amin Hernandez Pallares (0250146@up.edu.mx)

## Hitos implementados

### 1. Arquitectura TCP concurrente
- Socket TCP maestro en puerto configurado
- Fork por cada conexión de cliente
- Sesiones aisladas y concurrentes

### 2. Procesamiento JSON
- Protocolo basado en JSON
- Campo `action` para operaciones (LOGIN, PING, MSGSEND)
- Ejemplo PING: `{"response": "pong"}`

### 3. Comunicación con Backend
- Socket TCP adicional para base de datos
- Reenvío de solicitudes al backend
- Retorno de respuestas al cliente

### 4. Daemon UDP (Load Balancing)
- Proceso hijo para UDP
- Heartbeats periódicos
- Información de servicio (IP:TCP_PORT, IP:UDP_PORT)

### 5. Manejo de señales
- SIGINT: Cierre ordenado de todos los sockets TCP y UDP
- SIGCHLD: Limpieza de procesos, recolección de hijos zombi tras cada fork
- Liberación de recursos