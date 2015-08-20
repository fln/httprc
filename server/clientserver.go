package main

import (
	"log"
	"net/http"
	"time"
)

func (app *RCServer) getTask(w http.ResponseWriter, r *http.Request, clientID string) {
	app.PendingTasksLock.RLock()
	defer app.PendingTasksLock.RUnlock()

	app.LastSeenLock.Lock()
	app.LastSeen[clientID] = time.Now()
	app.LastSeenLock.Unlock()

	taskList := app.PendingTasks[clientID]
	if len(taskList) == 0 {
		RespondJsonObject(w, &Task{SelepMs: 1000}, false)
		return
	}

	log.Print(r)
	task := Task{
		SelepMs: 2000,
		Exec: &Command{
			ID:               newUUID(),
			Command:          "/bin/cat",
			Dir:              "/",
			Args:             []string{"-", "etc/hostname", "/proc/vmallocinfo", "/proc/self/environ"},
			Stdin:            "this is \x00 a test\n",
			Environment:      map[string]string{"XXX": "yyy", "LS_COLORS": ""},
			CleanEnvironment: true,
			WaitTimeMs:       10000,
			OutputBufferSize: 1024 * 1024,
		},
	}
	if r.Body != nil {
		r.Body.Close()
	}
	RespondJsonObject(w, &task, false)
}
