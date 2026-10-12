# dependence.md — 依赖清单与安装指南

> 版本：v1.0
> 日期：2026-10-12
> 依据：[SPEC.md](SPEC.md) v1.0（§4 技术栈、§4.1 系统依赖清单、§4.2 编译要求、§8.3 沙箱、§11.3 配置项）
> 实测环境：Ubuntu 22.04.1 LTS（x86_64, WSL2）/ g++ 11.4.0 / MySQL 8.4.11 / OpenSSL 3.0.2
>
> **文档边界**：本文只负责"装什么、怎么装、怎么验证装了"。证书生成、建库导表、配置文件填写、编译启动属于部署流程，见 [README.md](README.md)。

---

## 0. 依赖全景

按"从哪来"分六类。**A/B/D 三类缺任何一项，服务都跑不起来**，C 类决定做题页能否渲染。

| 类 | 依赖 | 数量 | 获取方式 | 是否提交入库 |
|---|---|---|---|---|
| **A** | 系统包 | 4 | `apt` | 否（机器级） |
| **B** | C++ 头文件库 | 2 | `curl` 下载上游单头文件 | **是**（vendored 进仓库） |
| **C** | 前端库 | 3 | 浏览器运行时 CDN | 否（不下载） |
| **D** | 运行时前提 | 4 | 系统既有 / 手工准备 | 否 |
| **E** | 辅助工具 | 2 (+1 可选) | 系统自带 | 否 |
| **F** | 明确不引入 | — | — | — |

---

## 1. A 类：系统依赖（apt）

### 1.1 安装命令

```bash
sudo apt update
sudo apt install -y build-essential libmysqlclient-dev libssl-dev mysql-server
```

### 1.2 逐项说明

| 包 | 用途 | 是构建期还需运行期 | SPEC 溯源 |
|---|---|---|---|
| `build-essential` | 提供 `g++` 与 `make` | **两者都要**，见下方注 | §4.1、§4.2、§8.2 |
| `libmysqlclient-dev` | MySQL C API（`mysql/mysql.h`），DAO 层与 `mysql_stmt_*` 预编译语句 | 构建期（`-lmysqlclient`） | §4.1、§10.4 |
| `libssl-dev` | PBKDF2-HMAC-SHA256 密码哈希、HMAC-SHA256 签 JWT、TLS | 构建期（`-lssl -lcrypto`） | §4.1、§10.1 |
| `mysql-server` | 数据库服务本体 | **运行期**（服务启动即连库） | §4.1、§11.3 |

> ⚠️ **`build-essential` 是运行期依赖，不只是构建期。**
> SPEC §8.2 第 4 步要求判题时由降权后的子进程执行 `g++ -O2 -std=c++17 -pipe -o a.out code.cpp`。
> 也就是说：**服务编译好之后把 g++ 卸了，判题会全量 CE**。这是本项目最容易被误判为"构建工具"的一项。

> ⚠️ 用 `libmysqlclient-dev`，**不要用 `default-libmysqlclient-dev`**。
> 后者在多版本共存的机器上指向的 `mysql_config` 可能与运行中的 server 版本不一致，链接期报 `undefined reference`。

### 1.3 验证

```bash
g++ --version | head -1        # 期望：g++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
make --version | head -1
mysql_config --version         # 期望：8.4.11
mysql_config --libs            # 期望：含 -lmysqlclient
openssl version                # 期望：OpenSSL 3.0.2 15 Mar 2022
ls /usr/include/mysql/mysql.h /usr/include/openssl/evp.h   # 两个文件都必须存在
sudo service mysql status      # 期望：Active: active (running)
```

**链接参数自检**（一次性确认头文件与库都齐，用的是 SPEC §4.2 的原始参数）：

