# uac 认证中心 测试流程说明

测试环境: 无 docker、无数据库。约定:

- 认证中心: `http://127.0.0.1:27149`
- FastAPI 业务应用: `http://127.0.0.1:8000`(回调地址已登记在白名单)
- 演示账号: `admin/admin123`(角色 admin,user)、`alice/alice123`(角色 user)
- 客户端: `fastapi-app` / `fastapi-app-secret`

---

## 1. 启动

```bash
# 终端 1: 认证中心
cd uac/auth-center
qmake6 uac.pro CONFIG+=release && make -j"$(nproc)"
./auth-center        # 未设置 JWT_SECRET 时会告警并使用演示默认密钥(仅测试)

# 终端 2: FastAPI 业务应用(可选, 联调用)
cd uac/client-example
pip install -r requirements.txt
uvicorn app:app --host 127.0.0.1 --port 8000
```

---

## 2. 浏览器流程(对应架构图步骤 1~5)

1. 打开 `http://127.0.0.1:27149/login` —— 显示登录表单。
2. 输入 `admin / admin123`, 提交 —— 302 回首页, 页面显示当前用户信息与退出链接
   (DevTools → Application → Cookies 可见 `session_id`, 属性 HttpOnly / SameSite=Lax / Path=/)。
3. 首页点击“发起授权码流程” —— 302 到 `http://127.0.0.1:8000/callback?code=...&state=demo`。
4. 未登录状态下直接访问
   `http://127.0.0.1:27149/authorize?client_id=fastapi-app&response_type=code&redirect_uri=http%3A%2F%2F127.0.0.1%3A8000%2Fcallback&state=demo`
   —— 应 302 到 `/login?redirect=...`, 登录后自动跳回并携带授权码。
5. 点击“退出登录” —— 会话被清除, Cookie 删除。

## 3. curl 全流程(步骤 2~11)

```bash
AUTH=http://127.0.0.1:27149
CB_ENC=http%3A%2F%2F127.0.0.1%3A8000%2Fcallback   # redirect_uri 的 URL 编码
CB=http://127.0.0.1:8000/callback

# 3.1 登录页(200, HTML 表单)
curl -si $AUTH/login | head -5

# 3.2 登录(302 + Set-Cookie: session_id=...; HttpOnly; SameSite=Lax)
curl -si -c /tmp/uac-cookies.txt \
  -d "username=admin&password=admin123&redirect=/" $AUTH/login | grep -Ei '^(HTTP|Location|Set-Cookie)'

# 3.3 首页(携带 Cookie, 显示"当前用户: admin")
curl -s -b /tmp/uac-cookies.txt $AUTH/

# 3.4 授权端点(302, Location 携带 code)
LOC=$(curl -si -b /tmp/uac-cookies.txt \
  "$AUTH/authorize?client_id=fastapi-app&response_type=code&scope=openid+profile&state=demo&redirect_uri=$CB_ENC" \
  | grep -i '^location:' | tr -d '\r' | awk '{print $2}')
echo "Location: $LOC"
CODE=$(echo "$LOC" | sed 's/.*code=\([^&]*\).*/\1/')
echo "code=$CODE"

# 3.5 授权码换令牌(JSON: access_token/token_type=Bearer/expires_in=3600/scope)
TOKEN_JSON=$(curl -s -X POST $AUTH/token \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data-urlencode "grant_type=authorization_code" \
  --data-urlencode "code=$CODE" \
  --data-urlencode "redirect_uri=$CB" \
  --data-urlencode "client_id=fastapi-app" \
  --data-urlencode "client_secret=fastapi-app-secret")
echo "$TOKEN_JSON" | python3 -m json.tool

TOKEN=$(echo "$TOKEN_JSON" | python3 -c 'import sys,json;print(json.load(sys.stdin)["access_token"])')

# 3.6 用户信息端点(Bearer 令牌)
curl -s $AUTH/userinfo -H "Authorization: Bearer $TOKEN" | python3 -m json.tool
# 期望: {"sub":"u-1001","name":"admin","phone":"13800000001","email":"admin@example.com","roles":["admin","user"],...}

# 3.7 授权码一次性: 重复 3.5 的请求 -> {"error":"invalid_grant","error_description":"授权码无效或已被使用"}

# 3.8 登出(302, Set-Cookie Max-Age=0 清除会话)
curl -si -b /tmp/uac-cookies.txt $AUTH/logout | grep -Ei '^(HTTP|Location|Set-Cookie)'
```

