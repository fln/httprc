httprc
======

This package is an implementation of remote command execution over HTTP. It is
intended to be used for remotely controlling devices hidden behind firewalls.

Components
----------

Contents of this repository is:

- [client](client/) implementation of client service written in C
- [server](server/) implementation server service written in go
- [ca](ca/) tools for building simple PKI for testing

General system architecture
---------------------------

System architecture is based on the idea that client application behaviour must
be releatively simple and stricyly defined. Client must periodically send HTTP
request to a predefined server URL. Response for this request includes a time to
wait before making next request and optionaly a command to be executed on the
client. If command was executed client must inform the server about command
execution result.

Client/Server Authentication
----------------------------

Httprc system uses x509 PKI and TLS for mutual client-server and server-client
authorization. Each client must have private key and certificate signed by the
CA that server trust, and server must have a private key and certificate signed
by the CA trusted by the clients.

Directory `ca` contains scripts to build to build simple PKI system (CA, server,
client key pairs and certificates).
