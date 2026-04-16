export type Question = {
  id: number;
  title: string;
  content: string;
  category: string;
  difficulty: string;
  tags: string[];
};

export type StandardAnswer = {
  question_id: number;
  basic_answer: string;
  deep_answer: string;
  engineering_answer: string;
};

export type UserAnswer = {
  id: number;
  user_id: number;
  question_id: number;
  user_answer: string;
  score: number;
  feedback: string[];
  suggestions: string[];
  status: string;
  created_at: string;
};

export type Recommendation = {
  question: Question;
  reason: string;
  expected_score: number;
};
