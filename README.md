httprc
======

This package is an implementation of remote command execution over HTTP. It is
intended to be used for remotely controlling devices hidden behind firewalls.

Components
----------

This repository contains:

- [client](client/) implementation of client service (written in C).
- [server](server/) implementation server service (written in golang).
- [ca](ca/) tools for building simple PKI for testing.

General system architecture
---------------------------

System architecture is based on the idea that client application behaviour must
be relatively simple and strictly defined. Client must periodically send HTTP
request to a predefined server URL. Response for this request includes a time to
wait before making next request and optionally a command to be executed on the
client. When command is executed client must inform the server about execution
results by generating POST request to the same predefined server URL.

Data exchanged between server and client is encoded using JSON.

Client/Server Authentication
----------------------------

Httprc system uses x509 PKI and TLS for mutual client-server and server-client
authentication. Each client must have private key and certificate signed by the
CA that server trust, and server must have a private key and certificate signed
by the CA trusted by the clients.

Directory `ca` contains scripts to build to build simple PKI system (CA, server,
client key pairs and certificates).
