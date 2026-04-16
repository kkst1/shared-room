import Link from "next/link";
import { getAnalysis, getQuestion } from "../../../lib/api";

type Props = {
  params: { id: string };
};

export default async function AnalysisPage({ params }: Props) {
  const questionID = Number(params.id);
  const [questionRes, analysis] = await Promise.all([getQuestion(questionID), getAnalysis(questionID)]);

  return (
    <section className="section">
      <h1 className="title">解析页</h1>
      <article className="card">
        <h2>{questionRes.question.title}</h2>
        <p className="muted">{questionRes.question.content}</p>
        <div className="meta">
          <span>状态：{questionRes.status}</span>
          <span>{questionRes.question.category}</span>
          <span>{questionRes.question.difficulty}</span>
        </div>
      </article>

      <article className="card" style={{ marginTop: 14 }}>
        <h3>标准答案</h3>
        <p>
          <strong>基础回答：</strong>
          {analysis.basic_answer}
        </p>
        <p>
          <strong>深入回答：</strong>
          {analysis.deep_answer}
        </p>
        <p>
          <strong>工程化回答：</strong>
          {analysis.engineering_answer}
        </p>
      </article>

      <div style={{ marginTop: 14, display: "flex", gap: 10 }}>
        <Link href={`/practice/${questionID}`} className="btn">
          返回做题页
        </Link>
        <Link href="/questions" className="btn-outline">
          返回题库
        </Link>
      </div>
    </section>
  );
}
