Demo PKI tools
==============

This directory contains a helper `Makefile` to create a minimal PKI having keys 
and certificates for CA, servers and clients.

To use this tool `certtool` from gnutls-bin package must be installed on the
system.

Usage
-----

Makefile accepts following targets:

- ca.key - generate CA public/private key pair
- ca.pem - generate a self-signed CA certificate
- server.key - generate server public/private key pair
- server.pem - generate and sign server certificate
- client_x.key - generate a client public/private key pair. Where `x` is any
string.
- client_x.pem - generate and sign certificate for client `x`. String `x` will
be used as CN in the certificate.

Generated certificate details can be fine tuned by editing config files ca.cfg,
server.cfg and client.cfd acordingly. Please note that client.cfg is a template
for client certificate config.

Requesting to generate a certificate without first generating a public/private
key pair will automatically generate keys too.

Requesting to generate either client or server certificate without first
generating CA will automatically generate a new CA.

Examples
--------

To create a mini PKI with single client use command:

```shell
$ make server.pem client_123.pem
```

It will generate files - ca.key, ca.pem, server.key, server.pem, client_123.key,
client_123.pem.
