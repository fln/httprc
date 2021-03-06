Demo httprc server
==================

This is a demo server for passing commands to httprc clients. Commands can be
scheduled for execution by using integrated admin web UI.

Internally this application starts two independent web servers. One HTTP/HTTPS
server communicates with httprc clients (devices on which commands can be
executed remotely), by default it is started on tcp port 8989. Second server is
used for admin web IU and by default it is started on 127.0.0.1:8889. Admin web
interface is not protected by any authentication mechanism and by default
accessible only from localhost.

Basic server architecture:

```
+-------------+
| Client #001 |<-\
+-------------+   \        ++---------------++
                   \------>||               ||
+-------------+    HTTP/S  ||               ||   HTTP   +-------------------+
| Client #002 |<---------->|| httprc server ||<-------->| Admin web browser |
+-------------+            ||               ||          +-------------------+
   .                 /---->||               ||
   .                /      ++---------------++
   .               /       ^                 ^
+-------------+   /        |                 |
| Client #999 |<-/   (client backend)   (admin web UI)
+-------------+        0.0.0.0:8989     127.0.0.1:8889
```

For near real-time command execution this server is using
[long-polling](https://en.wikipedia.org/wiki/Push_technology#Long_polling) in
the client backend server and
[SSE](https://en.wikipedia.org/wiki/Server-sent_events)
in admin web UI.

Building
--------

Install golang tools by following
[official golang installation instructions](http://golang.org/doc/install).

Download dependencies:
```bash
$ go get    # download application dependencies
$ go build  # compile the application
```

Usage
-----

Server can be started in different security level modes.

### Plain-text mode
In this mode server is waiting for clients to connect using plain-text
HTTP. Clients must provide client identifier in the request url.

Starting server in plain text mode requires no additional arguments:

```bash
./server
```

In this mode clients should use backend URL `http://server.example.net:8989/httprc/{clientID}`.

### HTTPS without client authentication

Server is waiting for clients to connect using HTTPS and does not require
client certificate. Clients must provide client identifier in the request url.

To start server in HTTPS mode without client authentication server certificate
and private key must be provided:

```bash
./server --server-cert ../ca/server.pem  --server-key ../ca/server.key
```
In this mode clients should use backend URL `https://server.example.net:8989/httprc/{clientID}`.

### HTTPS with client authentication mode

Server is waiting for clients to connect using HTTPS, and only accepts the
connection if client provides a valid certificate signed by the CA the server
trusts. In this mode client certificate Common Name field is used as client
identifier.

To use client authentication server must be provided with CA certificate:

```bash
./server --client-ca ../ca/ca.pem  --server-cert ../ca/server.pem  --server-key ../ca/server.key
```

In this mode clients are identified by the client certificate and can use
backend URL without `clientID` part `https://server.example.net:8989/httprc`.

### Startup arguments summary

| Argument                | Description                                                                               |
|-------------------------|-------------------------------------------------------------------------------------------|
| --server-cert FILE      | Path to server certificate. This certificate is used only for communication with clients. |
| --server-key FILE       | Path to server private key. This private key is used only for communication with clients. |
| --client-ca FILE        | Path to CA certificate. Used to authenticate clients.                                     |
| --listen-client ADDRESS | Address on which to start client backend. Default value is ":8989".                       |
| --listen-admin ADDRESS  | Address on which to start admin web UI. Default value is "127.0.0.1:8889".                |
