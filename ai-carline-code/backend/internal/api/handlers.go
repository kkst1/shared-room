package api

import (
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"strings"

	"vibecoding/backend/internal/models"
	"vibecoding/backend/internal/services"
)

type Handler struct {
	questionService       *services.QuestionService
	analysisService       *services.AnalysisService
	answerService         *services.AnswerService
	userService           *services.UserService
	recommendationService *services.RecommendationService
}

func NewHandler(
	questionService *services.QuestionService,
	analysisService *services.AnalysisService,
	answerService *services.AnswerService,
	userService *services.UserService,
	recommendationService *services.RecommendationService,
) *Handler {
	return &Handler{
		questionService:       questionService,
		analysisService:       analysisService,
		answerService:         answerService,
		userService:           userService,
		recommendationService: recommendationService,
	}
}

func (h *Handler) Health(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (h *Handler) HomeRecommendations(w http.ResponseWriter, r *http.Request) {
	userID := parseInt64WithDefault(r.URL.Query().Get("user_id"), 1)
	response := h.recommendationService.Recommend(userID)
	writeJSON(w, http.StatusOK, response)
}

func (h *Handler) Questions(w http.ResponseWriter, r *http.Request) {
	query := r.URL.Query()
	questions := h.questionService.List(
		query.Get("category"),
		query.Get("difficulty"),
		query.Get("tag"),
		query.Get("keyword"),
	)
	writeJSON(w, http.StatusOK, map[string]any{
		"total":     len(questions),
		"questions": questions,
	})
}

func (h *Handler) QuestionByID(w http.ResponseWriter, r *http.Request) {
	questionID, err := extractQuestionID(r.URL.Path)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}

	question, err := h.questionService.Get(questionID)
	if err != nil {
		writeError(w, http.StatusNotFound, err)
		return
	}

	userID := parseInt64WithDefault(r.URL.Query().Get("user_id"), 1)
	status := h.getStatus(userID, questionID)
	writeJSON(w, http.StatusOK, map[string]any{
		"question": question,
		"status":   status,
	})
}

func (h *Handler) QuestionAnalysis(w http.ResponseWriter, r *http.Request) {
	questionID, err := extractQuestionID(r.URL.Path)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	answer, err := h.analysisService.GetStandardAnswer(questionID)
	if err != nil {
		writeError(w, http.StatusNotFound, err)
		return
	}
	writeJSON(w, http.StatusOK, answer)
}

func (h *Handler) SubmitAnswer(w http.ResponseWriter, r *http.Request) {
	questionID, err := extractQuestionID(r.URL.Path)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}

	var input services.SubmitInput
	if err := json.NewDecoder(r.Body).Decode(&input); err != nil {
		writeError(w, http.StatusBadRequest, errors.New("invalid request body"))
		return
	}
	if input.UserID == 0 {
		input.UserID = 1
	}

	out, err := h.answerService.Submit(questionID, input)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	writeJSON(w, http.StatusOK, out)
}

func (h *Handler) UserAnswers(w http.ResponseWriter, r *http.Request) {
	userID, err := extractUserID(r.URL.Path)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	answers, err := h.answerService.ListByUser(userID)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"total":   len(answers),
		"answers": answers,
	})
}

func (h *Handler) UpdateStatus(w http.ResponseWriter, r *http.Request) {
	userID, questionID, err := extractUserAndQuestionIDs(r.URL.Path)
	if err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	payload := struct {
		Status models.AnswerStatus `json:"status"`
	}{}
	if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
		writeError(w, http.StatusBadRequest, errors.New("invalid request body"))
		return
	}
	if err := h.userService.SetStatus(userID, questionID, payload.Status); err != nil {
		writeError(w, http.StatusBadRequest, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"user_id":     userID,
		"question_id": questionID,
		"status":      payload.Status,
	})
}

func (h *Handler) getStatus(userID, questionID int64) models.AnswerStatus {
	answers, err := h.answerService.ListByUser(userID)
	if err == nil {
		for _, ans := range answers {
			if ans.QuestionID == questionID {
				return ans.Status
			}
		}
	}
	return models.StatusTodo
}

func writeJSON(w http.ResponseWriter, code int, payload any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(payload)
}

func writeError(w http.ResponseWriter, code int, err error) {
	writeJSON(w, code, map[string]string{"error": err.Error()})
}

func parseInt64WithDefault(value string, def int64) int64 {
	if value == "" {
		return def
	}
	parsed, err := strconv.ParseInt(value, 10, 64)
	if err != nil {
		return def
	}
	return parsed
}

func extractQuestionID(path string) (int64, error) {
	parts := strings.Split(strings.Trim(path, "/"), "/")
	if len(parts) < 3 {
		return 0, errors.New("invalid question path")
	}
	return strconv.ParseInt(parts[2], 10, 64)
}

func extractUserID(path string) (int64, error) {
	parts := strings.Split(strings.Trim(path, "/"), "/")
	if len(parts) < 3 {
		return 0, errors.New("invalid user path")
	}
	return strconv.ParseInt(parts[2], 10, 64)
}

func extractUserAndQuestionIDs(path string) (int64, int64, error) {
	parts := strings.Split(strings.Trim(path, "/"), "/")
	if len(parts) < 6 {
		return 0, 0, errors.New("invalid status update path")
	}
	userID, err := strconv.ParseInt(parts[2], 10, 64)
	if err != nil {
		return 0, 0, errors.New("invalid user id")
	}
	questionID, err := strconv.ParseInt(parts[4], 10, 64)
	if err != nil {
		return 0, 0, errors.New("invalid question id")
	}
	return userID, questionID, nil
}
