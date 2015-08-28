package main

import (
	"log"
	"net/http"
	"time"
)

var finishedTaskLimit = 10

func (app *RCServer) clientPostTask(w http.ResponseWriter, r *http.Request, clientID string) {
	var result Result
	var fTask FinishedTask

	if err := RequestJsonToStruct(r, &result); err != nil {
		log.Print(err)
		RespondErrorCode(w, 400)
	}

	app.ClientMapLock.RLock()
	client := app.ClientMap[clientID]
	app.ClientMapLock.RUnlock()

	if client == nil {
		RespondErrorCode(w, 404)
		return
	}

	client.Lock()
	defer client.Unlock()
	for i, c := range client.PendingTasks {
		if c.ID == result.ID {
			client.PendingTasks = append(client.PendingTasks[:i], client.PendingTasks[i+1:]...)
			fTask = FinishedTask{
				Command: c,
				Result:  result,
			}
			client.FinishedTasks = append(client.FinishedTasks, fTask)
			app.ps.Pub(fTask, clientID+"-result")
			break
		}
	}
	if len(client.FinishedTasks) > finishedTaskLimit {
		client.FinishedTasks = client.FinishedTasks[(len(client.FinishedTasks) - finishedTaskLimit):]
	}
	w.WriteHeader(http.StatusNoContent)
}

func (app *RCServer) clientGetTask(w http.ResponseWriter, r *http.Request, clientID string) {
	defaultSleepTime := 100
	log.Print("Client ", clientID, " connected")
	defer log.Print("Client ", clientID, " disconnected")

	app.ClientMapLock.RLock()
	client := app.ClientMap[clientID]
	app.ClientMapLock.RUnlock()

	if client == nil {
		client = &Client{
			PendingTasks:  []Command{},
			FinishedTasks: []FinishedTask{},
		}
		app.ClientMapLock.Lock()
		app.ClientMap[clientID] = client
		app.ClientMapLock.Unlock()
	}

	client.Lock()
	client.LastSeen.Time = time.Now()
	client.LastSeen.IP = r.RemoteAddr
	client.Unlock()

	client.RLock()
	if len(client.PendingTasks) != 0 {
		RespondJsonObject(w, &Task{
			SelepMs: defaultSleepTime,
			Exec:    &client.PendingTasks[0],
		}, false)
		client.RUnlock()
		return
	}
	client.RUnlock()

	topic := clientID + "-cmd"
	cmdChan := app.ps.Sub(topic)
	defer app.ps.Unsub(cmdChan, topic)
	timeoutChan := time.After(60 * time.Second)
	select {
	case msg := <-cmdChan:
		cmd, ok := msg.(Command)
		if ok {
			RespondJsonObject(w, &Task{
				SelepMs: defaultSleepTime,
				Exec:    &cmd,
			}, false)
			return
		}
	case <-timeoutChan:
	}
	RespondJsonObject(w, &Task{SelepMs: defaultSleepTime}, false)
}
