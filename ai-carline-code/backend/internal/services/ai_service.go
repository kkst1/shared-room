package services

import (
	"regexp"
	"strings"

	"vibecoding/backend/internal/models"
)

type AIService struct{}

func NewAIService() *AIService {
	return &AIService{}
}

func (s *AIService) Evaluate(question models.Question, standard models.StandardAnswer, userAnswer string) models.EvaluationResult {
	answer := strings.TrimSpace(strings.ToLower(userAnswer))
	if answer == "" {
		return models.EvaluationResult{
			Score: 0,
			Feedback: []string{
				"答案为空，未覆盖核心考点。",
			},
			Suggestions: []string{
				"先给出定义，再补充底层原理，最后补工程优化点。",
			},
		}
	}

	keywords := extractKeywords(standard.BasicAnswer + " " + standard.DeepAnswer + " " + standard.EngineeringAnswer)
	matched := 0
	missing := make([]string, 0)
	for _, kw := range keywords {
		if strings.Contains(answer, kw) {
			matched++
		} else {
			missing = append(missing, kw)
		}
	}

	coverage := float64(matched) / float64(len(keywords))
	score := int(coverage * 100)
	if score < 45 && len(answer) > 120 {
		score += 8
	}
	if score > 98 {
		score = 98
	}

	feedback := []string{
		"已从基础定义、机制原理、工程实践三个维度进行评估。",
	}
	if len(missing) > 0 {
		feedback = append(feedback, "当前答案缺少关键点："+strings.Join(sample(missing, 4), "、"))
	} else {
		feedback = append(feedback, "关键词覆盖较完整，结构性表达较好。")
	}

	suggestions := []string{
		"建议按“定义 -> 原理 -> 场景 -> 优化”结构作答。",
		"加入 1 个线上案例或性能指标，让回答更有说服力。",
	}
	if question.Category == "操作系统" {
		suggestions = append(suggestions, "补充调度、内存或系统调用相关细节会更加分。")
	}
	if score >= 80 {
		suggestions = append(suggestions, "可以继续补充反例和边界条件，冲刺高分回答。")
	}

	return models.EvaluationResult{
		Score:       score,
		Feedback:    feedback,
		Suggestions: suggestions,
	}
}

func extractKeywords(text string) []string {
	lower := strings.ToLower(text)
	re := regexp.MustCompile(`[a-zA-Z0-9_]+|[\p{Han}]{2,}`)
	tokens := re.FindAllString(lower, -1)
	unique := map[string]struct{}{}
	out := make([]string, 0)
	for _, token := range tokens {
		if len(token) < 2 {
			continue
		}
		if _, exists := unique[token]; exists {
			continue
		}
		unique[token] = struct{}{}
		out = append(out, token)
		if len(out) >= 12 {
			break
		}
	}
	if len(out) == 0 {
		return []string{"核心原理", "关键机制", "工程实践"}
	}
	return out
}

func sample(items []string, n int) []string {
	if len(items) <= n {
		return items
	}
	return items[:n]
}