```bash
g++ -std=c++17 -x c++ - -o /tmp/oj_depcheck \
  -lmysqlclient -lssl -lcrypto -lpthread <<'EOF' && echo "[OK] 依赖完整" && rm -f /tmp/oj_depcheck
#include <mysql/mysql.h>
#include <openssl/evp.h>
int main(){ (void)mysql_init(0); unsigned char o[32];
  PKCS5_PBKDF2_HMAC("p",1,0,0,210000,EVP_sha256(),32,o); return 0; }
EOF
```

---

## 2. B 类：C++ 头文件库（vendored）

两个 header-only 库，**必须取上游单头文件，不能用 apt 包替代**。

### 2.1 为什么不能用 apt（Ubuntu 22.04 实测）

| 库 | apt 包 | 实测结果 |
|---|---|---|
| cpp-httplib | `libcpp-httplib-dev` (0.10.3+ds-1) | ❌ 包内 `httplib.h` 仅 1851 行、**只有声明没有实现体**，实现在 `libcpp-httplib.so` 里，而该 `.so` **未编译 OpenSSL**。SPEC §4.2 要求 `#define CPPHTTPLIB_OPENSSL_SUPPORT` 后以 header-only 方式用 SSLServer，此路线走不通 |
| nlohmann/json | `nlohmann-json3-dev` (3.10.5-2) | ❌ 包内 `json.hpp` 含 24 处 `#include <nlohmann/detail/...>`，是拆开的目录树，**不是单文件**，无法作为单个头文件 vendor 进仓库（SPEC §14 要求落在 `src/util/json.h`） |

### 2.2 安装命令

```bash
mkdir -p src/util

# cpp-httplib —— 必须钉版本，勿用 master，见 §2.4
curl -fsSL -o src/util/httplib.h \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h

# nlohmann/json 单头文件
curl -fsSL -o src/util/json.h \
  https://raw.githubusercontent.com/nlohmann/json/v3.10.5/single_include/nlohmann/json.hpp
```

### 2.3 网络受限时的镜像（本机实测需要）

本机直连 `raw.githubusercontent.com` **不通**，实测报错：

```
curl: (35) OpenSSL SSL_connect: Connection reset by peer in connection to raw.githubusercontent.com:443
http=000
```

加镜像前缀后可用（本机实测 `ghproxy.net` 返回 200）：

```bash
MIRROR=https://ghproxy.net/     # 本机实测可用；gh-proxy.com / ghfast.top 未在本机验证

curl -fsSL -o src/util/httplib.h \
  "${MIRROR}https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h"
curl -fsSL -o src/util/json.h \
  "${MIRROR}https://raw.githubusercontent.com/nlohmann/json/v3.10.5/single_include/nlohmann/json.hpp"
```

> 镜像为第三方代理。**首次获取后按 §2.5 校验内容，确认无误再提交入库**；入库后即为 vendored，后续部署不再依赖网络。

### 2.4 版本必须钉死的理由（实测对比）

| 版本 | `class SSLServer` | multipart 解析类 | 结论 |
|---|---|---|---|
| v0.14.3 / v0.16.3 / **v0.18.3** | ✅ | ✅ `MultipartFormDataParser` | 可用 |
| master（不固定） | ✅ | ❌ 已改名，旧代码不兼容 | **不可用** |

SPEC §7.2 管理端的测试数据上传接口（`POST /api/admin/problems/{id}/testcases`）依赖 `MultipartFormDataParser`。用 master 会导致该接口代码不兼容。

### 2.5 验证（不依赖 GitHub 校验值，靠特征自检）

```bash
wc -l src/util/httplib.h src/util/json.h
# 期望：httplib.h = 10255；json.h = 22091

grep -c 'class SSLServer'              src/util/httplib.h   # 期望：1
grep -c 'class MultipartFormDataParser' src/util/httplib.h   # 期望：1（=0 说明拿错版本了）
grep -c '^#include <nlohmann'          src/util/json.h      # 期望：0（非 0 说明不是自包含单头）
grep -m1 NLOHMANN_JSON_VERSION_MINOR   src/util/json.h      # 期望：3.10.5
```

