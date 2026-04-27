package services

import (
	"slices"

	"vibecoding/backend/internal/models"
	"vibecoding/backend/internal/store"
)

type RecommendationService struct {
	store *store.Store
}

func NewRecommendationService(s *store.Store) *RecommendationService {
	return &RecommendationService{store: s}
}

func (s *RecommendationService) Recommend(userID int64) models.RecommendationResponse {
	result := make([]models.Recommendation, 0, 6)
	seen := map[int64]struct{}{}

	userAnswers := s.store.ListUserAnswers(userID)
	questions := s.store.ListQuestions("", "", "", "")

	questionByID := map[int64]models.Question{}
	for _, q := range questions {
		questionByID[q.ID] = q
	}

	for _, answer := range userAnswers {
		if answer.Score >= 70 {
			continue
		}
		q, ok := questionByID[answer.QuestionID]
		if !ok {
			continue
		}
		result = append(result, models.Recommendation{
			Question:      q,
			Reason:        "根据错题推荐",
			ExpectedScore: 75,
		})
		seen[q.ID] = struct{}{}
	}

	for _, answer := range userAnswers {
		if answer.Score >= 70 {
			continue
		}
		q, ok := questionByID[answer.QuestionID]
		if !ok {
			continue
		}
		for _, candidate := range questions {
			if candidate.ID == q.ID {
				continue
			}
			if _, ok := seen[candidate.ID]; ok {
				continue
			}
			if overlap(candidate.Tags, q.Tags) {
				result = append(result, models.Recommendation{
					Question:      candidate,
					Reason:        "根据标签推荐",
					ExpectedScore: 78,
				})
				seen[candidate.ID] = struct{}{}
			}
		}
	}

	for _, q := range s.store.HighFrequencyQuestions(6) {
		if _, ok := seen[q.ID]; ok {
			continue
		}
		result = append(result, models.Recommendation{
			Question:      q,
			Reason:        "高频面试题",
			ExpectedScore: 80,
		})
		seen[q.ID] = struct{}{}
	}

	if len(result) > 6 {
		result = result[:6]
	}

	return models.RecommendationResponse{
		UserID:          userID,
		Recommendations: result,
	}
}

func overlap(a, b []string) bool {
	for _, item := range a {
		if slices.Contains(b, item) {
			return true
		}
	}
	return false
}
