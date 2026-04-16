import Link from "next/link";
import { getRecommendations, listUserAnswers } from "../lib/api";

export default async function HomePage() {
  const [recommendations, answers] = await Promise.all([
    getRecommendations(1),
    listUserAnswers(1)
  ]);

  return (
    <>
      <section className="section">
        <h1 className="title">首页 · 推荐题</h1>
        <p className="muted">
          训练闭环：题库 -> 做题 -> AI 评分 -> 解析 -> 状态流转（未做 / 已提交 / 已评分 / 已掌握）
        </p>
        <div className="grid">
          {recommendations.map((item) => (
            <article key={item.question.id} className="card">
              <h3>{item.question.title}</h3>
              <div className="meta">
                <span>{item.question.category}</span>
                <span>{item.question.difficulty}</span>
                <span>推荐理由：{item.reason}</span>
                <span>期望分：{item.expected_score}</span>
              </div>
              <Link href={`/practice/${item.question.id}`} className="btn">
                去做题
              </Link>
            </article>
          ))}
        </div>
      </section>

      <section className="section">
        <h2 className="title">最近作答</h2>
        {answers.length === 0 ? (
          <p className="muted">你还没有作答记录，先去题库挑一道题开始吧。</p>
        ) : (
          <div className="grid">
            {answers.slice(0, 5).map((ans) => (
              <article key={ans.id} className="card">
                <div className="meta">
                  <span>题目ID: {ans.question_id}</span>
                  <span>分数: {ans.score}</span>
                  <span>状态: {ans.status}</span>
                  <span>{new Date(ans.created_at).toLocaleString()}</span>
                </div>
                <Link href={`/analysis/${ans.question_id}`} className="btn-outline">
                  查看解析
                </Link>
              </article>
            ))}
          </div>
        )}
      </section>
    </>
  );
}
