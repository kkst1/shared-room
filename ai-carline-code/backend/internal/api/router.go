package api

import (
	"log"
	"net/http"
	"strings"
	"time"
)

func NewRouter(h *Handler) http.Handler {
	mux := http.NewServeMux()

	mux.HandleFunc("/api/health", method("GET", h.Health))
	mux.HandleFunc("/api/home/recommendations", method("GET", h.HomeRecommendations))
	mux.HandleFunc("/api/questions", method("GET", h.Questions))

	mux.HandleFunc("/api/questions/", func(w http.ResponseWriter, r *http.Request) {
		switch {
		case strings.HasSuffix(r.URL.Path, "/analysis"):
			method("GET", h.QuestionAnalysis)(w, r)
		case strings.HasSuffix(r.URL.Path, "/submit"):
			method("POST", h.SubmitAnswer)(w, r)
		default:
			method("GET", h.QuestionByID)(w, r)
		}
	})

	mux.HandleFunc("/api/users/", func(w http.ResponseWriter, r *http.Request) {
		switch {
		case strings.HasSuffix(r.URL.Path, "/answers"):
			method("GET", h.UserAnswers)(w, r)
		case strings.HasSuffix(r.URL.Path, "/status"):
			method("PATCH", h.UpdateStatus)(w, r)
		default:
			http.NotFound(w, r)
		}
	})

	return logging(cors(mux))
}

func method(expected string, next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		if r.Method != expected {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		next(w, r)
	}
}

func cors(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET,POST,PATCH,OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func logging(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		log.Printf("%s %s %s", r.Method, r.URL.Path, time.Since(start))
	})
}