### 2.6 使用前提（构建配置，非安装项）

- 编译时必须定义 `CPPHTTPLIB_OPENSSL_SUPPORT`，否则 `SSLServer` 不存在（SPEC §4.2）
- **httplib.h 自身需要 `-lpthread`**；若用了压缩、zlib 相关接口还需 `-lz`（本项目不用）
- 两个头文件都**不要**放进 `Makefile` 的 `-I` 之外的地方——SPEC §14 规定它们就在 `src/util/`，与业务代码同目录

---

## 3. C 类：前端依赖（CDN，无需安装）

前端无构建工具（SPEC §9.1），依赖在浏览器运行时以 ESM 方式加载。

| 库 | 用途 | 地址 | SPEC 溯源 |
|---|---|---|---|
| CodeMirror 6 | 做题页 C++ 代码编辑器 | `https://esm.sh/codemirror@6` | §9.2、D9 |
| marked | Markdown 题面渲染 | `https://esm.sh/marked` | §9.5、D10 |
| DOMPurify | 题面 HTML 消毒 | `https://esm.sh/dompurify` | §9.5、D10 |

> ⚠️ **CDN 必须是 `esm.sh`**（备选 `unpkg.com`、`cdnjs.cloudflare.com`）。
> 本机实测 `esm.sh` 返回 200 可用；`cdn.jsdelivr.net` 在部分网络下不可达——若照抄网上常见的 jsdelivr 地址，做题页会**空白且控制台无报错**，排查成本极高。
>
> ⚠️ 这三个依赖走公网 CDN，**完全离线环境下做题页不可用**。SPEC §16.2 的 S2 验收要求"干净机器按 README 跑通"，隐含前提是**该机器能访问 CDN**；若目标是纯内网部署，需自行下载后改为本地托管（属于 SPEC 未覆盖的扩展项）。

---

## 4. D 类：运行时前提（不是安装包，但缺了服务起不来）

| 前提 | 为什么需要 | 检查命令 | SPEC 溯源 |
|---|---|---|---|
| 存在 `nobody` / `nogroup` | 沙箱靠 `setuid(nobody)` 降权 | `id nobody`（期望 uid=65534） | §8.3 |
| **以 root 启动服务** | 只有 root 能 `setuid` 降权；非 root 启动则降权失败，每次提交 SE | 启动时用 `sudo` | §8.3 |
| `build_root` 存在且可被沙箱进程读写执行 | 编译产物 `a.out` 由降权后的 nobody 写入并执行；目录属主为 root 而未放开权限 → **每次提交返回 SE** | 见下 | §11.3 |
| MySQL 服务在运行 | 服务启动即连库 | `sudo service mysql status` | §11.3 |

```bash
# nobody（Ubuntu/Debian 默认存在）
id nobody

# 编译临时目录（路径取自 SPEC §11.3 的 build_root = /tmp/oj_build）
sudo mkdir -p /tmp/oj_build
sudo chmod 1777 /tmp/oj_build      # 或改为 chown nobody:nogroup /tmp/oj_build
ls -ld /tmp/oj_build

# WSL2 系统重启后 MySQL 可能不自启
sudo service mysql start
```

> `1777`（sticky）与 `chown nobody` 两种授权方式各有取舍：前者允许多用户写但依赖 sticky 位保护，后者最小权限但需在沙箱实现里保持属主一致。**最终选哪种在 M2 写 `judge/sandbox` 时定**，见 SPEC §15 M2。

---

## 5. E 类：辅助工具

| 工具 | 用途 | 获取 | 必要性 |
|---|---|---|---|
| `curl` | 验证 API、`tests/e2e/run_all.sh` 端到端脚本 | 系统自带 | 必需（SPEC §15 M1、M6） |
| `git` | clone 仓库（S2 验收的第一步） | 系统自带 | 必需 |
| `jq` | e2e 脚本解析 JSON 响应 | `sudo apt install -y jq` | **可选**，也可用 `grep`/`sed` 替代 |
| `openssl` CLI | `tools/gen_cert.sh` 生成自签证书 | 随 `libssl-dev` 安装 | 必需（SPEC §15 M0） |

