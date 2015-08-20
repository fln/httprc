package main

import (
	"net/http"
)

func (app *RCServer) clientPresence(w http.ResponseWriter, r *http.Request) {
	app.LastSeenLock.RLock()
	defer app.LastSeenLock.RUnlock()

	RespondJsonObject(w, app.LastSeen, true)
}
