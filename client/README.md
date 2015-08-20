httprcclient
============

This is a client side application for httprc system.

Requirements
------------

This application depends on following libraries:

- libcurl
- libjansson >= 2.7

Internally httprcclient uses Linux specific epoll syscall interface to track
executed application stdin/stdout/stderr descriptors. This interface might not
be available on other than Linux operating systems.

For command line arguments parsing httprcclient uses argp which is included in
GNU libc by default. When using other C standard library argp support might need
to be added as a separate library.

Building
--------

This application uses GNU auto-tools for building and installation.

For a standard build procedure follow these steps:

```bash
$ autoreconf -fvi   # bootstrap building environment
$ ./configure       # detect availability of required libraries
$ make              # compile the application
$ make install      # (optional) intall application on the system
```

Httprcclient compiles into single binary executable located in src/httprcclient.

Usage
-----

Httpcrclient can work in three modes:

- *Unauthenticated* - client connects to server via plain-text HTTP. This mode
is useful only for secure internal networks or debugging.
- *Authenticated server* - client connects to server via HTTPS. Commands are
executed only if server certificate is signed by the CA that client application
trusts. In this mode command execution results can be faked by malicious user,
because server do not perform any client authentication.
- *Mutually authenticaed* - both client and server have a certificate.
Connection is established only if server accepts client certificate as trusted
and client accepts server certificate as trusted. This is preferred mode to use
this application in production environment.

Httprcclient is configured by command-line arguments. These three examples shows
how to start a server in each mode described above.

Example using plain-text HTTP mode:

```bash
$ httprcclient http://backend.example.net/{client_id}
```

Example using server authentication only:

```bash
$ httprcclient --server-ca /path/to/ca.pem https://backend.example.net/{client_id}
```

Example using mutual authentication

```bash
$ httprcclient --server-ca /path/to/ca.pem --client-cert /path/to/client.pem --client-key /path/to.client.key https://backend.example.net/{client_id}
```

Summary of available arguments:

| Argument           | Description                                                                   |
|--------------------|-------------------------------------------------------------------------------|
| --server-ca FILE   | Path to CA certificate. Used to authenticate server.                          |
| --client-cert FILE | Path to client certificate. Used for client authentication.                   |
| --client-key FILE  | Path to client private key. Used for client authentication.                   |
| --verbose          | Enable verbose mode. Show received and transmitted JSON blobs.                |
| BACKEND_URL        | URL where client checks for commands to execute. **This option is madatory**. |


