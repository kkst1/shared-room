package main

import (
	"log"
	"net/http"
	"os"

	"vibecoding/backend/internal/api"
	"vibecoding/backend/internal/services"
	"vibecoding/backend/internal/store"
)

func main() {
	s := store.New()

	userService := services.NewUserService(s)
	questionService := services.NewQuestionService(s)
	analysisService := services.NewAnalysisService(s)
	aiService := services.NewAIService()
	answerService := services.NewAnswerService(s, userService, questionService, analysisService, aiService)
	recommendationService := services.NewRecommendationService(s)

	handler := api.NewHandler(
		questionService,
		analysisService,
		answerService,
		userService,
		recommendationService,
	)
	router := api.NewRouter(handler)

	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}
	addr := ":" + port

	log.Printf("backend listening on %s", addr)
	if err := http.ListenAndServe(addr, router); err != nil {
		log.Fatal(err)
	}
}
