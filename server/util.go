package main

import (
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"strconv"
)

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
	return response
	//return append(response, '\n')
}

func RespondJsonObject(w http.ResponseWriter, o interface{}, indent bool) {
	RespondJsonObjectCustom(w, o, indent, 200)
}

func RespondJsonObjectCustom(w http.ResponseWriter, o interface{}, indent bool, code int) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	w.Write(JsonMustMarshal(o, indent))
}

func RespondErrorCode(w http.ResponseWriter, code int) {
	if code == 404 {
		http.Error(w, "404 page not found", http.StatusNotFound)
	} else {
		http.Error(w, strconv.Itoa(code)+" "+http.StatusText(code), code)
	}
}

func RequestJsonToStruct(r *http.Request, dest interface{}) error {
	data, err := ioutil.ReadAll(r.Body)
	if err != nil {
		return err
	}

	if err = json.Unmarshal(data, dest); err != nil {
		return err
	}
	return nil
}
