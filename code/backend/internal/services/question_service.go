package services

import (
	"errors"

	"vibecoding/backend/internal/models"
	"vibecoding/backend/internal/store"
)

type QuestionService struct {
	store *store.Store
}

func NewQuestionService(s *store.Store) *QuestionService {
	return &QuestionService{store: s}
}

func (s *QuestionService) List(category, difficulty, tag, keyword string) []models.Question {
	return s.store.ListQuestions(category, difficulty, tag, keyword)
}

func (s *QuestionService) Get(questionID int64) (models.Question, error) {
	q, ok := s.store.GetQuestion(questionID)
	if !ok {
		return models.Question{}, errors.New("question not found")
	}
	return q, nil
}
