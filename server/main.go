package main

import (
	"crypto/tls"
	"crypto/x509"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/mux"
)

type RCServer struct {
	LastSeen         map[string]time.Time
	LastSeenLock     sync.RWMutex
	PendingTasks     map[string][]Command
	PendingTasksLock sync.RWMutex
	FinishedTasks    map[string][]Result
	server           *http.Server
	adminServer      *http.Server
}

type Result struct {
	ID         string `json:"id"`
	ReturnCode int    `json:"returnCode"`
	Stdout     string `json:"stdout"`
	Stderr     string `json:"stderr"`
	DurationMs int    `json:"durationMs"`
}

type Command struct {
	ID               string            `json:"id"`
	Command          string            `json:"command"`
	Args             []string          `json:"args,omitempty"`
	Dir              string            `json:"directory,omitempty"`
	Environment      map[string]string `json:"environment,omitempty"`
	CleanEnvironment bool              `json:"cleanEnvironment,omitempty"`
	Stdin            string            `json:"stdin,omitempty"`
	WaitTimeMs       int               `json:"waitTimeMs"`
	OutputBufferSize int               `json:"outputBufferSize"`
}

type Task struct {
	SelepMs int      `json:"sleepMs"`
	Exec    *Command `json:"exec,omitempty"`
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
		clientID := mux.Vars(r)["clientID"]
		log.Printf("tlsAuth, CN: %s, clientID: %s", cn, clientID)
		if cn != "" {
			f(w, r, cn)
		} else {
			RespondErrorCode(w, 401)
		}
	})
}

//func (app *RCServer) startSimple(addr string) {
//}

//func (app *RCServer) startTLS(addr string, certFile string, keyFile string) {
//}

func (app *RCServer) startTLSAuth(addr string, certFile string, keyFile string, caFile string) {
	var err error
	router := mux.NewRouter()
	app.server = &http.Server{
		Addr:    addr,
		Handler: router,
	}

	router.Handle("/v1/httprc/{clientID}", tlsAuth(app.getTask))

	// Enable client authentication
	if caFile != "" {
		app.server.TLSConfig = &tls.Config{
			ClientAuth: tls.RequireAndVerifyClientCert,
			ClientCAs:  x509.NewCertPool(),
		}
		if caPEM, err := ioutil.ReadFile(caFile); err != nil {
			log.Fatal(err)
		} else {
			app.server.TLSConfig.ClientCAs.AppendCertsFromPEM(caPEM)
		}
	}

	if certFile != "" && keyFile != "" {
		err = app.server.ListenAndServeTLS(certFile, keyFile)
	} else {
		err = app.server.ListenAndServe()
	}
	log.Fatal(err)
}

func (app *RCServer) startAdminServer(addr string) {
	router := mux.NewRouter()
	app.adminServer = &http.Server{
		Addr:    addr,
		Handler: router,
	}

	router.HandleFunc("/v1/httprc/presence", app.clientPresence)
	router.HandleFunc("/v1/httprc", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, "Hello admin!")
	})
	router.PathPrefix("/").Handler(http.FileServer(http.Dir("static")))
	err := app.adminServer.ListenAndServe()
	log.Fatal(err)
}

func main() {
	clientCACertFile := flag.String("ca-cert", "", "Enable client authentication using specified CA certificate")
	serverCertFile := flag.String("server-cert", "", "Server certificate for TLS mode")
	serverKeyFile := flag.String("server-key", "", "Server private key for TLS mode")

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

	app := RCServer{
		LastSeen:      make(map[string]time.Time),
		PendingTasks:  make(map[string][]Command),
		FinishedTasks: make(map[string][]Result),
	}

	go app.startTLSAuth(":8989", *serverCertFile, *serverKeyFile, *clientCACertFile)
	go app.startAdminServer(":8889")
	select {}
}
