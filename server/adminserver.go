package main

import (
	"fmt"
	"log"
	"net/http"
	"time"

	"github.com/gorilla/mux"
)

type ClientListEntry struct {
	ClientID string    `json:"id"`
	Time     time.Time `json:"time"`
	IP       string    `json:"IP"`
}

func (app *RCServer) adminClientList(w http.ResponseWriter, r *http.Request) {
	app.ClientMapLock.RLock()
	defer app.ClientMapLock.RUnlock()

	clientList := make([]ClientListEntry, 0, len(app.ClientMap))

	for clientID, c := range app.ClientMap {
		c.RLock()
		clientList = append(clientList, ClientListEntry{
			ClientID: clientID,
			Time:     c.LastSeen.Time,
			IP:       c.LastSeen.IP,
		})
		c.RUnlock()
	}

	RespondJsonObject(w, clientList, true)
}

func (app *RCServer) adminDeviceEvents(w http.ResponseWriter, r *http.Request) {
	clientID := mux.Vars(r)["clientID"]
	f, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "Streaming unsupported!", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	resultChan := app.ps.Sub(clientID + "-result")
	defer app.ps.Unsub(resultChan, clientID+"-result")
	cmdChan := app.ps.Sub(clientID + "-cmd")
	defer app.ps.Unsub(cmdChan, clientID+"-cmd")

loop:
	for {
		select {
		case msg := <-cmdChan:
			cmd, ok := msg.(Command)
			if !ok {
				continue loop
			}
			_, err := fmt.Fprintf(w, "event: command\ndata: %s\n\n", JsonMustMarshal(cmd, false))
			if err != nil {
				log.Print("Disconnecting broken client")
				break loop
			}
			f.Flush()
		case msg := <-resultChan:
			result, ok := msg.(FinishedTask)
			if !ok {
				continue loop
			}
			_, err := fmt.Fprintf(w, "event: result\ndata: %s\n\n", JsonMustMarshal(result, false))
			if err != nil {
				log.Print("Disconnecting broken client")
				break loop
			}
			f.Flush()
		}
	}
}

func (app *RCServer) adminClientGet(w http.ResponseWriter, r *http.Request) {
	clientID := mux.Vars(r)["clientID"]

	app.ClientMapLock.RLock()
	client, ok := app.ClientMap[clientID]
	app.ClientMapLock.RUnlock()

	if ok {
		RespondJsonObject(w, client, true)
	} else {
		RespondErrorCode(w, 404)
	}
}

func (app *RCServer) adminAddTask(w http.ResponseWriter, r *http.Request) {
	clientID := mux.Vars(r)["clientID"]

	app.ClientMapLock.RLock()
	client := app.ClientMap[clientID]
	app.ClientMapLock.RUnlock()

	var c Command
	if err := RequestJsonToStruct(r, &c); err != nil {
		log.Print(err)
		RespondErrorCode(w, 400)
		return
	}

	if client == nil {
		RespondErrorCode(w, 404)
		return
	}

	c.ID = newUUID()
	client.Lock()
	client.PendingTasks = append(client.PendingTasks, c)
	client.Unlock()

	app.ps.Pub(c, clientID+"-cmd")

	RespondJsonObject(w, c, true)
}
