all: ca.key ca.pem server.key server.pem client_007.key client_007.pem

.SECONDARY:

%.key:
	certtool --generate-privkey --rsa --sec-param=high --outfile=$@

ca.pem: ca.key ca.cfg
	certtool --generate-self-signed --template=ca.cfg --load-privkey=ca.key --outfile=$@

%.cfg:
	echo "Please create $@"
	false

server.pem: server.key server.cfg ca.key ca.pem
	certtool --generate-certificate --load-ca-privkey=ca.key --load-ca-certificate=ca.pem --template=server.cfg --load-privkey=server.key --outfile=$@

client_%.cfg: client.cfg
	sed 's|FIXME|$(@:client_%.cfg=%)|' $< > $@

client_%.pem: client_%.key client_%.cfg ca.key ca.pem
	certtool --generate-certificate --load-ca-privkey=ca.key --load-ca-certificate=ca.pem --template=$(@:%.pem=%.cfg) --load-privkey=$(@:%.pem=%.key) --outfile=$@
