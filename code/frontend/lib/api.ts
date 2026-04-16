import { Question, Recommendation, StandardAnswer, UserAnswer } from "../types";

const API_BASE = process.env.NEXT_PUBLIC_API_BASE_URL ?? "http://localhost:8080";

async function fetchJSON<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    ...init,
    headers: {
      "Content-Type": "application/json",
      ...(init?.headers ?? {})
    },
    cache: "no-store"
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text || `Request failed: ${res.status}`);
  }
  return res.json() as Promise<T>;
}

export async function getRecommendations(userId = 1): Promise<Recommendation[]> {
  const data = await fetchJSON<{ recommendations: Recommendation[] }>(
    `/api/home/recommendations?user_id=${userId}`
  );
  return data.recommendations;
}

export async function listQuestions(params?: {
  category?: string;
  difficulty?: string;
  tag?: string;
  keyword?: string;
}): Promise<Question[]> {
  const search = new URLSearchParams();
  if (params?.category) search.set("category", params.category);
  if (params?.difficulty) search.set("difficulty", params.difficulty);
  if (params?.tag) search.set("tag", params.tag);
  if (params?.keyword) search.set("keyword", params.keyword);

  const query = search.toString();
  const data = await fetchJSON<{ questions: Question[] }>(
    `/api/questions${query ? `?${query}` : ""}`
  );
  return data.questions;
}

export async function getQuestion(questionId: number): Promise<{ question: Question; status: string }> {
  return fetchJSON(`/api/questions/${questionId}`);
}

export async function getAnalysis(questionId: number): Promise<StandardAnswer> {
  return fetchJSON(`/api/questions/${questionId}/analysis`);
}

export async function submitAnswer(questionId: number, payload: { user_id: number; user_answer: string }) {
  return fetchJSON<{
    question: Question;
    user_answer: UserAnswer;
    standard_answer: StandardAnswer;
  }>(`/api/questions/${questionId}/submit`, {
    method: "POST",
    body: JSON.stringify(payload)
  });
}

export async function listUserAnswers(userId = 1): Promise<UserAnswer[]> {
  const data = await fetchJSON<{ answers: UserAnswer[] }>(`/api/users/${userId}/answers`);
  return data.answers;
}
