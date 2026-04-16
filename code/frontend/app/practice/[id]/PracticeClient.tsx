"use client";

import { useState } from "react";
import { submitAnswer } from "../../../lib/api";
import { Question, StandardAnswer, UserAnswer } from "../../../types";

type Props = {
  question: Question;
  status: string;
};

type SubmitResult = {
  userAnswer: UserAnswer;
  standardAnswer: StandardAnswer;
};

export default function PracticeClient({ question, status }: Props) {
  const [answer, setAnswer] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");
  const [result, setResult] = useState<SubmitResult | null>(null);

  async function onSubmit() {
    setLoading(true);
    setError("");
    try {
      const data = await submitAnswer(question.id, { user_id: 1, user_answer: answer });
      setResult({
        userAnswer: data.user_answer,
        standardAnswer: data.standard_answer
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "提交失败";
      setError(msg);
    } finally {
      setLoading(false);
    }
  }

  return (
    <section className="section">
      <h1 className="title">做题页（核心）</h1>
      <article className="card">
        <h2>{question.title}</h2>
        <div className="meta">
          <span>{question.category}</span>
          <span>{question.difficulty}</span>
          <span>当前状态：{result?.userAnswer.status ?? status}</span>
        </div>
        <p>{question.content}</p>
        <div className="meta">
          {question.tags.map((tag) => (
            <span key={tag} className="tag">
              {tag}
            </span>
          ))}
        </div>
      </article>

      <article className="card" style={{ marginTop: 14 }}>
        <h3>输入框（写答案）</h3>
        <textarea
          placeholder="按“定义 -> 原理 -> 场景 -> 优化”来写，会更容易拿高分。"
          value={answer}
          onChange={(e) => setAnswer(e.target.value)}
        />
        <div style={{ marginTop: 10, display: "flex", gap: 10 }}>
          <button className="btn" disabled={loading} onClick={onSubmit}>
            {loading ? "提交中..." : "提交按钮"}
          </button>
        </div>
        {error ? <p style={{ color: "#b91c1c" }}>{error}</p> : null}
      </article>

      {result ? (
        <>
          <article className="result">
            <div className="muted">AI评分 + 标准答案</div>
            <div className="score">{result.userAnswer.score}</div>
            <h4>反馈</h4>
            <ul>
              {result.userAnswer.feedback.map((item) => (
                <li key={item}>{item}</li>
              ))}
            </ul>
            <h4>改进建议</h4>
            <ul>
              {result.userAnswer.suggestions.map((item) => (
                <li key={item}>{item}</li>
              ))}
            </ul>
          </article>

          <article className="card" style={{ marginTop: 14 }}>
            <h3>深入解析（可展开）</h3>
            <details open>
              <summary>基础回答</summary>
              <p>{result.standardAnswer.basic_answer}</p>
            </details>
            <details>
              <summary>深入回答</summary>
              <p>{result.standardAnswer.deep_answer}</p>
            </details>
            <details>
              <summary>工程化回答</summary>
              <p>{result.standardAnswer.engineering_answer}</p>
            </details>
          </article>
        </>
      ) : null}
    </section>
  );
}