## 4. 负面测试

| 场景 | 命令要点 | 期望 |
|------|----------|------|
| 密码错误 | `-d "username=admin&password=wrong"` POST /login | 401 登录页提示"用户名或密码错误" |
| 登录防抖 | 同一 IP 连续 5 次错误密码后再次尝试 | 429 + `Retry-After`, 锁定 900 秒(可配置) |
| 未知 client_id | /authorize 带 `client_id=xxx` | 400 未知的 client_id |
| redirect_uri 不在白名单 | /authorize 带 `redirect_uri=https://evil.com/cb` | 400 不在白名单 |
| 非 code 响应类型 | /authorize 带 `response_type=token` | 302 到 redirect_uri 且带 `error=unsupported_response_type` |
| 未登录访问 /authorize | 不带 Cookie | 302 到 `/login?redirect=...` |
| 错误 client_secret | /token 传错 secret | 401 `invalid_client` + `WWW-Authenticate: Basic` |
| 错误 grant_type | /token 传 `grant_type=password` | 400 `unsupported_grant_type` |
| 错误 Content-Type | /token 用 JSON 体 | 400 `invalid_request` |
| 无令牌访问 /userinfo | 不带 Authorization 头 | 401 `invalid_token` + `WWW-Authenticate: Bearer` |
| 伪造令牌 | `Authorization: Bearer aaa.bbb.ccc` | 401 签名校验失败 |
| 令牌黑名单 | config.json → plugins → blacklist → blocked_tokens 填入令牌后重启 | /userinfo 返回 401 令牌已被列入黑名单 |
| 过期令牌 | 设 `UAC_TOKEN_TTL=2` 重启, 换取令牌后等待 3 秒 | 401 令牌已过期 |

## 5. FastAPI 业务应用联调

```bash
# 浏览器访问:
open http://127.0.0.1:8000/login        # -> 认证中心 -> 登录 -> 跳回 /callback 显示 access_token 与 claims
# curl 模拟:
curl -si http://127.0.0.1:8000/login | grep -i location   # 指向 /authorize
CB_LOC=$(curl -si -c /tmp/fapi.txt http://127.0.0.1:8000/login | grep -i '^location:' | tr -d '\r' | awk '{print $2}')
CODE2=$(curl -si -b /tmp/uac-cookies.txt "$CB_LOC" | grep -i '^location:' | tr -d '\r' | awk '{print $2}' | sed 's/.*code=\([^&]*\).*/\1/')
APP_TOKEN=$(curl -s "http://127.0.0.1:8000/callback?code=$CODE2&state=demo-state" | python3 -c 'import sys,json;print(json.load(sys.stdin)["access_token"])')

curl -s http://127.0.0.1:8000/protected        -H "Authorization: Bearer $APP_TOKEN"
# {"message":"欢迎","sub":"u-1001","name":"admin","roles":["admin","user"]}
curl -s http://127.0.0.1:8000/protected/admin  -H "Authorization: Bearer $APP_TOKEN"
# {"message":"管理员面板",...}

# alice 登录后访问 /protected/admin -> 403 需要 admin 角色
```

## 6. 验收标准对照

| 验收项 | 方法 |
|--------|------|
| □ Debian/FreeBSD 编译运行 | §1 + docs/DEPLOYMENT.md |
| □ https://auth.your-domain.com/login 显示登录页 | §2.1(经 Nginx) |
| □ admin/admin123 登录成功并创建会话 | §3.2 |
| □ /authorize 正确重定向并生成授权码 | §3.4 |
| □ 授权码换 JWT | §3.5 |
| □ JWT 访问 /userinfo 返回用户信息 | §3.6 |
| □ FastAPI 完成登录并访问 /protected | §5 |
| □ /protected/admin 仅 admin 可访问 | §5(alice 403) |
| □ Nginx 反向代理正常 | §3.6 + /healthz |
| □ docker 开机自启、崩溃重启 | docs/DEPLOYMENT.md §5(`restart: unless-stopped`) |
| □ 日志无敏感信息泄露 | 检查 journal/终端输出: 无密码、JWT_SECRET、完整令牌 |
