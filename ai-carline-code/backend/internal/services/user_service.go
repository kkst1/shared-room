package services

import (
	"errors"

	"vibecoding/backend/internal/models"
	"vibecoding/backend/internal/store"
)

type UserService struct {
	store *store.Store
}

func NewUserService(s *store.Store) *UserService {
	return &UserService{store: s}
}

func (s *UserService) ValidateUser(userID int64) error {
	_, ok := s.store.GetUser(userID)
	if !ok {
		return errors.New("user not found")
	}
	return nil
}

func (s *UserService) SetStatus(userID, questionID int64, status models.AnswerStatus) error {
	if status != models.StatusTodo && status != models.StatusSubmitted && status != models.StatusScored && status != models.StatusMastered {
		return errors.New("invalid status")
	}
	return s.store.SetUserQuestionStatus(userID, questionID, status)
}
