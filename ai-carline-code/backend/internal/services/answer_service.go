package services

import (
	"errors"
	"strings"

	"vibecoding/backend/internal/models"
	"vibecoding/backend/internal/store"
)

type AnswerService struct {
	store           *store.Store
	userService     *UserService
	questionService *QuestionService
	analysisService *AnalysisService
	aiService       *AIService
}

type SubmitInput struct {
	UserID     int64  `json:"user_id"`
	UserAnswer string `json:"user_answer"`
}

type SubmitOutput struct {
	Question       models.Question       `json:"question"`
	UserAnswer     models.UserAnswer     `json:"user_answer"`
	StandardAnswer models.StandardAnswer `json:"standard_answer"`
}

func NewAnswerService(
	s *store.Store,
	userService *UserService,
	questionService *QuestionService,
	analysisService *AnalysisService,
	aiService *AIService,
) *AnswerService {
	return &AnswerService{
		store:           s,
		userService:     userService,
		questionService: questionService,
		analysisService: analysisService,
		aiService:       aiService,
	}
}

func (s *AnswerService) Submit(questionID int64, input SubmitInput) (SubmitOutput, error) {
	if err := s.userService.ValidateUser(input.UserID); err != nil {
		return SubmitOutput{}, err
	}
	if strings.TrimSpace(input.UserAnswer) == "" {
		return SubmitOutput{}, errors.New("user_answer is required")
	}

	question, err := s.questionService.Get(questionID)
	if err != nil {
		return SubmitOutput{}, err
	}
	standard, err := s.analysisService.GetStandardAnswer(questionID)
	if err != nil {
		return SubmitOutput{}, err
	}
	evaluation := s.aiService.Evaluate(question, standard, input.UserAnswer)

	status := models.StatusScored
	answer := models.UserAnswer{
		UserID:      input.UserID,
		QuestionID:  questionID,
		UserAnswer:  input.UserAnswer,
		Score:       evaluation.Score,
		Feedback:    evaluation.Feedback,
		Suggestions: evaluation.Suggestions,
		Status:      status,
	}
	saved := s.store.SaveUserAnswer(answer)

	return SubmitOutput{
		Question:       question,
		UserAnswer:     saved,
		StandardAnswer: standard,
	}, nil
}

func (s *AnswerService) ListByUser(userID int64) ([]models.UserAnswer, error) {
	if err := s.userService.ValidateUser(userID); err != nil {
		return nil, err
	}
	return s.store.ListUserAnswers(userID), nil
}
