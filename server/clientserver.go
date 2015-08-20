package main

import (
	"log"
	"net/http"
	"time"
)

var finishedTaskLimit = 10

func (app *RCServer) clientPostTask(w http.ResponseWriter, r *http.Request, clientID string) {
	var result Result
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
			client.FinishedTasks = append(client.FinishedTasks, FinishedTask{
				Command: c,
				Result:  result,
			})
			break
		}
	}
	if len(client.FinishedTasks) > finishedTaskLimit {
		client.FinishedTasks = client.FinishedTasks[(len(client.FinishedTasks) - finishedTaskLimit):]
	}
	w.WriteHeader(http.StatusNoContent)
}

func (app *RCServer) clientGetTask(w http.ResponseWriter, r *http.Request, clientID string) {
	defaultSleepTime := 1000

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
	defer client.RUnlock()
	if len(client.PendingTasks) == 0 {
		RespondJsonObject(w, &Task{SelepMs: defaultSleepTime}, false)
		return
	}

	log.Print(r)
	/*dummyCmd := &Command{
		ID:               newUUID(),
		Command:          "/bin/cat",
		Dir:              "/",
		Args:             []string{"-", "etc/hostname", "/proc/vmallocinfo", "/proc/self/environ"},
		Stdin:            "this is \x00 a test\n",
		Environment:      map[string]string{"XXX": "yyy", "LS_COLORS": ""},
		CleanEnvironment: true,
		WaitTimeMs:       10000,
		OutputBufferSize: 1024 * 1024,
	}*/

	task := Task{
		SelepMs: defaultSleepTime,
		Exec:    &client.PendingTasks[0],
	}
	RespondJsonObject(w, &task, false)
}
