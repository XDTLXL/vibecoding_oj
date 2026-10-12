#!/usr/bin/env bash
#
# 生成 VibeCoding OJ 使用的自签证书
# 依据：SPEC.md §4（传输加密）、§13 R5（自签证书的代价）、§15 M0
#
# 产出（与 config/config.ini 的 [server] cert_file / key_file 对应）：
#   certs/server.crt   证书
#   certs/server.key   私钥（权限 600）
#
# 关于 SAN：现代浏览器只认 subjectAltName，不再认 CN。因此本脚本必须写入 SAN，
# 否则即使绕过告警，部分浏览器仍会拒绝连接（且报错信息不指向真正原因）。

set -euo pipefail

OUT_DIR="${OJ_CERT_DIR:-certs}"
FORCE=0
EXTRA_SANS=()

usage() {
  cat <<'EOF'
用法: tools/gen_cert.sh [--force] [--san <SAN条目>]...

生成 certs/server.key 与 certs/server.crt（自签、RSA 2048、SHA-256、825 天）。

默认 subjectAltName 覆盖本机常见访问方式：
  DNS:localhost, IP:127.0.0.1, IP:::1, 本机主机名, 本机主 IP

选项:
  --san <条目>   追加一个 SAN 条目，可重复。例：--san IP:192.168.1.10 --san DNS:oj.example.com
  --force        证书已存在时覆盖（默认拒绝覆盖，避免无意中轮换指纹）
  -h, --help     显示本帮助

环境变量:
  OJ_CERT_DIR    输出目录，默认 certs

示例:
  tools/gen_cert.sh
  tools/gen_cert.sh --force --san IP:192.168.1.10
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --force)   FORCE=1; shift ;;
    --san)
      if [ $# -lt 2 ]; then
        echo "[FAIL] --san 需要一个参数" >&2
        exit 2
      fi
      EXTRA_SANS+=("$2")
      shift 2
      ;;
    *)
      echo "[FAIL] 未知参数：$1（用 --help 查看用法）" >&2
      exit 2
      ;;
  esac
done

if ! command -v openssl >/dev/null 2>&1; then
  echo "[FAIL] 找不到 openssl 命令；执行：sudo apt install -y libssl-dev" >&2
  exit 1
fi

# 收集 SAN。::1 写作 IP:::1 —— 条目格式是 "<类型>:<值>"，值是 "::1"
build_san_list() {
  local san="DNS:localhost,IP:127.0.0.1,IP:::1"

  local host=""
  host="$(hostname 2>/dev/null || true)"
  if [ -n "$host" ] && [ "$host" != "localhost" ]; then
    san="${san},DNS:${host}"
  fi

  # 取默认路由对应的本机 IP；没有 ip 命令或无网络时静默跳过
  local ip=""
  ip="$(ip route get 1.1.1.1 2>/dev/null | awk '{ for (i = 1; i <= NF; i++) if ($i == "src") { print $(i + 1); exit } }')" || true
  if [ -n "$ip" ]; then
    san="${san},IP:${ip}"
  fi

  if [ "${#EXTRA_SANS[@]}" -gt 0 ]; then
    local extra=""
    for extra in "${EXTRA_SANS[@]}"; do
      san="${san},${extra}"
    done
  fi

  printf '%s' "$san"
}

SAN="$(build_san_list)"

CRT="${OUT_DIR}/server.crt"
KEY="${OUT_DIR}/server.key"

if [ "$FORCE" -ne 1 ] && { [ -e "$CRT" ] || [ -e "$KEY" ]; }; then
  echo "[FAIL] 证书已存在：$CRT / $KEY"
  echo "       重新生成会更换指纹，浏览器需重新确认，正在运行的服务需重启。"
  echo "       确实要重新生成请加 --force。"
  exit 3
fi

mkdir -p "$OUT_DIR"

generate() {
  openssl req -x509 -newkey rsa:2048 -sha256 -nodes \
    -days 825 \
    -keyout "$KEY" -out "$CRT" \
    -subj "/C=CN/O=VibeCoding OJ/CN=localhost" \
    -addext "subjectAltName=${SAN}" \
    -addext "basicConstraints=critical,CA:FALSE" \
    -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
    -addext "extendedKeyUsage=serverAuth"
}

# openssl 生成密钥时会往 stderr 打一串 +++ 进度噪声，正常路径下丢弃；
# 失败时重跑一次把真实报错显示出来，否则用户只看到一句"生成失败"无从下手
umask 077
if ! generate >/dev/null 2>&1; then
  echo "[INFO] 首次生成失败，重跑以显示 openssl 的原始报错：" >&2
  generate || { echo "[FAIL] 证书生成失败" >&2; exit 4; }
fi

chmod 600 "$KEY"
chmod 644 "$CRT"

echo "[OK] 证书已生成"
echo "     证书    : $CRT"
echo "     私钥    : $KEY（权限 $(stat -c '%a' "$KEY")）"
echo "     有效期至: $(openssl x509 -in "$CRT" -noout -enddate | cut -d= -f2)"
echo "     指纹    : $(openssl x509 -in "$CRT" -noout -fingerprint -sha256 | cut -d= -f2)"
echo "     SAN     :"
openssl x509 -in "$CRT" -noout -ext subjectAltName 2>/dev/null | tail -n +2 | sed 's/^ */       /'

cat <<'EOF'

接下来：
  1) 确认 config/config.ini 的 [server] 指向本证书（默认值即为 certs/server.crt 与 certs/server.key）
  2) 浏览器访问时会提示「证书不受信任」——这是自签证书的预期行为（SPEC.md §13 R5）。
     绕过方式：
       Chrome / Edge：点「高级」→「继续前往」；若被拦死无按钮，在本页键盘直接输入 thisisunsafe
       Firefox      ：点「高级」→「接受风险并继续」
       curl         ：curl -k https://localhost:8443/api/health
  3) 若要从另一台机器访问，本机 IP 必须在该 SAN 列表里，否则加参数重新生成：
       tools/gen_cert.sh --force --san IP:<本机IP>
  4) certs/ 已在 .gitignore 中：证书与私钥不入库，每台机器各自生成

注意：服务必须以 root 启动（判题沙箱需要 setuid 降权，SPEC.md §8.3）。
EOF
