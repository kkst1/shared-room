package store

import (
	"errors"
	"slices"
	"strings"
	"sync"
	"time"

	"vibecoding/backend/internal/models"
)

type Store struct {
	mu sync.RWMutex

	nextAnswerID int64

	users           map[int64]models.User
	questions       map[int64]models.Question
	standardAnswers map[int64]models.StandardAnswer
	userAnswers     []models.UserAnswer
	userProgress    map[int64]map[int64]models.AnswerStatus
}

func New() *Store {
	s := &Store{
		nextAnswerID:    1,
		users:           map[int64]models.User{},
		questions:       map[int64]models.Question{},
		standardAnswers: map[int64]models.StandardAnswer{},
		userAnswers:     []models.UserAnswer{},
		userProgress:    map[int64]map[int64]models.AnswerStatus{},
	}
	s.seed()
	return s
}

func (s *Store) seed() {
	s.users[1] = models.User{ID: 1, Name: "demo-user"}

	questions := []models.Question{
		{
			ID:         1,
			Title:      "线程与进程上下文切换的核心差异",
			Content:    "请从开销来源、地址空间、调度粒度三个维度说明线程切换与进程切换的区别，并结合 Linux 实现谈优化思路。",
			Category:   "操作系统",
			Difficulty: "中等",
			Tags:       []string{"进程", "线程", "调度", "上下文切换"},
		},
		{
			ID:         2,
			Title:      "TCP 三次握手为什么不能是两次",
			Content:    "从可靠性、历史连接污染、序列号同步角度，解释三次握手的必要性，并说明 SYN flood 的工程化防护。",
			Category:   "计算机网络",
			Difficulty: "中等",
			Tags:       []string{"TCP", "握手", "可靠性", "安全"},
		},
		{
			ID:         3,
			Title:      "Go map 并发读写为什么会 panic",
			Content:    "解释 Go map 的并发安全问题、扩容机制触发点，并给出常见工程解法。",
			Category:   "编程语言",
			Difficulty: "简单",
			Tags:       []string{"Go", "map", "并发", "运行时"},
		},
		{
			ID:         4,
			Title:      "从源码视角解释 epoll 的高性能来源",
			Content:    "请对比 select/poll，解释 epoll 在事件注册、就绪通知、遍历复杂度上的优化点。",
			Category:   "操作系统",
			Difficulty: "困难",
			Tags:       []string{"epoll", "IO多路复用", "内核"},
		},
	}

	answers := []models.StandardAnswer{
		{
			QuestionID:        1,
			BasicAnswer:       "线程切换通常只切寄存器和线程栈；进程切换还涉及地址空间切换和 TLB 影响。",
			DeepAnswer:        "Linux 下进程切换会变更 mm 结构，可能触发 TLB flush；线程切换共享地址空间，开销更低。",
			EngineeringAnswer: "高并发场景用线程池、减少锁竞争、降低线程数震荡，能显著减少上下文切换带来的尾延迟。",
			FrequencyWeight:   9,
		},
		{
			QuestionID:        2,
			BasicAnswer:       "三次握手确保双方都具备收发能力，并确认初始序列号。",
			DeepAnswer:        "两次无法避免旧 SYN 导致的半开连接错判；第三次 ACK 用于确认服务端 SYN-ACK 被客户端正确接收。",
			EngineeringAnswer: "线上可配合 SYN Cookie、半连接队列监控、连接速率限制防止 SYN flood。",
			FrequencyWeight:   10,
		},
		{
			QuestionID:        3,
			BasicAnswer:       "Go map 在写入时会扩容和重排桶，并发读写会破坏内部状态，因此 runtime 直接 panic。",
			DeepAnswer:        "并发写触发数据竞争，hmap/bmap 结构可能被同时修改；读线程看到中间态会导致不一致。",
			EngineeringAnswer: "使用 sync.RWMutex、sync.Map、分片 map 或 channel 串行化访问。",
			FrequencyWeight:   8,
		},
		{
			QuestionID:        4,
			BasicAnswer:       "epoll 把关注集合维护在内核，通过回调把活跃 fd 加入就绪队列，避免每次全量遍历。",
			DeepAnswer:        "select/poll 每次都要把用户态集合拷贝到内核并线性扫描；epoll_wait 只处理就绪队列。",
			EngineeringAnswer: "生产中要注意 LT/ET 模式差异、非阻塞 IO、惊群和 fd 生命周期管理。",
			FrequencyWeight:   7,
		},
	}

	for _, q := range questions {
		s.questions[q.ID] = q
	}
	for _, a := range answers {
		s.standardAnswers[a.QuestionID] = a
	}
}

