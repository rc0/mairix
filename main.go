package main

import (
	"fmt"
	"log"
	"net/http"
	"unicode/utf8"

	"github.com/rc0/mairix/internal/engine"
)

func main() {
	// Initialize engine
	eng := engine.NewEngine()

	http.HandleFunc("/query", func(w http.ResponseWriter, r *http.Request) {
		query := r.URL.Query().Get("q")

		if !utf8.ValidString(query) {
			http.Error(w, "Invalid UTF-8 sequence in query", http.StatusBadRequest)
			return
		}

		results := eng.Search(query)
		fmt.Fprintf(w, "Results for %s: %v", query, results)
	})

	log.Println("Mairix server starting on :8080...")
	log.Fatal(http.ListenAndServe(":8080", nil))
}
