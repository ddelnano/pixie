package main

import (
 "context"
 "flag"
 "time"
 "log"
 "net/http"
 "encoding/json"
 "github.com/gorilla/mux"
)

type Request struct {
  Sleep       string  `json:"sleep"`
  RequestId   uint64  `json:"request_id"`
}

type Response struct {
  Slept      string `json:"slept"`
  RequestId  uint64 `json:"request_id"`
  ServerId   uint64 `json:"server_id"`
}

var once = false;
var server_id uint64 = 0;
var srv *http.Server

var ctx     context.Context
var cancel  context.CancelFunc

func handlePost(w http.ResponseWriter, r *http.Request) {
  var req Request;
  err := json.NewDecoder(r.Body).Decode(&req);
  if err != nil {
    http.Error(w, err.Error(), http.StatusBadRequest)
  } else {
    dur, err := time.ParseDuration(req.Sleep);
    if err != nil {
      http.Error(w, err.Error(), http.StatusBadRequest)
    } else {
      time.Sleep(dur);
      var res Response;
      res = Response{Slept: req.Sleep, RequestId: req.RequestId, ServerId: server_id};
      json.NewEncoder(w).Encode(res)
      if f, ok := w.(http.Flusher); ok {
        f.Flush()
      }
    }
  }
  if once {
    cancel()
  }
}

func main() {

  /* CLI */
  var addr string;
  flag.BoolVar(&once, "once", false, "Has the server, serve only one request");
  flag.StringVar(&addr, "addr", ":8080", "The address to use");
  flag.Uint64Var(&server_id, "sid", 0, "Server id");
  flag.Parse();

  /* Setup Server */
  router := mux.NewRouter();
  srv = &http.Server{
    Addr:    addr,
    Handler: router,
  }
  ctx, cancel = context.WithCancel(context.Background())
  router.HandleFunc("/", handlePost).Methods("POST")

  /* Run the server in another function for Cancel */
  go func() {
    err := srv.ListenAndServe();
    if err != nil && err != http.ErrServerClosed {
      log.Fatal(err)
    }
  }()

  /* Shutdown when the first request is processed */
  select {
    case <- ctx.Done():
      srv.Shutdown(ctx)
  }
}
