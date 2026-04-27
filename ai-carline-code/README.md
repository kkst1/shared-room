# VibeCoding Code

基于 `architecture` 的最小可运行实现，目录如下：

- `backend`：Golang API（用户、题库、作答、解析、AI评分、推荐）
- `frontend`：Next.js Web（首页、题库页、做题页、解析页）

## 1) 启动后端

```bash
cd backend
go run ./cmd/server
```

默认监听 `http://localhost:8080`。

## 2) 启动前端

```bash
cd frontend
npm install
npm run dev
```

默认访问 `http://localhost:3000`。

可通过环境变量配置后端地址：

```bash
NEXT_PUBLIC_API_BASE_URL=http://localhost:8080
```

## 3) 已实现的 API

- `GET /api/health`
- `GET /api/home/recommendations?user_id=1`
- `GET /api/questions?category=&difficulty=&tag=&keyword=`
- `GET /api/questions/{id}`
- `GET /api/questions/{id}/analysis`
- `POST /api/questions/{id}/submit`
- `GET /api/users/{id}/answers`
- `PATCH /api/users/{id}/questions/{question_id}/status`

## 4) 当前实现说明

- 数据层为内存存储（内置 seed 数据），便于快速演示。
- AI 评分采用规则引擎占位，后续可替换成真实 LLM API。
- 推荐策略包含错题推荐、标签推荐、高频题推荐三类基础逻辑。
