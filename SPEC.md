# SPEC.md — VibeCoding OJ 规格说明书

> 版本：v1.0
> 日期：2026-10-12
> 状态：已确认，可进入开发
> 定位：求职作品集项目

---

## 目录

1. [项目目标与成功标准](#1-项目目标与成功标准)
2. [范围界定](#2-范围界定)
3. [角色与核心用例](#3-角色与核心用例)
4. [技术栈与选型理由](#4-技术栈与选型理由)
5. [系统架构](#5-系统架构)
6. [数据库设计](#6-数据库设计)
7. [API 设计](#7-api-设计)
8. [判题子系统](#8-判题子系统)
9. [前端与 UI/UX](#9-前端与-uiux)
10. [安全设计](#10-安全设计)
11. [非功能需求](#11-非功能需求)
12. [边缘案例与异常处理](#12-边缘案例与异常处理)
13. [已知风险与权衡](#13-已知风险与权衡)
14. [项目目录结构](#14-项目目录结构)
15. [TODO 清单](#15-todo-清单)
16. [验收标准](#16-验收标准)
17. [附录：关键决策记录](#17-附录关键决策记录)

---

## 1. 项目目标与成功标准

### 1.1 目标

构建一个仿 LeetCode 的在线判题平台（Online Judge），支持用户注册登录、浏览题库、在线编写 C/C++ 代码、提交判题、查看提交记录与排行榜，管理员可通过后台管理题目与测试数据。

**本项目的首要目标是"可讲清楚"，而非"功能多"。** 每一个技术选型都应当能在面试中被追问三层而不露怯。

### 1.2 成功标准（可验证）

项目视为达成目标，当且仅当以下两条同时满足：

| # | 标准 | 验证方式 |
|---|---|---|
| S1 | **全流程演示无卡顿** | 按 [16.1 演示脚本](#161-演示脚本) 连续走完 7 步，无异常弹窗、无空白页、无超过 3 秒的无响应 |
| S2 | **陌生机器 clone 后可跑通** | 在一台干净 Linux 机器上，严格只按 README 的复制粘贴命令操作，能启动服务并完成一次提交 |

> S2 是本项目与"自己电脑上的玩具"的分界线，优先级等同于功能开发本身。

### 1.3 明确不追求的目标

- 不追求支撑公开规模的并发（当前目标：同时提交 5–20）
- 不追求语言广度（仅 C/C++）
- 不追求竞赛级判题隔离（不使用容器 / cgroup / nsjail）

---

## 2. 范围界定

### 2.1 本期实现（In Scope）

**用户侧**
- 注册 / 登录 / 登出，JWT 鉴权
- 题目列表：分页、按难度筛选、按标签筛选、按我的作答状态筛选、显示通过率
- 题目详情：Markdown 题面渲染、样例展示、代码编辑器
- 运行代码（跑题面样例，支持自定义输入）——不产生提交记录
- 提交代码（跑全量隐藏用例）——产生提交记录
- 我的提交记录列表 + 单条提交详情
- 排行榜

**管理侧**
- 题目完整 CRUD（含删除，软删除语义）
- 测试数据上传 / 替换 / 查看

**系统侧**
- 进程内判题队列与线程池
- 沙箱执行（资源限制 + 降权 + 目录隔离）
- 判题态一致性自愈
- 资源滥用防护

### 2.2 本期明确不做（Out of Scope）

以下项目**经过讨论被有意排除**，不是"忘了"：

| 不做项 | 理由 |
|---|---|
| 全局提交广场 | 与"我的提交记录"信息重复度高，收益低于成本 |
| 比赛 / 周赛模式 | 需要独立的赛制、计时、封榜逻辑，工作量大且与主线无关 |
| 讨论区 / 题解区 | 社区功能需要内容审核，是另一个产品 |
| 多语言支持 | 仅 C/C++，沙箱与判题逻辑保持纯粹 |
| 密码找回 / 邮箱验证 | 需要邮件服务依赖，作品集场景无必要 |
| 提交频率限流 | 已用队列上限 + 代码长度上限覆盖主要风险，详见 §13 R3 |
| 提交源代码持久化 | 用户提交的代码**不落库**，见 §13 R1 |
| 移动端适配 | 做题页为桌面场景，仅保证不破版 |

---

## 3. 角色与核心用例

### 3.1 角色

| 角色 | 权限 |
|---|---|
| 游客（未登录） | 浏览题目列表、题目详情、排行榜 |
| 用户（role=USER） | 游客权限 + 运行代码 + 提交代码 + 查看自己的提交记录 |
| 管理员（role=ADMIN） | 用户权限 + 题目 CRUD + 测试数据管理 + 查看全部提交 |

### 3.2 核心用例流程

**UC-1 注册登录**
```
访问 /login.html → 输入用户名密码 → 注册 → 自动登录 → 跳转题目列表
                              └→ 或直接登录 → 签发 JWT → 前端存 token
```

**UC-2 做题（主流程）**
```
题目列表 → 筛选/翻页 → 点开某题
  → 阅读 Markdown 题面 + 样例
  → 在 CodeMirror 中写 C++ 代码
  → [可选] 点「运行」→ 输入自定义 stdin → 轮询 → 逐样例对比结果
  → 点「提交」→ 拿到 submission_id → 前端每 800ms 轮询状态
  → 看到 AC / WA(第 3 个测试点不通过) / TLE / MLE / RE / CE / OLE / SE
  → 提交记录页可回看历次结果
```

**UC-3 管理员出题**
```
后台登录（role=ADMIN）→ 题目列表 → 新增题目
  → 填标题/难度/标签/时间限制/内存限制/Markdown 题面
  → 上传测试数据（N 组 .in/.out 配对）
  → 保存 → 前台立即可见
```

---

## 4. 技术栈与选型理由

| 层次 | 选型 | 理由 | 代价 |
|---|---|---|---|
| HTTP 服务 | **cpp-httplib**（header-only） | 零构建依赖，自带 SSLServer，thread-per-connection 模型简单直接 | 无路由分组、无中间件框架，需自行封装 |
| JSON | **nlohmann/json**（单头文件，vendored 进仓库） | 事实标准，API 友好 | 编译期开销较大 |
| 数据库 | **MySQL** | 关系模型契合"用户-题目-提交"三实体；国内面试认知度高 | 需链接 libmysqlclient，多一步部署 |
| 密码哈希 | **OpenSSL PBKDF2-HMAC-SHA256** | libssl 几乎预装；PBKDF2 是标准答案，能讲清"为何不能裸存 SHA256" | 需自行设计编码格式 |
| JWT 签名 | **OpenSSL HMAC-SHA256** | 复用已有依赖，不引入新库 | 需自行实现 base64url 与 JWT 编解码 |
| 传输加密 | **cpp-httplib 原生 SSLServer + 自签证书** | 已有 OpenSSL 依赖，零额外中间件 | 浏览器证书告警，需在 README 说明 |
| 前端 | **原生 HTML + CSS + JS** | 项目硬性前提；无构建工具链 | 需自行管理状态与 DOM |
| 代码编辑器 | **CodeMirror 6**（ESM CDN） | 体积小、原生 ESM、契合"无构建工具"前提，自带 C++ 高亮 | 需处理 ESM 模块加载 |
| Markdown 渲染 | **marked + DOMPurify**（CDN） | 渲染与消毒分离，安全边界清晰 | 两个前端依赖 |
| 构建 | **手写 Makefile + apt 系统依赖** | 最少的工具链抽象 | 依赖版本不锁定，换机器需重装 |
| 部署 | **裸二进制 + 手动后台运行** | 最简单，无中间件 | 无进程守护，重启需手动拉起 |

### 4.1 系统依赖清单

```
build-essential        # g++ / make
libmysqlclient-dev     # MySQL C API
libssl-dev             # OpenSSL：PBKDF2 / HMAC / TLS
mysql-server           # 数据库服务
```

### 4.2 编译要求

- C++17 或以上
- 必须定义 `CPPHTTPLIB_OPENSSL_SUPPORT` 宏（否则 SSLServer 不可用）
- 链接：`-lmysqlclient -lssl -lcrypto -lpthread`

---

## 5. 系统架构

### 5.1 整体架构图

```
                          ┌───────────────────────────────┐
                          │          浏览器（前端）         │
                          │  HTML/CSS/JS + CodeMirror 6   │
                          │  marked + DOMPurify           │
                          └───────────────┬───────────────┘
                                          │ HTTPS（自签证书）
                                          ▼
   ┌──────────────────────────────────────────────────────────────────┐
   │                    单个 oj_server 进程（C++）                      │
   │                                                                  │
   │   ┌──────────────────────────────────────────────────────────┐   │
   │   │  cpp-httplib SSLServer（thread-per-connection）           │   │
   │   │  ┌──────────┬───────────┬──────────┬──────────────────┐  │   │
   │   │  │ 静态文件  │ 认证中间件 │ 路由分发  │ 统一响应体封装    │  │   │
   │   │  └──────────┴───────────┴──────────┴──────────────────┘  │   │
   │   └──────┬─────────────────────────────────────┬─────────────┘   │
   │          │                                     │                 │
   │          │ SQL                                 │ 投递任务         │
   │          ▼                                     ▼                 │
   │   ┌─────────────┐                   ┌──────────────────────┐     │
   │   │  Service 层  │                   │ 判题队列（内存，有上限）│     │
   │   └──────┬──────┘                   └───────────┬──────────┘     │
   │          │                                      │ 取任务          │
   │          ▼                                      ▼                │
   │   ┌─────────────┐                   ┌──────────────────────┐     │
   │   │   DAO 层     │                   │ 判题线程池（4 线程）    │     │
   │   │ MySQL 连接   │                   │  ┌─────────────────┐  │     │
   │   └──────┬──────┘                   │  │  Sandbox Runner │  │     │
   │          │                          │  │   编译 → 逐用例   │  │     │
   │          │                          │  │   判定 → 写结果   │  │     │
   │          │                          │  └────────┬────────┘  │     │
   │          │                          └───────────┼──────────┘     │
   │          │                                      │                │
   │          │  写回状态                             │ fork + exec    │
   │          └──────────────────────────────────────┤                │
   │                                                 ▼                │
   │                                    ┌────────────────────────┐    │
   │                                    │  受限子进程             │    │
   │                                    │  setrlimit → setuid    │    │
   │                                    │  → chdir(tmpdir)       │    │
   │                                    │  → exec g++ / a.out    │    │
   │                                    │  wait4 → rusage        │    │
   │                                    └────────────────────────┘    │
   └──────────────────────────────────────────────────────────────────┘
                    │                                    │
                    ▼                                    ▼
        ┌───────────────────────┐          ┌────────────────────────────┐
        │       MySQL            │          │         文件系统            │
        │  users                 │          │  data/problems/{id}/       │
        │  problems              │          │     1.in  1.out  2.in ...  │
        │  submissions           │          │  certs/server.{crt,key}    │
        │  tags                  │          │  build/  （编译临时目录）    │
        │  problem_tags          │          └────────────────────────────┘
        └───────────────────────┘
```

### 5.2 分层职责

| 层 | 职责 | 禁止 |
|---|---|---|
| **HTTP 层** | 路由注册、参数解析、鉴权中间件、统一响应封装、静态文件 | 禁止写业务逻辑、禁止直接访问数据库 |
| **Service 层** | 业务逻辑、事务边界、跨实体编排（如注册 = 校验 + 哈希 + 入库） | 禁止直接拼接 SQL、禁止感知 HTTP 细节 |
| **DAO 层** | SQL 执行、结果集到结构体的映射、预编译语句 | 禁止包含业务判断 |
| **Judge 层** | 沙箱执行、编译、逐用例判定、状态机推进 | 禁止访问 HTTP 上下文；仅通过 DAO 写结果 |

### 5.3 并发模型说明

- **HTTP 侧**：cpp-httplib 的 thread-per-connection，每个请求一个线程，阻塞式。
- **判题侧**：固定 4 个判题线程（可配置），从内存队列取任务串行处理。
- **两侧共享 CPU**：这是单进程模型的固有代价，见 §13 R2。
- **DAO 线程安全**：每个判题线程持有独立的 MySQL 连接，不使用全局共享连接（libmysqlclient 的连接非线程安全）。

---

## 6. 数据库设计

### 6.1 实体关系

```
users ──< submissions >── problems >── problem_tags ──< tags
                                    └── is_deleted（软删除）
```

### 6.2 DDL

```sql
-- 用户表
CREATE TABLE users (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(32)  NOT NULL,
    password_hash VARCHAR(255) NOT NULL COMMENT 'PBKDF2 编码串，含算法/迭代次数/salt',
    role          ENUM('USER','ADMIN') NOT NULL DEFAULT 'USER',
    created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 题目表
CREATE TABLE problems (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    title           VARCHAR(128)  NOT NULL,
    difficulty      ENUM('EASY','MEDIUM','HARD') NOT NULL,
    description_md  MEDIUMTEXT    NOT NULL COMMENT 'Markdown 原文',
    time_limit_ms   INT UNSIGNED  NOT NULL DEFAULT 1000,
    memory_limit_mb INT UNSIGNED  NOT NULL DEFAULT 256,
    is_deleted      TINYINT(1)    NOT NULL DEFAULT 0 COMMENT '软删除标记',
    created_by      BIGINT UNSIGNED NOT NULL,
    created_at      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_difficulty (difficulty),
    KEY idx_deleted (is_deleted)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 标签表
CREATE TABLE tags (
    id   BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(32) NOT NULL,
    UNIQUE KEY uk_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 题目-标签关联
CREATE TABLE problem_tags (
    problem_id BIGINT UNSIGNED NOT NULL,
    tag_id     BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (problem_id, tag_id),
    KEY idx_tag (tag_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 提交记录表（仅元数据，不含源代码）
CREATE TABLE submissions (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id         BIGINT UNSIGNED NOT NULL,
    problem_id      BIGINT UNSIGNED NOT NULL,
    language        VARCHAR(16)  NOT NULL DEFAULT 'cpp',
    status          ENUM('PENDING','JUDGING','AC','WA','TLE','MLE','OLE','RE','CE','SE')
                    NOT NULL DEFAULT 'PENDING',
    time_ms         INT UNSIGNED  NULL COMMENT 'AC/WA 时的最大耗时',
    memory_kb       INT UNSIGNED  NULL COMMENT '峰值内存（ru_maxrss）',
    failed_case_idx INT UNSIGNED  NULL COMMENT '首个失败用例序号（从 1 开始）',
    total_cases     INT UNSIGNED  NULL COMMENT '测试点总数',
    compile_msg     TEXT          NULL COMMENT '编译错误信息，截断至 4KB',
    created_at      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    finished_at     DATETIME      NULL,
    KEY idx_user_time (user_id, created_at DESC),
    KEY idx_problem_status (problem_id, status),
    KEY idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

> **注意**：`submissions` 表**不存储源代码**。这是经过讨论的有意取舍，见 §13 R1。

### 6.3 关键查询口径

**题目通过率**
```sql
-- 分子：AC 过该题的去重人数；分母：尝试过该题的去重人数（LeetCode 口径）
SELECT
  COUNT(DISTINCT CASE WHEN status='AC' THEN user_id END) AS ac_users,
  COUNT(DISTINCT user_id)                                AS tried_users
FROM submissions
WHERE problem_id = ?;
```
通过率 = `ac_users / tried_users`，分母为 0 时展示为 `--`。

> 该聚合查询在题目列表页会按页重复执行，需注意 N+1 问题：应改为一次 `GROUP BY problem_id` 批量取回。

**排行榜**
```sql
-- AC 题数降序；并列时，谁更早"达成该通过题数"谁靠前。
-- 排序键用 MAX 而非 MIN：MIN 表示"首次 AC 任何题的时间"，
-- 那实际是在比谁注册得早，榜单会固化成元老榜。
SELECT u.id, u.username,
       COUNT(DISTINCT s.problem_id) AS solved,
       MAX(s.finished_at)           AS reached_at
FROM users u
JOIN submissions s ON s.user_id = u.id AND s.status = 'AC'
GROUP BY u.id, u.username
HAVING solved > 0   -- 过滤 0 题用户，否则新注册用户会在榜尾并列一大片
ORDER BY solved DESC, reached_at ASC
LIMIT ? OFFSET ?;
```

**我的作答状态筛选**
`solved` = 存在 AC 记录；`attempted` = 有提交但无 AC；`todo` = 无任何提交。

---

## 7. API 设计

### 7.1 统一约定

**响应体格式**（所有 JSON 接口）
```json
{ "code": 0, "message": "ok", "data": { } }
```

**HTTP 状态码使用规则**

| 场景 | HTTP | body.code |
|---|---|---|
| 业务成功 | 200 | 0 |
| 业务失败（参数错误、资源不存在等） | 200 | 非 0 |
| 未携带 / 携带无效 token | 401 | 1001 |
| 已登录但权限不足 | 403 | 1003 |
| 路由不存在 | 404 | — |
| 未捕获异常 | 500 | 5000 |

> 约定：一旦进入业务层，一律返回 HTTP 200，业务结果看 `code`。认证与权限在中间件层拦截，使用标准 HTTP 语义。前端 `api.js` 统一处理这两类分支。

**业务错误码**

| code | 含义 |
|---|---|
| 0 | 成功 |
| 1000 | 参数校验失败 |
| 1001 | 未登录 / token 无效或过期 |
| 1002 | 用户名或密码错误 |
| 1003 | 权限不足 |
| 1004 | 资源不存在 |
| 1005 | 资源已存在（如用户名重复） |
| 2001 | 题目已删除或不可用 |
| 2002 | 判题队列已满，请稍后重试 |
| 2003 | 代码长度超限 |
| 2004 | 测试点数量超过上限 |
| 2005 | 提交记录不存在 |
| 5000 | 服务器内部错误 |

**鉴权头**
```
Authorization: Bearer <jwt>
```

**分页参数**：`?page=1&size=20`（`size` 默认 20，最大 100），响应 `data` 内含 `{ total, page, size, items }`。

### 7.2 端点清单

#### 认证

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| POST | `/api/auth/register` | — | 注册，成功后直接返回 token |
| POST | `/api/auth/login` | — | 登录，返回 `{ token, user }` |
| GET | `/api/auth/me` | 用户 | 返回当前用户信息 |

#### 题目

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| GET | `/api/problems` | 可选 | 列表；参数 `page,size,difficulty,tag,status`；含通过率与我的状态 |
| GET | `/api/problems/{id}` | 可选 | 详情；**仅返回题面与样例，绝不返回隐藏用例** |
| GET | `/api/tags` | — | 全部标签 |

#### 判题

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| POST | `/api/judge/run` | 用户 | 运行样例；body `{problem_id, code, stdin?}`；返回 `{ run_id }` |
| GET | `/api/judge/run/{run_id}` | 用户 | 轮询运行结果；**结果存内存，TTL 10 分钟** |
| POST | `/api/judge/submit` | 用户 | 提交；body `{problem_id, code}`；返回 `{ submission_id }` |
| GET | `/api/submissions/{id}` | 用户 | 轮询提交状态与结果 |

#### 提交记录

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| GET | `/api/submissions` | 用户 | 我的提交；参数 `page,size,problem_id,status` |

#### 排行榜

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| GET | `/api/leaderboard` | — | 参数 `page,size` |

#### 管理端

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| POST | `/api/admin/problems` | 管理员 | 新增题目 |
| PUT | `/api/admin/problems/{id}` | 管理员 | 编辑题目 |
| DELETE | `/api/admin/problems/{id}` | 管理员 | **软删除** |
| GET | `/api/admin/problems` | 管理员 | 含已删除题目 |
| POST | `/api/admin/problems/{id}/testcases` | 管理员 | multipart 上传 `.in`/`.out` 配对 |
| GET | `/api/admin/problems/{id}/testcases` | 管理员 | 查看测试点清单（不含内容） |
| DELETE | `/api/admin/problems/{id}/testcases/{idx}` | 管理员 | 删除单个测试点 |

#### 系统

| 方法 | 路径 | 鉴权 | 说明 |
|---|---|---|---|
| GET | `/api/health` | — | 返回服务状态与队列长度 |

### 7.3 关键响应示例

**提交后轮询（判题中）**
```json
{ "code": 0, "message": "ok", "data": {
    "id": 1024, "status": "JUDGING", "problem_id": 7,
    "time_ms": null, "memory_kb": null
} }
```

**提交后轮询（WA）**
```json
{ "code": 0, "message": "ok", "data": {
    "id": 1024, "status": "WA", "problem_id": 7,
    "failed_case_idx": 3, "total_cases": 10,
    "time_ms": 12, "memory_kb": 3456
} }
```

**运行样例结果**
```json
{ "code": 0, "message": "ok", "data": {
    "run_id": "run_8f3a...", "status": "DONE",
    "cases": [
      { "index": 1, "verdict": "AC", "stdin": "1 2", "expected": "3",
        "actual": "3", "time_ms": 3, "memory_kb": 2048 },
      { "index": 2, "verdict": "WA", "stdin": "10 20", "expected": "30",
        "actual": "300", "time_ms": 3, "memory_kb": 2048 }
    ]
} }
```

---

## 8. 判题子系统

### 8.1 判题状态机

```
                     ┌──────────────────────────────────────────┐
                     │                                          │
  提交 / 运行 ──> PENDING ──(线程池取任务)──> JUDGING ──┬──> CE │
                     │                          │      ├──> AC │
                     │                          │      ├──> WA │
                     │                          │      ├──> TLE│
                     │                          │      ├──> MLE│   终态
                     │                          │      ├──> OLE│
                     │                          │      ├──> RE │
                     │                          │      └──> SE │
                     │                          │              │
                     └──(启动扫描)───────────────┴──(看门狗)─────┘
                              全部置为 SE            超时置为 SE
```

**状态定义**

| 状态 | 含义 | 终态 |
|---|---|---|
| `PENDING` | 已入队，等待判题线程 | 否 |
| `JUDGING` | 正在编译或执行 | 否 |
| `AC` | 全部测试点通过 | 是 |
| `WA` | 首个答案不符的测试点 | 是 |
| `TLE` | 超出时间限制 | 是 |
| `MLE` | 超出内存限制 | 是 |
| `OLE` | 单测试点输出超出上限 | 是 |
| `RE` | 非零退出码 / 异常终止 | 是 |
| `CE` | 编译失败 | 是 |
| `SE` | 系统错误（崩溃、看门狗、内部异常） | 是 |

### 8.2 判题流程

```
1. 收到提交
   ├─ 校验：题目存在且未删除、代码长度 ≤ 上限
   ├─ SUBMIT：写 submissions 行（status=PENDING）→ 返回 submission_id
   └─ RUN   ：生成 run_id，结果占位写入内存 registry

2. 投递任务到内存队列
   └─ 队列已满 → 返回业务错误 2002（SUBMIT 时需回滚/标记该行）

3. 判题线程取出任务 → status = JUDGING

4. 编译阶段（同样受限执行）
   ├─ 写 code.cpp 到临时目录
   ├─ 受限子进程执行 g++ -O2 -std=c++17 -pipe -o a.out code.cpp
   │   （rlimit CPU 10s、NPROC、降权 nobody；不设 AS 上限或设宽松值）
   ├─ 编译失败 → status = CE，compile_msg 截断至 4KB
   └─ 编译成功 → 继续

5. 执行阶段：对每个测试点
   ├─ 受限子进程执行 ./a.out，stdin 重定向自 {idx}.in
   ├─ 输出写入临时文件，超过输出上限立即 kill → OLE
   ├─ wait4 取 rusage：ru_maxrss → memory_kb
   ├─ 判定顺序（严格按此优先级）：
   │     1) 被信号杀死且非超限 → RE
   │     2) CPU 时间 > time_limit 或 wall > 2×time_limit → TLE
   │     3) ru_maxrss > memory_limit 或 见 §8.4 → MLE
   │     4) 输出超上限 → OLE
   │     5) 退出码非 0 → RE
   │     6) 输出（去除行尾空白后）与 .out 不符 → WA
   │     7) 否则 → 通过该测试点
   ├─ 遇首个失败 → 停止后续测试点（不做全量跑）
   └─ 全部通过 → AC

6. 回写结果
   ├─ SUBMIT → 更新 submissions 行（status/time_ms/memory_kb/
   │            failed_case_idx/total_cases/finished_at）
   └─ RUN    → 更新内存 registry

7. 清理临时目录（无论成败，用 RAII 保证）
```

### 8.3 沙箱设计

| 措施 | 实现 | 防范目标 |
|---|---|---|
| CPU 时间限制 | `setrlimit(RLIMIT_CPU, time_limit)` | 死循环吃满 CPU |
| 墙钟兜底 | 父进程计时，超 `2 × time_limit` 强杀 | `sleep` 类挂起绕过 CPU 限制 |
| 地址空间限制 | `setrlimit(RLIMIT_AS, memory_limit)` | 吃爆内存拖垮整机 |
| 进程数限制 | `setrlimit(RLIMIT_NPROC, ...)` | fork 炸弹 |
| 文件大小限制 | `setrlimit(RLIMIT_FSIZE, 输出上限)` | 写满磁盘 |
| 降权 | `setgid/setuid(nobody)` | 删除或篡改项目文件 |
| 目录隔离 | `chdir` 到 `mkdtemp` 创建的临时目录 | 访问其他题目数据或数据库凭据 |
| 输出上限 | 父进程读取输出时计数，超限 kill | 死循环打印打爆磁盘 |
| 环境净化 | 清空环境变量后 exec | 泄露配置与密钥 |

> **未包含的加固**：chroot、seccomp、namespace、cgroup。理由见 §13。

### 8.4 内存判定细节

直接用 `RLIMIT_AS` 做判定会导致一个经典误报：内存超限时 `malloc` 返回 NULL，程序往往直接段错误（SIGSEGV），于是本该是 MLE 的提交被报成 RE。

**本项目的处理方式（两层各司其职）**：

- `RLIMIT_AS` **只作为硬性兜底**，防止程序真的吃爆机器
- **MLE 的判定依据取自 `wait4` 返回的 `ru_maxrss`**（进程峰值 RSS），超过题目限制即判 MLE

设置 `RLIMIT_AS` 时预留约 16MB 余量给动态链接器等运行时开销，避免正常程序在限额边界被误杀。

### 8.5 Run 与 Submit 的差异

| 维度 | RUN | SUBMIT |
|---|---|---|
| 测试数据来源 | 题面样例（公开，最多 5 个）；可接受自定义 stdin | `data/problems/{id}/` 下的全部隐藏用例 |
| 结果存放 | **内存 registry**（`run_<random>` 键，TTL 10 分钟） | **MySQL submissions 表** |
| 是否留痕 | 否，用户不可见 | 是，进"我的提交记录" |
| 返回结构 | 逐样例的输入/期望/实际对比 | 汇总状态 + 首个失败位置 |
| 是否影响排行榜 | 否 | 是 |
| 执行路径 | **与 Submit 共用同一队列、同一沙箱、同一线程池** | 同左 |

> **设计说明**：Run 与 Submit 复用同一套队列与沙箱代码，仅结果输出目标（sink）不同。这样沙箱加固不会出现"两边不一致"的漏洞，代价是高峰期 Run 需排在 Submit 队列之后。
>
> Run 结果存内存意味着**服务重启后仍可轮询的 run_id 会失效**，前端需提示"运行结果已过期，请重新运行"。这是刻意选择——Run 本质是瞬时操作，不值得落库。

### 8.6 判题态自愈

**两道防线：**

1. **启动扫描**：服务启动时，将所有 `status IN ('PENDING','JUDGING')` 的记录批量置为 `SE`，`compile_msg` 记为"服务重启，判题中断"。
   - 因为提交的源代码不落库（§13 R1），**无法自动重新入队重判**，用户需重新提交。

2. **运行时看门狗**：判题线程池维护每个在办任务的开始时间。独立巡检周期（1 秒）扫描，超过 `max(2 × time_limit, 5s)` 仍未完成的任务，强制置为 `SE` 并释放其线程槽位。
   - 防止判题逻辑出现 bug 时线程永久挂死。

---

## 9. 前端与 UI/UX

### 9.1 形态选择

**混合形态**：列表、详情、排行榜等以静态 HTML 页面承载；做题页与后台管理页由 JS 重度驱动。无前端构建工具，无框架。

### 9.2 页面清单

| 页面 | 路径 | 形态 | 主要内容 |
|---|---|---|---|
| 登录 / 注册 | `web/login.html` | 静态 + JS | 表单切换、表单校验、登录后写 token |
| 题目列表 | `web/index.html` | 静态 + JS | 表格、难度/标签/状态筛选、分页、通过率列 |
| 题目详情 + 做题 | `web/problem.html?id=N` | JS 重交互 | 左侧 Markdown 题面、右侧 CodeMirror、Run/Submit 按钮、结果面板 |
| 我的提交记录 | `web/submissions.html` | 静态 + JS | 列表、按题目/状态筛选、分页 |
| 提交详情 | `web/submission.html?id=N` | 静态 + JS | 状态、耗时、内存、失败测试点位置、编译错误信息 |
| 排行榜 | `web/leaderboard.html` | 静态 + JS | 名次、用户名、通过题数、达成该题数的时间 |
| 管理后台 | `web/admin.html` | JS 重交互 | 题目列表、新增/编辑表单、测试数据上传 |

### 9.3 做题页交互约定

**布局**
```
┌──────────────────────────────┬──────────────────────────────┐
│  题面区（Markdown 渲染）        │  代码区                       │
│  - 标题 / 难度 / 标签            │  ┌────────────────────────┐   │
│  - 题目描述                     │  │  CodeMirror 6 (C++)     │   │
│  - 输入输出格式                  │  │                        │   │
│  - 样例（输入/输出/说明）         │  └────────────────────────┘   │
│  - 数据范围提示                  │  [ 自定义输入 ]（可折叠）       │
│                                │  [ 运行 ]  [ 提交 ]           │
│                                │  ┌────────────────────────┐   │
│                                │  │  结果面板                │   │
│                                │  │  状态 / 耗时 / 内存       │   │
│                                │  │  逐样例对比（Run）        │   │
│                                │  └────────────────────────┘   │
└──────────────────────────────┴──────────────────────────────┘
```

**交互规则**

| 规则 | 说明 |
|---|---|
| 轮询节奏 | 提交后立即查一次，之后每 800ms 一次；进入终态即停止 |
| 轮询上限 | 连续轮询超过 60 秒无终态，停止并提示"判题超时，请刷新查看" |
| 按钮状态 | 判题中禁用 Run/Submit 按钮，防止重复投递 |
| 页面刷新恢复 | URL 携带 `?submission=1024` 时，页面加载后自动恢复轮询 |
| 语言选择 | 本期仅 C/C++，下拉框保留但只有一项（为未来扩展留位） |

### 9.4 反馈粒度（重要产品约定）

**Submit 失败时的反馈**
- **显示**：状态（WA / TLE / MLE / RE / OLE / CE）、耗时、内存、`第 k 个测试点不通过`、测试点总数
- **不显示**：隐藏测试点的输入数据、期望输出、实际输出

**Run 失败时的反馈**
- 显示完整的逐样例对比（因为用的是公开样例，不存在泄露问题）

**例外**：`CE`（编译错误）必须完整回显编译器的错误信息（截断至 4KB），否则用户无从下手。

> 防作弊理由：若 WA 时回显隐藏用例数据，第一个隐藏用例事实上就变成了公开的，后续必须不断补充新用例，形成成本黑洞。

### 9.5 安全渲染约定

- **题面**：Markdown 原文取自后端 → `marked` 渲染 → **`DOMPurify` 消毒** → 插入 DOM
- **严禁**把用户或管理员输入未经消毒直接 `innerHTML`
- 代码展示区使用 CodeMirror 只读模式或 `textContent`，绝不 `innerHTML`

---

## 10. 安全设计

### 10.1 认证与授权

| 项 | 方案 |
|---|---|
| 密码存储 | PBKDF2-HMAC-SHA256，随机 16 字节 salt，迭代 210,000 次；编码为 `pbkdf2_sha256$<iter>$<b64salt>$<b64hash>` 单字段存储 |
| 密码校验 | 使用**常数时间比较**，防时序攻击 |
| 令牌 | JWT（HS256），payload 含 `uid`、`role`、`exp`；有效期 24 小时 |
| 签名密钥 | 从配置文件读取，**不硬编码进源码**；README 说明须自行生成 |
| 令牌存放 | 前端 `localStorage`，每次请求带 `Authorization: Bearer` |
| 权限校验 | 中间件解析 token → 注入当前用户 → 路由层校验角色 |
| 传输 | HTTPS（cpp-httplib SSLServer + 自签证书） |

**关于令牌存 localStorage 的已知权衡**：XSS 可窃取 token。本项目的缓解措施是题面渲染强制走 DOMPurify 消毒，且无用户可自由写入的 HTML 区域。改用 httpOnly Cookie 会引入 CSRF 防护需求，在无前端框架下成本更高，故本期不采用。

### 10.2 资源滥用防护

| 防护项 | 阈值 | 触发结果 |
|---|---|---|
| 代码长度上限 | 64 KB | 业务错误 2003 |
| 判题队列上限 | 100（可配置） | 业务错误 2002 |
| 编译超时 | 10 秒 | CE，提示"编译超时" |
| 单测试点输出上限 | 16 MB | OLE |
| 单题测试点数量上限 | 100 | 业务错误 2004 |
| 单次 Run 样例数量 | 5 | 超出部分忽略 |

> 这些不是可选的"加固"，而是**功能正确性的一部分**：一句 `while(true) cout << 1;` 若无输出上限，可在几十秒内写满磁盘并拖垮整台服务器上的 MySQL。

### 10.3 输入校验

- 用户名：3–32 字符，仅限字母数字下划线
- 密码：8–72 字符
- 所有 ID 类参数：必须为正整数，否则返回 1000
- 分页 `size` 上限 100，防止一次拉全表
- 上传的测试数据文件名必须为 `<数字>.in` / `<数字>.out`，防路径穿越（拒绝含 `/`、`..` 的文件名）

### 10.4 已知的安全边界（有意不做）

| 项 | 说明 |
|---|---|
| 沙箱逃逸 | 无 chroot/seccomp/namespace，理论上恶意代码可逃逸。适用于**可信用户**场景（同学、面试演示） |
| 提交频率限流 | 未做；由队列上限兜底 |
| SQL 注入 | 通过预编译语句（`mysql_stmt_*`）防护；**严禁字符串拼接 SQL** |
| CSRF | 使用 Bearer Token 而非 Cookie，天然不受 CSRF 影响 |
| 暴力破解登录 | 未做账号锁定与验证码 |

---

## 11. 非功能需求

### 11.1 性能指标

| 指标 | 目标值 | 说明 |
|---|---|---|
| 峰值同时提交 | 5–20 | 设计目标，非硬性 SLA |
| 判题线程数 | 4（可配置） | 建议 `min(4, CPU核数-1)` |
| 单次提交端到端耗时 | 通常 < 3s | 含编译（约 1–2s）+ 执行 |
| 列表页响应 | < 300ms | 题量 < 500 时 |
| 判题吞吐 | ≥ 15 提交/分钟 | 4 线程、平均单次判题 15s 的估算下限 |

### 11.2 可扩展性预留

即便本期不做，以下扩展点应预留：

- **多语言**：语言配置（源文件名、编译命令、时间/内存系数）抽为可配置结构，新增语言不改判题核心
- **独立判题进程**：判题队列抽象为接口，未来可替换为 MySQL 表队列或 Redis
- **题目数量增长**：列表查询已建索引；若题量过万，需引入通过率预聚合表

### 11.3 配置项

外部配置文件（`config/config.ini` 或 `config/config.json`），至少包含：

```ini
[server]
port = 8443
static_root = web
cert_file = certs/server.crt
key_file = certs/server.key

[mysql]
host = 127.0.0.1
port = 3306
user = oj
password =
database = oj
pool_size = 8

[auth]
jwt_secret =
jwt_expire_hours = 24
pbkdf2_iterations = 210000

[judge]
threads = 4
queue_capacity = 100
compile_timeout_ms = 10000
max_code_bytes = 65536
max_output_bytes = 16777216
max_cases_per_problem = 100
run_result_ttl_seconds = 600

[paths]
data_root = data/problems
build_root = /tmp/oj_build
sandbox_uid = nobody
```

### 11.4 日志

- 分级：`DEBUG / INFO / WARN / ERROR`
- 必须记录：请求行（方法 + 路径 + 状态码 + 耗时）、判题任务开始/结束与结果、所有异常
- **禁止记录**：密码、JWT 原文、源代码内容
- 输出到 stdout（配合 `nohup` 重定向到文件）

---

## 12. 边缘案例与异常处理

### 12.1 判题相关

| 场景 | 处理 |
|---|---|
| 编译产物损坏 / `a.out` 未生成 | 报 CE |
| 测试数据缺失（`.in` 无配对 `.out`） | 该题判题前置校验失败，返回 SE 并记 ERROR 日志 |
| 程序无输出但与期望都是空 | 判 AC（去除行尾空白后比较） |
| 程序输出超长但前 16MB 已正确 | 判 OLE（先于 WA 判定） |
| 程序在样例上 AC 但隐藏用例 TLE | 正常判 TLE，记录首个失败位置 |
| 提交时题目已被软删除 | 返回 2001，不消费队列容量 |
| 判题中服务崩溃 | 启动扫描置 SE（§8.6） |
| 判题线程挂死 | 看门狗置 SE 并释放槽位（§8.6） |
| 临时目录残留 | 每次判题用 RAII 清理；启动时清理 `build_root` 下全部残留 |

### 12.2 用户流程相关

| 场景 | 处理 |
|---|---|
| 用户名已存在 | 返回 1005 |
| 密码错误 | 返回 1002（**不区分"用户不存在"与"密码错误"**，防用户枚举） |
| token 过期 | 返回 401 + 1001，前端清除本地 token 并跳登录页 |
| 非管理员访问管理接口 | 返回 403 + 1003 |
| 判题排队中用户刷新页面 | URL 带 `?submission=id`，加载后自动恢复轮询 |
| 判题排队中用户关闭页面 | 判题继续执行，结果入库；下次进记录页可见 |
| 连点提交 | 前端禁用按钮 + 后端队列上限兜底 |
| 访问不存在的题目 ID | 返回 1004 |
| 访问已软删除的题目 | 普通用户返回 1004；管理员可见 |

### 12.3 数据一致性相关

| 场景 | 处理 |
|---|---|
| 管理员软删除题目 | `is_deleted=1`；用户侧列表/详情不可见、不可提交；已有提交记录与排行榜统计**保留并正常展示题号与标题** |
| 管理员上传测试数据覆盖 | 先写临时目录，全量校验通过后再原子替换，避免出现半套数据 |
| 提交入队失败（队列满） | 若已写入 submissions 行，需回滚删除或直接标记为 SE |
| 删除题目时测试数据文件 | 软删除**不删除文件**（题目可恢复）；仅在管理员显式清理时删除 |

---

## 13. 已知风险与权衡

> 本节记录本项目**有意接受**的代价。每一条都是决策，不是疏漏。

### R1 · 提交源代码不落库

**内容**：`submissions` 表只存元数据（用户、题目、状态、耗时、内存、失败位置），**不存源代码**。

**带来什么**
- 提交详情页**无法显示用户当时写的代码**（LeetCode 有这个功能）
- 崩溃后**无法自动重判**，只能标记为 SE（§8.6）

**为什么还这么做**：缩小存储与隐私面，且源代码是非常大的字段（每份几十 KB，高频提交下膨胀快）。

**如果后悔了怎么改**：`submissions` 加一个 `code MEDIUMTEXT` 字段即可，改动局限在 DAO 与提交详情页。

---

### R2 · 判题与 HTTP 共享同一进程的 CPU

**内容**：4 个判题线程与 HTTP 线程同进程竞争 CPU。

**带来什么**：判题满载时，HTTP 响应延迟上升；极端情况下前端轮询会变慢。

**为什么接受**：同时提交 5–20 的规模下，4 个判题线程不会长期打满；且单进程模型免去了进程间通信与任务状态同步的全部复杂度。

**缓解**：判题线程数可配置；若将来成为瓶颈，把 §5.1 中的判题线程池替换为独立进程即可（队列已抽象）。

---

### R3 · 未做提交频率限流

**内容**：用户可以连续快速提交，仅受队列上限（100）约束。

**带来什么**：恶意或误操作可快速占满队列，导致其他用户收到 2002。

**为什么接受**：作品集场景用户可信；队列上限已阻止了更严重的资源耗尽。

**代价的边界**：若未来公开访问，这是**第一个必须补的缺口**。

---

### R4 · 沙箱隔离等级为"防范"而非"对抗"

**内容**：无 chroot / seccomp / namespace / cgroup，仅有 rlimit + 降权 + 目录隔离。

**带来什么**：理论上恶意代码仍可逃逸（如利用内核漏洞、或降权后仍可读全局可读文件）。

**为什么接受**：达到"防误伤"（防删文件、防吃爆资源）的目标，同时保持零额外依赖与部署简单。这是与 Docker/nsjail 方案的核心权衡点，应在面试中主动说明选择理由。

---

### R5 · 自签证书 + 手动部署抬高复现门槛

**内容**：无域名、无 Let's Encrypt、无进程守护，靠 `nohup` 运行。

**带来什么**：面试官访问时会遇到证书告警；服务重启需手动拉起。

**为什么接受**：零成本、零外部依赖。

**缓解**：README 必须是"逐条复制粘贴可执行"的级别，包含证书生成命令与告警绕过说明。**因为 S2（陌生人可跑通）是验收标准**，这一项的风险由 README 质量承担。

---

### R6 · 通过率聚合查询的成本

**内容**：题目列表页每行都要算通过率，天然是 `GROUP BY` 聚合。

**风险**：容易写成 N+1 查询（每题一次聚合）。

**处理**：必须一次 `GROUP BY problem_id` 批量取回，与题目列表按页 JOIN 或二次查询后在内存合并。题量超过 1 万时需改为预聚合表。

---

## 14. 项目目录结构

```
vibecoding_oj/
├── Makefile                     # 构建入口：all / clean / test
├── README.md                    # 部署与使用说明（S2 验收标准承载者）
├── SPEC.md                      # 本文档
├── config/
│   └── config.ini               # 配置文件（jwt_secret 等敏感项不入库）
├── certs/                       # 自签证书（.gitignore 排除）
├── sql/
│   ├── schema.sql               # 建表语句
│   └── seed.sql                 # 初始管理员账号 + 示例题目
├── data/
│   └── problems/
│       └── {problem_id}/
│           ├── 1.in
│           ├── 1.out
│           └── ...
├── src/
│   ├── main.cpp                 # 启动、加载配置、装配依赖
│   ├── config.{h,cpp}
│   ├── common/
│   │   ├── status.h             # 判题状态枚举与字符串互转
│   │   ├── error_code.h         # 业务错误码
│   │   └── types.h
│   ├── http/
│   │   ├── server.{h,cpp}       # SSLServer 封装
│   │   ├── router.{h,cpp}       # 路由注册
│   │   ├── middleware.{h,cpp}   # JWT 鉴权、角色校验
│   │   └── response.{h,cpp}     # 统一响应体构造
│   ├── service/
│   │   ├── auth_service.{h,cpp}
│   │   ├── problem_service.{h,cpp}
│   │   ├── submission_service.{h,cpp}
│   │   └── leaderboard_service.{h,cpp}
│   ├── dao/
│   │   ├── db.{h,cpp}           # 连接与断开、连接池
│   │   ├── user_dao.{h,cpp}
│   │   ├── problem_dao.{h,cpp}
│   │   └── submission_dao.{h,cpp}
│   ├── judge/
│   │   ├── task_queue.{h,cpp}   # 有界阻塞队列
│   │   ├── judge_pool.{h,cpp}   # 线程池 + 看门狗
│   │   ├── sandbox.{h,cpp}      # fork / rlimit / 降权 / exec / wait4
│   │   ├── runner.{h,cpp}       # 编译 + 逐用例判定
│   │   └── run_registry.{h,cpp} # Run 结果内存表（带 TTL）
│   ├── crypto/
│   │   ├── password.{h,cpp}     # PBKDF2 编码与校验
│   │   ├── jwt.{h,cpp}          # HS256 签发与验证
│   │   └── base64.{h,cpp}
│   └── util/
│       ├── json.h               # nlohmann/json（vendored 单头）
│       ├── log.{h,cpp}
│       ├── fs.{h,cpp}           # 临时目录 RAII、文件读写
│       └── str.{h,cpp}
├── web/
│   ├── login.html
│   ├── index.html
│   ├── problem.html
│   ├── submissions.html
│   ├── submission.html
│   ├── leaderboard.html
│   ├── admin.html
│   └── assets/
│       ├── css/
│       │   └── style.css
│       └── js/
│           ├── api.js           # fetch 封装 + 统一响应体解包 + 401 处理
│           ├── md.js            # marked + DOMPurify 封装
│           ├── login.js
│           ├── list.js
│           ├── problem.js       # CodeMirror + 轮询
│           ├── submissions.js
│           ├── leaderboard.js
│           └── admin.js
├── tests/
│   ├── unit/                    # 单元测试
│   └── e2e/
│       └── run_all.sh           # 端到端验收脚本
└── tools/
    ├── gen_cert.sh              # 生成自签证书
    └── import_problem.sh        # 脚本化导入题目与测试数据
```

---

## 15. TODO 清单

> **排期原则**：判题核心（M2）是全项目风险最高的部分，**提前做**。先把最可能失败的部分变成确定能成功的部分，再铺前端界面。

### M0 · 环境与骨架

- [ ] 写 `Makefile`：`all` / `clean` / `test` 目标，链接 `-lmysqlclient -lssl -lcrypto -lpthread`
- [ ] 引入 `nlohmann/json` 单头文件至 `src/util/json.h`
- [ ] 实现 `config.{h,cpp}`：读取 `config/config.ini`
- [ ] 实现 `util/log.{h,cpp}`：分级日志
- [ ] 搭起 `main.cpp`：启动一个最小 SSLServer，`/api/health` 返回 `{"code":0}` 跑通
- [ ] `tools/gen_cert.sh`：生成自签证书，确认浏览器可访问（含告警绕过说明）

### M1 · 数据层与认证

- [ ] 写 `sql/schema.sql` 并导入 MySQL
- [ ] 实现 `dao/db.{h,cpp}`：连接封装 + 每线程独立连接
- [ ] 实现 `crypto/base64`、`crypto/password`（PBKDF2 编码/校验）、`crypto/jwt`（HS256 签发/验证）
- [ ] 实现 `user_dao`：按用户名查、插入用户
- [ ] 实现 `auth_service`：注册、登录、用户信息
- [ ] 实现 `http/response`：统一响应体构造
- [ ] 实现 `http/middleware`：JWT 解析、注入当前用户、角色校验
- [ ] 路由：`/api/auth/register`、`/api/auth/login`、`/api/auth/me`
- [ ] **验证**：curl 完成注册 → 登录 → 带 token 访问 `/api/auth/me`

### M2 · 判题核心（最高风险，优先攻坚）

- [ ] 实现 `judge/sandbox`：`fork` + `setrlimit`（CPU/AS/NPROC/FSIZE）+ `setgid/setuid`(nobody) + `chdir`(临时目录) + `exec` + `wait4` 取 `rusage`
- [ ] **验证沙箱**：手写以下测试程序并确认能被正确拦截
  - [ ] 死循环 → TLE
  - [ ] `while(true) cout << 1;` → OLE（且磁盘未被写满）
  - [ ] 申请 1GB 内存 → MLE（**且不被误报为 RE**）
  - [ ] `system("rm -rf ~")` → 无法删除项目文件
  - [ ] `abort()` / 段错误 → RE
  - [ ] 编译错误 → CE 且错误信息完整回显
- [ ] 实现 `runner`：编译阶段（受限子进程跑 g++）+ 逐用例执行 + 判定优先级（§8.2 第 5 步顺序）
- [ ] 实现 `task_queue`：有界阻塞队列
- [ ] 实现 `judge_pool`：4 线程消费 + 运行时看门狗
- [ ] 实现 `run_registry`：Run 结果内存表 + TTL 清理
- [ ] 实现「启动扫描」：把非终态提交批量置为 SE
- [ ] **验证**：写三个独立程序（必 AC / 必 WA / 必 TLE）跑通全链路

### M3 · 判题服务与 API

- [ ] 实现 `problem_dao`：列表（含筛选/分页/通过率批量聚合）、详情、增删改、标签关联、软删除
- [ ] 实现 `submission_dao`：插入、按 id 查、按用户分页查、批量状态更新
- [ ] 实现 `submission_service`：提交编排、入队、结果回写
- [ ] 实现 `leaderboard_service`：排行榜口径（§6.3）
- [ ] 测试数据存储层：`data/problems/{id}/` 的读写与配对校验
- [ ] 路由：`/api/judge/run`、`/api/judge/run/{id}`、`/api/judge/submit`、`/api/submissions/{id}`
- [ ] 路由：`/api/problems`、`/api/problems/{id}`、`/api/tags`
- [ ] 路由：`/api/submissions`、`/api/leaderboard`
- [ ] 路由：`/api/admin/problems` 系列（含 multipart 测试数据上传，含文件名路径穿越校验）
- [ ] 更新 `/api/health`：返回队列长度与判题线程忙碌数

### M4 · 前端主线

- [ ] `assets/js/api.js`：fetch 封装、统一响应体解包、401 自动跳登录
- [ ] `login.html` + `login.js`：注册/登录切换、表单校验
- [ ] `index.html` + `list.js`：题目表格、筛选器、分页、通过率列
- [ ] `problem.html` + `problem.js`：Markdown 渲染（marked + DOMPurify）、CodeMirror 接入、Run/Submit 按钮、结果面板、轮询逻辑（800ms / 60s 上限 / 刷新恢复）
- [ ] `submissions.html` + `submission.html`：我的提交列表与详情
- [ ] `leaderboard.html`：排行榜
- [ ] `assets/css/style.css`：全局样式，保证做题页不破版

### M5 · 管理后台

- [ ] `admin.html` + `admin.js`：题目列表（含已删除）、新增/编辑表单
- [ ] 标签的创建与绑定
- [ ] 测试数据上传界面与配对结果回显
- [ ] 删除确认与软删除语义

### M6 · 加固、测试与文档

- [ ] 补齐资源防护阈值（代码长度、队列上限、输出上限、测试点数量上限）
- [ ] 实现 `tests/e2e/run_all.sh`：注册 → 登录 → 提交错解看 WA → 提交正解看 AC → 查记录 → 查排行榜
- [ ] 补齐关键单元测试：PBKDF2 编码/校验、JWT 签发/验证、判定优先级、路径穿越校验
- [ ] 启动时清理 `build_root` 下的残留临时目录
- [ ] 写 `README.md`：环境依赖、证书生成、数据库初始化、配置说明、启动命令、**常见问题**
- [ ] 按 §16.2 在干净机器上完整走一遍

### M7 · 加分项（主线完成后）

- [ ] 为多语言预留的配置结构落地（不改判题核心）
- [ ] 压测：20 并发提交下的响应延迟与判题吞吐数据
- [ ] 判题状态对照表：每种状态一个可复现的测试程序

---

## 16. 验收标准

### 16.1 演示脚本

以下 7 步必须**连续**完成，无卡顿、无异常：

| # | 步骤 | 期望结果 |
|---|---|---|
| 1 | 访问站点，注册新账号 | 注册成功并自动登录，跳转题目列表 |
| 2 | 在题目列表按「简单」+ 某标签筛选 | 列表正确过滤，通过率列有值 |
| 3 | 打开某题，点「运行」 | 数秒内返回逐样例对比结果 |
| 4 | 提交一个错误解法 | 得到 WA，显示"第 k 个测试点不通过"与耗时内存 |
| 5 | 提交正确解法 | 得到 AC，耗时与内存合理 |
| 6 | 查看排行榜 | 自己的账号出现在榜单，通过题数 +1 |
| 7 | 用管理员账号登录后台，新增一道题并上传测试数据 | 保存后前台立即可见，且能正常提交判题 |

### 16.2 陌生人可跑通（S2）

在**一台干净的 Linux 机器**上，只按 README 操作：

- [ ] 所有 `apt install` 依赖一次性装齐，无遗漏
- [ ] 数据库建表脚本可执行，无语法错误
- [ ] 证书生成脚本可执行，生成的证书能被浏览器加载
- [ ] 配置文件有可用的示例值，且明确标注哪几项必须自行修改（尤其是 `jwt_secret`）
- [ ] `make` 一次编译通过，无警告阻塞
- [ ] 启动命令可执行，服务正常监听
- [ ] 初始管理员账号可登录（`seed.sql` 提供）
- [ ] 能完成一次完整的提交并得到 AC

### 16.3 判题正确性

每种状态都必须有一个**能稳定复现**的测试程序：

- [ ] `AC` — 正确解法
- [ ] `WA` — 输出错误结果
- [ ] `TLE` — 死循环
- [ ] `MLE` — 分配超限内存（**且不被误报为 RE**）
- [ ] `OLE` — 无限打印（**且磁盘未被写满**）
- [ ] `RE` — 主动 `abort()` 或除零
- [ ] `CE` — 语法错误，编译信息完整回显
- [ ] `SE` — 人为制造（如删除测试数据文件后提交）

### 16.4 安全边界

- [ ] 未带 token 访问 `/api/submissions` → HTTP 401
- [ ] 普通用户访问 `/api/admin/problems` → HTTP 403
- [ ] 普通用户访问他人提交 `/api/submissions/{他人id}` → 拒绝
- [ ] `/api/problems/{id}` 响应中**不含**隐藏测试数据
- [ ] 提交超长代码 → 业务错误 2003
- [ ] 测试数据上传含 `../` 的文件名 → 被拒绝
- [ ] 沙箱内执行 `system("rm -rf ~")` → 项目文件安然无恙
- [ ] 数据库查询全部使用预编译语句（代码走查项）

### 16.5 一致性自愈

- [ ] 提交一批任务后立即 `kill` 服务 → 重启后这些提交状态为 `SE`，无永久 `JUDGING`
- [ ] 人为让判题线程挂死 → 看门狗在阈值内将其置为 `SE` 并释放槽位
- [ ] 服务重启后 `build_root` 下无残留临时目录

---

## 17. 附录：关键决策记录

| # | 决策 | 选择 | 核心权衡 |
|---|---|---|---|
| D1 | 判题沙箱方案 | `fork` + `rlimit` + 降权 + 临时目录 | 零依赖、部署简单 ↔ 隔离强度低于 Docker/nsjail |
| D2 | 判题结果返回方式 | 异步 + 前端轮询（800ms） | HTTP 线程秒级释放 ↔ 需多一层状态机与轮询逻辑 |
| D3 | 判题队列位置 | 进程内线程池 + 内存队列 | 零部署成本 ↔ 重启丢队列、与 HTTP 抢 CPU |
| D4 | 提交源代码 | **不落库** | 存储与隐私面小 ↔ 无法自动重判、详情页看不到代码 |
| D5 | 数据持久化 | MySQL 存用户/题目/提交元数据 | 关系模型契合、面试认知度高 ↔ 多一个部署依赖 |
| D6 | 测试数据存储 | 落盘 `data/problems/{id}/` | 大文件不入库 ↔ 需自行保证文件与数据库一致 |
| D7 | 题目删除 | 软删除（`is_deleted`） | 历史提交与排行榜不受影响 ↔ 需在所有查询中过滤标记 |
| D8 | 前端形态 | 混合：静态页 + JS 重交互 | 保留"原生"的轻 ↔ 需自行管理系统性状态 |
| D9 | 代码编辑器 | CodeMirror 6（ESM CDN） | 体积小、契合无构建工具 ↔ 功能弱于 Monaco |
| D10 | 题面格式 | Markdown 存库 + 前端渲染 + DOMPurify | 排版好、有 XSS 防护 ↔ 引入两个前端依赖 |
| D11 | MLE 判定 | `RLIMIT_AS` 兜底 + `ru_maxrss` 判报告 | 判定准确 ↔ 需理解两者语义差异 |
| D12 | 反馈粒度 | 样例级对比 + 「第 k 个测试点不通过」 | 防作弊、不泄露用例 ↔ 用户调试信息偏少 |
| D13 | 传输加密 | cpp-httplib 原生 SSL + 自签证书 | 零额外中间件 ↔ 浏览器告警 |
| D14 | 排行榜口径 | AC 题数降序 → 并列比「达成该题数的时间」 | 与 LeetCode 心智一致 ↔ 需聚合查询；排序键必须是 MAX 而非 MIN，否则退化为"注册时间榜" |
| D15 | 构建方式 | 手写 Makefile + apt 依赖 | 工具链透明 ↔ 依赖版本不锁定 |

---

*本文档由九轮需求访谈收敛而成。所有"不做"与"代价"均为显式决策，而非遗漏。*
