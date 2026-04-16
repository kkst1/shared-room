package services

import (
	"errors"

	"vibecoding/backend/internal/models"
	"vibecoding/backend/internal/store"
)

type AnalysisService struct {
	store *store.Store
}

func NewAnalysisService(s *store.Store) *AnalysisService {
	return &AnalysisService{store: s}
}

func (s *AnalysisService) GetStandardAnswer(questionID int64) (models.StandardAnswer, error) {
	ans, ok := s.store.GetStandardAnswer(questionID)
	if !ok {
		return models.StandardAnswer{}, errors.New("analysis not found")
	}
	return ans, nil
}
