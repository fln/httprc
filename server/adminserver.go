package main

import (
	"log"
	"net/http"

	"github.com/gorilla/mux"
)

func (app *RCServer) adminClientList(w http.ResponseWriter, r *http.Request) {
	app.ClientMapLock.RLock()
	defer app.ClientMapLock.RUnlock()

	clientList := make([]string, 0, len(app.ClientMap))

	for clientID, _ := range app.ClientMap {
		clientList = append(clientList, clientID)
	}

	RespondJsonObject(w, clientList, true)
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
	RespondJsonObject(w, c, true)
}
