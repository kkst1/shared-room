package models

import "time"

type Question struct {
	ID         int64    `json:"id"`
	Title      string   `json:"title"`
	Content    string   `json:"content"`
	Category   string   `json:"category"`
	Difficulty string   `json:"difficulty"`
	Tags       []string `json:"tags"`
}

type StandardAnswer struct {
	QuestionID         int64  `json:"question_id"`
	BasicAnswer        string `json:"basic_answer"`
	DeepAnswer         string `json:"deep_answer"`
	EngineeringAnswer  string `json:"engineering_answer"`
	FrequencyWeight    int    `json:"-"`
}

type User struct {
	ID   int64  `json:"id"`
	Name string `json:"name"`
}

type AnswerStatus string

const (
	StatusTodo      AnswerStatus = "未做"
	StatusSubmitted AnswerStatus = "已提交"
	StatusScored    AnswerStatus = "已评分"
	StatusMastered  AnswerStatus = "已掌握"
)

type UserAnswer struct {
	ID          int64        `json:"id"`
	UserID      int64        `json:"user_id"`
	QuestionID  int64        `json:"question_id"`
	UserAnswer  string       `json:"user_answer"`
	Score       int          `json:"score"`
	Feedback    []string     `json:"feedback"`
	Suggestions []string     `json:"suggestions"`
	Status      AnswerStatus `json:"status"`
	CreatedAt   time.Time    `json:"created_at"`
}

type EvaluationResult struct {
	Score       int      `json:"score"`
	Feedback    []string `json:"feedback"`
	Suggestions []string `json:"suggestions"`
}

type Recommendation struct {
	Question      Question `json:"question"`
	Reason        string   `json:"reason"`
	ExpectedScore int      `json:"expected_score"`
}

type RecommendationResponse struct {
	UserID          int64            `json:"user_id"`
	Recommendations []Recommendation `json:"recommendations"`
}
