import Link from "next/link";
import { listQuestions } from "../../lib/api";

type Props = {
  searchParams?: {
    category?: string;
    difficulty?: string;
    tag?: string;
    keyword?: string;
  };
};

export default async function QuestionPage({ searchParams }: Props) {
  const questions = await listQuestions({
    category: searchParams?.category,
    difficulty: searchParams?.difficulty,
    tag: searchParams?.tag,
    keyword: searchParams?.keyword
  });

  return (
    <section className="section">
      <h1 className="title">题库页</h1>
      <p className="muted">当前为简化版筛选，可直接通过 URL 查询参数过滤。</p>
      <div className="row" style={{ marginBottom: 16 }}>
        <form action="/questions">
          <label htmlFor="keyword">关键词</label>
          <input id="keyword" name="keyword" defaultValue={searchParams?.keyword ?? ""} />
        </form>
        <form action="/questions">
          <label htmlFor="tag">标签</label>
          <input id="tag" name="tag" placeholder="如: 并发" defaultValue={searchParams?.tag ?? ""} />
        </form>
      </div>
      <div className="grid">
        {questions.map((q) => (
          <article key={q.id} className="card">
            <h3>{q.title}</h3>
            <p className="muted">{q.content}</p>
            <div className="meta">
              <span>{q.category}</span>
              <span>{q.difficulty}</span>
              {q.tags.map((tag) => (
                <span key={`${q.id}-${tag}`} className="tag">
                  {tag}
                </span>
              ))}
            </div>
            <div style={{ display: "flex", gap: 10 }}>
              <Link href={`/practice/${q.id}`} className="btn">
                做题
              </Link>
              <Link href={`/analysis/${q.id}`} className="btn-outline">
                看解析
              </Link>
            </div>
          </article>
        ))}
      </div>
    </section>
  );
}
