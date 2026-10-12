# vibecoding_oj

仿 LeetCode 的在线判题平台（Online Judge）。C++17 + MySQL + 原生 HTML/CSS/JS，无前端构建工具。

> 完整规格见 [SPEC.md](SPEC.md)。**本文档是唯一的部署依据**：在一台干净的 Linux 机器上，
> 从上到下逐条复制粘贴执行本文档的命令，即可启动服务并完成一次提交。
> 无需先读 SPEC.md —— 文中对 SPEC 章节号的引用仅为方便溯源，每个结论此处都已写明。

## 目录

| # | 章节 | 状态 |
|---|---|---|
| 1 | [环境准备](#1-环境准备) | ✅ 已完成 |
| 2 | 证书生成 | ⏳ 待补 |
| 3 | 数据库初始化 | ⏳ 待补 |
| 4 | 配置说明 | ⏳ 待补 |
| 5 | 编译与启动 | ⏳ 待补 |
| 6 | 常见问题 | ⏳ 待补 |

---

## 1. 环境准备

### 1.1 目标环境

| 项 | 要求 | 已验证环境 |
|---|---|---|
| 操作系统 | 任意现代 Linux（Debian/Ubuntu 系可直接照抄命令） | Ubuntu 22.04.1 LTS (x86_64, WSL2) |
| 编译器 | 支持 C++17 | g++ 11.4.0 |
| 数据库 | MySQL 8.x | MySQL Server 8.4.11 |
| 加密库 | OpenSSL 1.1.1 / 3.x | OpenSSL 3.0.2 |
| 权限 | 能以 **root** 启动服务进程 | — |
| CPU | 双核以上 | 4 核（判题线程数取 `min(4, 核数-1)`） |

> 只有 x86_64 上验证过。ARM 理论上可用，但沙箱依赖 `nobody` 用户与 `wait4` 行为，未做验证。

### 1.2 系统依赖（apt）

一条命令装齐：

```bash
sudo apt update
sudo apt install -y build-essential libmysqlclient-dev libssl-dev mysql-server
```

| 包 | 用途 | 对应 SPEC |
|---|---|---|
| `build-essential` | g++ 与 make，编译服务与沙箱内编译用户代码 | §4.1 |
| `libmysqlclient-dev` | MySQL C API（`mysql/mysql.h`） | §4.1 |
| `libssl-dev` | PBKDF2 密码哈希、HMAC-SHA256 签发 JWT、TLS | §4.1 |
| `mysql-server` | 数据库服务本体 | §4.1 |

`openssl` 命令行工具随上述依赖安装，用于第 2 章的证书生成。`curl` 与 `nohup` 为系统自带，用于验证与后台运行。

**验证：**

```bash
g++ --version | head -1                    # 期望：g++ (Ubuntu 11.4.0... 11.4.0
mysql_config --version                     # 期望：8.4.11
openssl version                            # 期望：OpenSSL 3.0.2 ...
ls /usr/include/mysql/mysql.h /usr/include/openssl/evp.h   # 两个文件都必须存在
sudo service mysql status                  # 期望：Active: active (running)
```

### 1.3 C++ 头文件库（vendored，不走包管理器）

项目用到两个 header-only 库，**都必须取上游单头文件，不能用 apt 包替代**。

#### 为什么不能用 apt

以下结论均在 Ubuntu 22.04 上实测：

| 库 | apt 包 | 实测结果 |
|---|---|---|
| cpp-httplib | `libcpp-httplib-dev` (0.10.3+ds-1) | ❌ 包内 `httplib.h` 仅 1851 行、**只有声明没有实现体**，实现被编进 `libcpp-httplib.so`，而该 `.so` **未编译 OpenSSL**。项目要求 `#define CPPHTTPLIB_OPENSSL_SUPPORT` 后以 header-only 方式使用 SSLServer，此路线走不通 |
| nlohmann/json | `nlohmann-json3-dev` (3.10.5-2) | ❌ 包内 `json.hpp` 含 24 处 `#include <nlohmann/detail/...>`，是拆开的目录树，**不是单文件**，无法作为单个头文件 vendor 进仓库 |

#### 获取命令

```bash
mkdir -p src/util

# cpp-httplib（必须钉版本，勿用 master —— 见下方版本说明）
curl -fsSL -o src/util/httplib.h \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h

# nlohmann/json 单头文件
curl -fsSL -o src/util/json.h \
  https://raw.githubusercontent.com/nlohmann/json/v3.10.5/single_include/nlohmann/json.hpp
```

> **网络受限时**：`raw.githubusercontent.com` 在部分网络下不可达（实测返回 000）。此时在 URL 前加镜像前缀即可，例如 `https://ghproxy.net/https://raw.githubusercontent.com/...`。
> 镜像为第三方代理，首次获取后建议核对文件内容无误再提交入库；**入库后即为 vendored，后续部署不再依赖网络**。

**版本说明（实测对比，决定为何钉 v0.18.3）：**

| 版本 | `class SSLServer` | multipart 解析类 | OpenSSL 3 分支 |
|---|---|---|---|
| v0.14.3 / v0.16.3 / **v0.18.3** | ✅ 有 | ✅ `MultipartFormDataParser` | ✅ 有 |
| master（不固定） | ✅ 有 | ❌ 已改名，旧代码不兼容 | ✅ 有 |

项目第 3 章之后的管理端测试数据上传接口依赖 `MultipartFormDataParser`，用 master 会导致该接口代码不兼容。**不要用 master 或未钉版本的下载地址。**

**验证（不依赖 GitHub 校验值，靠特征自检）：**

```bash
wc -l src/util/httplib.h src/util/json.h
# 期望：httplib.h ≈ 10255 行；json.h ≈ 22091 行（远小/远大于此说明下错了文件）

grep -c 'class SSLServer' src/util/httplib.h
# 期望：≥ 1。为 0 说明误用了 apt 的声明版或错误文件

grep -c '^#include <nlohmann' src/util/json.h
# 期望：0。非 0 说明拿到的不是自包含单头文件
```

### 1.4 前端依赖（CDN，无需安装）

前端无构建工具，依赖在浏览器运行时从 CDN 以 ESM 方式加载：

| 库 | 用途 | 已验证地址 |
|---|---|---|
| CodeMirror 6 | 做题页 C++ 代码编辑器 | `https://esm.sh/codemirror@6` |
| marked | Markdown 题面渲染 | `https://esm.sh/marked` |
| DOMPurify | 题面 HTML 消毒 | `https://esm.sh/dompurify` |

> ⚠️ **CDN 必须是 esm.sh**（或备选 `unpkg.com`、`cdnjs.cloudflare.com`）。
> 实测 `cdn.jsdelivr.net` 在部分网络下不可达；若照抄网上常见的 jsdelivr 地址，做题页会**空白无报错**。
>
> ⚠️ 这三个依赖走公网 CDN，**完全离线环境下做题页不可用**。若部署环境无外网，需自行下载后改为本地托管。

### 1.5 运行时前提（不是安装包，但缺了服务起不来）

| 前提 | 为什么需要 | 检查方式 |
|---|---|---|
| 存在 `nobody` / `nogroup` 用户 | 判题沙箱靠 `setuid(nobody)` 降权，防用户代码删改项目文件 | `id nobody`，期望 uid=65534 |
| **以 root 启动服务** | 只有 root 能 `setuid` 降权，否则沙箱降权失败 | 启动时用 `sudo` |
| 编译临时目录可被 `nobody` 读写执行 | 编译产物 `a.out` 由降权后的 nobody 写入并执行；目录属主若为 root 而未放开权限，**每次提交都会返回 SE** | 见下方命令 |
| MySQL 服务在运行 | 服务启动即连库 | `sudo service mysql status` |

```bash
# nobody 用户（Ubuntu/Debian 默认存在）
id nobody

# 编译临时目录（路径取自 SPEC §11.3 的 build_root）
sudo mkdir -p /tmp/oj_build
sudo chmod 1777 /tmp/oj_build      # 或按实际沙箱实现改为 chown nobody
ls -ld /tmp/oj_build

# WSL2 中系统重启后 MySQL 可能不自启
sudo service mysql start
```

### 1.6 环境自检

执行以下脚本，全部输出 `[OK]` 即可进入第 2 章：

```bash
check() {
  if eval "$2" >/dev/null 2>&1; then printf '[OK]   '; else printf '[FAIL] '; fi
  echo "$1"
}

check "g++ 可编译 C++17"       'echo "int main(){}" | g++ -std=c++17 -x c++ - -o /tmp/t.$$'
check "make 可用"              'command -v make'
check "MySQL 客户端库头文件"    'test -f /usr/include/mysql/mysql.h'
check "OpenSSL 开发头文件"      'test -f /usr/include/openssl/evp.h'
check "mysql_config 可用"      'command -v mysql_config'
check "openssl 命令行"         'command -v openssl'
check "MySQL 服务在运行"       'ss -ltn | grep -q ":3306"'
check "nobody 用户存在"        'id nobody'
check "httplib.h 已入库"       'grep -q "class SSLServer" src/util/httplib.h'
check "json.h 是自包含单头"     'test $(grep -c "^#include <nlohmann" src/util/json.h) -eq 0'
check "build_root 可写"        'test -w /tmp/oj_build'

rm -f /tmp/t.$$
```

### 1.7 一句话避坑清单

| 现象 | 原因 | 处理 |
|---|---|---|
| 前端做题页空白，控制台无报错 | CDN 用了 jsdelivr | 换成 `esm.sh`（§1.4） |
| 头文件下载返回 000 / 连接超时 | `raw.githubusercontent.com` 不可达 | URL 前加镜像前缀（§1.3） |
| 每次提交都是 SE | 服务未以 root 启动，降权失败；或 `build_root` 权限不足 | 见 §1.5 |
| 编译报 `SSLServer` 未定义 | 未定义 `CPPHTTPLIB_OPENSSL_SUPPORT` 宏，或误用了 apt 的声明版 httplib | 见 §1.3 与 SPEC §4.2 |
| 提交代码后判题直接报错 | 未安装 `nobody` 用户 / 目录隔离失败 | 见 §1.5 |
| WSL2 重启后连不上数据库 | MySQL 未自启 | `sudo service mysql start` |