func (s *Store) GetUser(userID int64) (models.User, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	user, ok := s.users[userID]
	return user, ok
}

func (s *Store) ListQuestions(category, difficulty, tag, keyword string) []models.Question {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := make([]models.Question, 0, len(s.questions))
	for _, q := range s.questions {
		if category != "" && !strings.EqualFold(q.Category, category) {
			continue
		}
		if difficulty != "" && !strings.EqualFold(q.Difficulty, difficulty) {
			continue
		}
		if tag != "" && !slices.Contains(q.Tags, tag) {
			continue
		}
		if keyword != "" {
			k := strings.ToLower(keyword)
			if !strings.Contains(strings.ToLower(q.Title), k) && !strings.Contains(strings.ToLower(q.Content), k) {
				continue
			}
		}
		result = append(result, q)
	}
	slices.SortFunc(result, func(a, b models.Question) int {
		if a.ID < b.ID {
			return -1
		}
		if a.ID > b.ID {
			return 1
		}
		return 0
	})
	return result
}

func (s *Store) GetQuestion(questionID int64) (models.Question, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	q, ok := s.questions[questionID]
	return q, ok
}

func (s *Store) GetStandardAnswer(questionID int64) (models.StandardAnswer, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	ans, ok := s.standardAnswers[questionID]
	return ans, ok
}

func (s *Store) SaveUserAnswer(ans models.UserAnswer) models.UserAnswer {
	s.mu.Lock()
	defer s.mu.Unlock()
	ans.ID = s.nextAnswerID
	s.nextAnswerID++
	ans.CreatedAt = time.Now()
	s.userAnswers = append(s.userAnswers, ans)

	if _, ok := s.userProgress[ans.UserID]; !ok {
		s.userProgress[ans.UserID] = map[int64]models.AnswerStatus{}
	}
	s.userProgress[ans.UserID][ans.QuestionID] = ans.Status

	return ans
}

func (s *Store) ListUserAnswers(userID int64) []models.UserAnswer {
	s.mu.RLock()
	defer s.mu.RUnlock()
	result := make([]models.UserAnswer, 0)
	for _, ans := range s.userAnswers {
		if ans.UserID == userID {
			result = append(result, ans)
		}
	}
	slices.SortFunc(result, func(a, b models.UserAnswer) int {
		if a.CreatedAt.Before(b.CreatedAt) {
			return 1
		}
		if a.CreatedAt.After(b.CreatedAt) {
			return -1
		}
		return 0
	})
	return result
}

func (s *Store) SetUserQuestionStatus(userID, questionID int64, status models.AnswerStatus) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.users[userID]; !ok {
		return errors.New("user not found")
	}
	if _, ok := s.questions[questionID]; !ok {
		return errors.New("question not found")
	}
	if _, ok := s.userProgress[userID]; !ok {
		s.userProgress[userID] = map[int64]models.AnswerStatus{}
	}
	s.userProgress[userID][questionID] = status
	return nil
}

func (s *Store) GetUserQuestionStatus(userID, questionID int64) models.AnswerStatus {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if statusByQuestion, ok := s.userProgress[userID]; ok {
		if status, ok := statusByQuestion[questionID]; ok {
			return status
		}
	}
	return models.StatusTodo
}

func (s *Store) HighFrequencyQuestions(limit int) []models.Question {
	s.mu.RLock()
	defer s.mu.RUnlock()

	if limit <= 0 {
		limit = 3
	}

	type ranked struct {
		q models.Question
		w int
	}
	buffer := make([]ranked, 0, len(s.questions))
	for qid, q := range s.questions {
		w := s.standardAnswers[qid].FrequencyWeight
		buffer = append(buffer, ranked{q: q, w: w})
	}

	slices.SortFunc(buffer, func(a, b ranked) int {
		if a.w > b.w {
			return -1
		}
		if a.w < b.w {
			return 1
		}
		return 0
	})

	out := make([]models.Question, 0, limit)
	for idx, item := range buffer {
		if idx >= limit {
			break
		}
		out = append(out, item.q)
	}
	return out
}
