package main

import (
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/http"
)

type ExecTask struct {
	ID               string            `json:"id"`
	DaemonMode       bool              `json:"daemonMode"`
	Command          string            `json:"command"`
	Args             []string          `json:"args,omitempty"`
	WorkDir          string            `json:"workDir,omitempty"`
	Environment      map[string]string `json:"environment,omitempty"`
	Stdin            string            `json:"stdin,omitempty"`
	WaitTimeMs       int               `json:"waitTimeMs"`
	OutputBufferSize int               `json:"outputBufferSize"`
}

type Task struct {
	SelepMs int       `json:"sleepMs"`
	Exec    *ExecTask `json:"exec,omitempty"`
}

func newUUID() string {
	uuid := make([]byte, 16)
	n, err := io.ReadFull(rand.Reader, uuid)
	if n != len(uuid) || err != nil {
		panic(err)
	}
	// variant bits; see section 4.1.1
	uuid[8] = uuid[8]&^0xc0 | 0x80
	// version 4 (pseudo-random); see section 4.1.3
	uuid[6] = uuid[6]&^0xf0 | 0x40
	return fmt.Sprintf("%x-%x-%x-%x-%x", uuid[0:4], uuid[4:6], uuid[6:8], uuid[8:10], uuid[10:])
}

func JsonMustMarshal(o interface{}, indent bool) []byte {
	var response []byte
	var e error

	if indent {
		response, e = json.MarshalIndent(o, "", "\t")
	} else {
		response, e = json.Marshal(o)
	}

	if e != nil {
		panic(e)
	}

	return append(response, '\n')
}

func RespondJsonObject(w http.ResponseWriter, o interface{}, indent bool) {
	RespondJsonObjectCustom(w, o, indent, 200)
}

func RespondJsonObjectCustom(w http.ResponseWriter, o interface{}, indent bool, code int) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(code)
	w.Write(JsonMustMarshal(o, indent))
}

func hello(w http.ResponseWriter, r *http.Request, userID string) {
	task := Task{
		SelepMs: 2000,
		Exec: &ExecTask{
			ID:         newUUID(),
			DaemonMode: false,
			Command:    "/bin/cat",
			Args:       []string{"-", "/etc/passwd", "/proc/vmallocinfo"},
			//Args:             []string{"/etc/passwd", "/proc/vmallocinfo"},
			Stdin:            "this is \x01 a test\n",
			WaitTimeMs:       10000,
			OutputBufferSize: 1024 * 1024,
		},
	}
	RespondJsonObject(w, &task, true)
}

func tlsAuth(f func(http.ResponseWriter, *http.Request, string)) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var cn string

		if r.TLS == nil {
			log.Print("Client without TLS context")
			return
		}
		for _, chain := range r.TLS.VerifiedChains {
			if len(chain) < 1 {
				continue
			}
			cert := chain[0]
			if cert != nil {
				cn = cert.Subject.CommonName
			}
		}
		if cn != "" {
			f(w, r, cn)
		}
	})
}

func startClientServer(addr string, caFile string, certFile string, keyFile string) {
	server := &http.Server{
		Addr:    addr,
		Handler: tlsAuth(hello),
		TLSConfig: &tls.Config{
			ClientAuth: tls.RequireAndVerifyClientCert,
			ClientCAs:  x509.NewCertPool(),
		},
	}

	if caPEM, err := ioutil.ReadFile(caFile); err != nil {
		log.Fatal(err)
	} else {
		server.TLSConfig.ClientCAs.AppendCertsFromPEM(caPEM)
	}

	err := server.ListenAndServeTLS(certFile, keyFile)
	log.Fatal(err)
}

func startAdminServer(addr string) {
	server := &http.Server{
		Addr: addr,
		Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			fmt.Fprintf(w, "Hello admin!")
		}),
	}
	err := server.ListenAndServe()
	log.Fatal(err)
}

func main() {
	clientCACertFile := flag.String("clientCA", "", "Path to file with CA certificate used to authenticate clients")
	serverCertFile := flag.String("serverCert", "", "Path to client facing server certificate")
	serverKeyFile := flag.String("serverKey", "", "Path to client facing server private key")

	flag.Parse()

	if *clientCACertFile == "" {
		log.Fatal("Missing \"clientCA\" flag")
	}

	if *serverCertFile == "" {
		log.Fatal("Missing \"serverCert\" flag")
	}

	if *serverKeyFile == "" {
		log.Fatal("Missing \"serverKey\" flag")
	}
	go startClientServer(":8989", *clientCACertFile, *serverCertFile, *serverKeyFile)
	go startAdminServer(":8889")
	select {}
}
