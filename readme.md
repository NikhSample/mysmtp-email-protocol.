
# My\_SMTP

## 📖 Project Description

My\_SMTP is a simplified version of the **Simple Mail Transfer Protocol (SMTP)**.
It allows a client to send, list, and retrieve emails through a server over a local area network (LAN).
The server stores messages locally, and clients can interact using custom My\_SMTP commands.

---

The project demonstrates:

* Application-layer protocol design
* TCP socket programming in C
* Client-server model
* Concurrent client handling
* File-based email storage and retrieval

### **Protocol Design**

   * Commands:
     * `HELO <client_id>`
     * `MAIL FROM: <email>`
     * `RCPT TO: <email>`
     * `DATA` (end with `.`)
     * `LIST <email>`
     * `GET_MAIL <email> <id>`
     * `QUIT`
   * Response codes:
     * `200 OK` → success
     * `400 ERR` → invalid syntax
     * `401 NOT FOUND` → no such email
     * `403 FORBIDDEN` → not permitted
     * `500 SERVER ERROR` → internal error

## Build Instructions

Compile both client and server using the provided **Makefile**:

```bash
make
```

This generates two executables:

* `mysmtp_server`
* `mysmtp_client`


## Usage

### Start the Server

```bash
make rs
```

### Connect with the Client

```bash
make rc
```

## Example Interaction

**Client Side**

```
> HELO test.com
200 OK
> MAIL FROM: john@test.com
200 OK
> RCPT TO: david@test.com
200 OK
> DATA
Enter your message (end with a single dot '.'):
Hello David,
Hope you are doing well.
.
200 Message stored successfully
> LIST david@test.com
200 OK
1: Email from john@test.com (15-08-2025)
> GET_MAIL david@test.com 1
200 OK
From: john@test.com
Date: 15-08-2025
Hello David,
Hope you are doing well.
> QUIT
200 Goodbye
```

**Server Side**

```
Listening on port 2525...
Client connected: 127.0.0.1
HELO received from test.com
MAIL FROM: john@test.com
RCPT TO: david@test.com
DATA received, message stored.
LIST david@test.com
Emails retrieved; list sent.
GET_MAIL david@test.com 1
Email with id 1 sent.
Client disconnected.
```

## Notes

   * Server listens on a port (default: 2525).
   * Emails are stored in `mailbox/<recipient>.txt` for later retrieval.