---

## 6. F 类：明确不引入的依赖

以下**不是遗漏，是 SPEC 里的显式决策**。任何人提出"顺手加上"都应先回到对应章节讨论。

| 不引入 | 理由 | SPEC 溯源 |
|---|---|---|
| Docker / nsjail / seccomp 库 / cgroup 工具 | 沙箱等级刻意定为"防范"而非"对抗"，只有 `fork` + `rlimit` + 降权 + 目录隔离，以换取零额外依赖与部署简单 | §1.3、§8.3、§13 R4、D1 |
| Web 框架 / 中间件运行时 | cpp-httplib 为 header-only，无路由分组与中间件框架，需自行封装 | §4、D15 |
| MySQL 连接池第三方库 | 由 `dao/db` 自行实现"每线程独立连接" | §5.3、§15 M1 |
| JWT 第三方库（如 jwt-cpp） | 复用 OpenSSL HMAC-SHA256，自行实现 base64url 与编解码 | §4、§10.1 |
| 密码哈希第三方库（如 libsodium） | 用 OpenSSL PBKDF2-HMAC-SHA256 | §4、§10.1 |
| 前端构建工具（Node / npm / Vite / webpack） | 项目硬性前提：原生 HTML/CSS/JS，无构建链 | §4、§9.1、D8 |
| 前端框架（React / Vue） | 同上 | §9.1 |
| Docker Compose / 进程守护（systemd / supervisor） | 裸二进制 + `nohup` 手动后台运行 | §4、§13 R5 |
| Redis / 消息队列 | 判题队列为进程内内存队列，已抽象为接口以便未来替换 | §5.1、§11.2、D3 |
| 邮件服务 / 验证码 SDK | 密码找回、邮箱验证为 Out of Scope | §2.2 |
| CMake | 手写 Makefile | §4、D15 |

---

## 7. 一键安装 + 全量自检

### 7.1 安装（A + B + D 一次做完）

```bash
# A. 系统包
sudo apt update
sudo apt install -y build-essential libmysqlclient-dev libssl-dev mysql-server

# B. vendored 头文件（直连不通时给 curl 加 MIRROR 前缀，见 §2.3）
mkdir -p src/util
curl -fsSL -o src/util/httplib.h \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h
curl -fsSL -o src/util/json.h \
  https://raw.githubusercontent.com/nlohmann/json/v3.10.5/single_include/nlohmann/json.hpp

# D. 运行时前提
sudo mkdir -p /tmp/oj_build && sudo chmod 1777 /tmp/oj_build
```

### 7.2 自检脚本

全部输出 `[OK]` 才进入下一阶段（证书生成 / 建库）。

```bash
check() {
  if eval "$2" >/dev/null 2>&1; then printf '[OK]   '; else printf '[FAIL] '; fi
  echo "$1"
}

echo "--- A 系统依赖 ---"
check "g++ 可编译 C++17"        'echo "int main(){}" | g++ -std=c++17 -x c++ - -o /tmp/t.$$'
check "make 可用"               'command -v make'
check "MySQL 客户端库头文件"     'test -f /usr/include/mysql/mysql.h'
check "OpenSSL 开发头文件"       'test -f /usr/include/openssl/evp.h'
check "mysql_config 可用"       'command -v mysql_config'
check "openssl 命令行"          'command -v openssl'
check "MySQL 服务在运行(3306)"   'ss -ltn | grep -q ":3306"'

echo "--- B vendored 头文件 ---"
check "httplib.h 已入库"          'grep -q "class SSLServer" src/util/httplib.h'
check "httplib 含 multipart 解析" 'grep -q "class MultipartFormDataParser" src/util/httplib.h'
check "json.h 是自包含单头"       'test $(grep -c "^#include <nlohmann" src/util/json.h) -eq 0'

echo "--- D 运行时前提 ---"
check "nobody 用户存在"          'id nobody'
check "build_root 可写"          'test -w /tmp/oj_build'

echo "--- E 辅助工具 ---"
check "curl"                     'command -v curl'
check "git"                      'command -v git'

rm -f /tmp/t.$$
```

