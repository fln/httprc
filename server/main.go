package main

import (
	"crypto/tls"
	"crypto/x509"
	"flag"
	"io/ioutil"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/mux"
	"github.com/tuxychandru/pubsub"
)

type Client struct {
	sync.RWMutex
	PendingTasks  []Command      `json:"pending"`
	FinishedTasks []FinishedTask `json:"finished"`
	LastSeen      struct {
		Time time.Time `json:"time"`
		IP   string    `json:"IP"`
	} `json:"lastSeen"`
}

type FinishedTask struct {
	Command Command `json:"command"`
	Result  Result  `json:"result"`
}

type RCServer struct {
	ClientMap     map[string]*Client
	ClientMapLock sync.RWMutex
	server        *http.Server
	adminServer   *http.Server
	ps            *pubsub.PubSub
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
			RespondErrorCode(w, 401)
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
		if cn == "" {
			RespondErrorCode(w, 401)
		}
		f(w, r, cn)
	})
}

func urlAuth(f func(http.ResponseWriter, *http.Request, string)) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		clientID := mux.Vars(r)["clientID"]
		if clientID != "" {
			f(w, r, clientID)
		} else {
			RespondErrorCode(w, 401)
		}
	})
}

func (app *RCServer) startTLSAuth(addr string, certFile string, keyFile string, caFile string) {
	var err error
	router := mux.NewRouter()
	app.server = &http.Server{
		Addr:    addr,
		Handler: router,
	}

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
		router.Handle("/httprc", tlsAuth(app.clientGetTask)).Methods("GET")
		router.Handle("/httprc/{clientID}", tlsAuth(app.clientGetTask)).Methods("GET")
		router.Handle("/httprc/{clientID}", tlsAuth(app.clientPostTask)).Methods("POST")
	} else {
		router.Handle("/httprc/{clientID}", urlAuth(app.clientGetTask)).Methods("GET")
		router.Handle("/httprc/{clientID}", urlAuth(app.clientPostTask)).Methods("POST")
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

	router.HandleFunc("/v1/httprc/clientList", app.adminClientList).Methods("GET")
	router.HandleFunc("/v1/httprc/client/{clientID}", app.adminClientGet).Methods("GET")
	router.HandleFunc("/v1/httprc/client/{clientID}/task", app.adminAddTask).Methods("POST")
	router.HandleFunc("/v1/httprc/client/{clientID}/events", app.adminDeviceEvents).Methods("GET")
	router.PathPrefix("/").Handler(http.FileServer(http.Dir("static")))
	err := app.adminServer.ListenAndServe()
	log.Fatal(err)
}

func main() {
	clientCACertFile := flag.String("client-ca", "", "Enable client authentication using specified CA certificate")
	serverCertFile := flag.String("server-cert", "", "Server certificate for TLS mode")
	serverKeyFile := flag.String("server-key", "", "Server private key for TLS mode")
	listenClient := flag.String("listen-client", ":8989", "Address to listen for client connections")
	listenAdmin := flag.String("listen-admin", "127.0.0.1:8889", "Address to listen for admin connections")
	flag.Parse()

	/*	if *clientCACertFile == "" {
			log.Fatal("Missing \"client-ca\" flag")
		}

		if *serverCertFile == "" {
			log.Fatal("Missing \"server-cert\" flag")
		}

		if *serverKeyFile == "" {
			log.Fatal("Missing \"server-key\" flag")
		}*/

	app := RCServer{
		ClientMap: make(map[string]*Client),
		ps:        pubsub.New(10),
	}

	go app.startTLSAuth(*listenClient, *serverCertFile, *serverKeyFile, *clientCACertFile)
	go app.startAdminServer(*listenAdmin)
	select {}
	app.ps.Shutdown()
}
