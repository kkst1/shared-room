import Link from "next/link";
import "./globals.css";

export const metadata = {
  title: "VibeCoding 八股训练",
  description: "题库、做题、AI 评分、解析一体化训练平台"
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="zh-CN">
      <body>
        <header className="header">
          <div className="container nav">
            <Link href="/" className="brand">
              VibeCoding
            </Link>
            <nav className="links">
              <Link href="/">首页</Link>
              <Link href="/questions">题库页</Link>
            </nav>
          </div>
        </header>
        <main className="container">{children}</main>
      </body>
    </html>
  );
}