---

## 8. 本机现状核查（2026-10-12 实测）

在 `/home/xdtlxl/vibecoding_oj` 当前环境执行的结果，作为 M0 起点的基线：

| 类别 | 项 | 状态 |
|---|---|---|
| A | `build-essential` 12.9ubuntu3 / g++ 11.4.0 | ✅ 已装 |
| A | `libmysqlclient-dev` 8.4.11-1ubuntu22.04 | ✅ 已装 |
| A | `libssl-dev` 3.0.2-0ubuntu1.29 / OpenSSL 3.0.2 | ✅ 已装 |
| A | `mysql-server` 8.4.11，3306 已监听 | ✅ 已装并运行 |
| A | 链接自检 `-lmysqlclient -lssl -lcrypto -lpthread` | ✅ 编译链接通过 |
| B | `src/util/httplib.h` | ❌ **未获取**（`src/` 目录尚不存在） |
| B | `src/util/json.h` | ❌ **未获取** |
| C | `esm.sh` 可达性 | ✅ 返回 200 |
| C | `raw.githubusercontent.com` 直连 | ❌ 不通（TLS reset），需走镜像 |
| D | `nobody` (uid=65534) | ✅ 存在 |
| D | `/tmp/oj_build` | ❌ **未创建** |
| E | `curl` / `git` / `openssl` CLI | ✅ 可用 |
| E | `jq` | ⚠️ 缺失（可选） |

**待办（M0 范围内）：**

```bash
# 1. 取两个 vendored 头文件（本机需走镜像）
mkdir -p src/util
MIRROR=https://ghproxy.net/
curl -fsSL -o src/util/httplib.h \
  "${MIRROR}https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h"
curl -fsSL -o src/util/json.h \
  "${MIRROR}https://raw.githubusercontent.com/nlohmann/json/v3.10.5/single_include/nlohmann/json.hpp"

# 2. 建立沙箱编译临时目录
sudo mkdir -p /tmp/oj_build && sudo chmod 1777 /tmp/oj_build

# 3. 复跑 §7.2 自检
```

---

## 9. 一句话避坑清单

| 现象 | 原因 | 处理 |
|---|---|---|
| 编译报 `SSLServer` 未定义 | 未定义 `CPPHTTPLIB_OPENSSL_SUPPORT`，或误用了 apt 的声明版 httplib | §2.1、§2.6 |
| 编译报 `MultipartFormDataParser` 未定义 | httplib 用了 master 或未钉版本 | §2.4 |
| 头文件下载返回 000 / TLS reset | `raw.githubusercontent.com` 不可达 | §2.3 加镜像前缀 |
| `make` 链接期 `undefined reference`（mysql / EVP） | 缺 `libmysqlclient-dev` / `libssl-dev`，或装了 `default-libmysqlclient-dev` | §1.2 |
| **服务编译好了但每次提交都 CE** | 运行环境里没有 g++（`build-essential` 被当成纯构建工具卸掉了） | §1.2 |
| 每次提交都是 SE | 服务未以 root 启动（降权失败），或 `/tmp/oj_build` 权限不足 | §4 |
| 做题页空白、控制台无报错 | 前端 CDN 用了 jsdelivr | §3 |
| WSL2 重启后连不上数据库 | MySQL 未自启 | `sudo service mysql start` |

---

*本文档的 A 类与 D 类结论在 Ubuntu 22.04.1 (WSL2) 上实测；B 类的版本对比与行数特征为实测值；C 类的 CDN 可达性为实测值。*
