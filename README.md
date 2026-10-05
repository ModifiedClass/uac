
## 依赖安装（Debian 12/13）
sudo apt install -y build-essential qt6-base-dev qt6-httpserver-dev

## 运行
cd bin
export JWT_SECRET="your-very-long-random-secret-key-here-at-least-32-bytes"
./auth-center -c config.json -t templates

## curl 快速验证
# 1. 未登录访问 /authorize -> 302 到 /login
curl -i "http://127.0.0.1:27149/authorize?client_id=fastapi-app&redirect_uri=http://your-domain.com/callback&response_type=code&scope=openid%20profile&state=abc123"

# 2. 登录并保存 Cookie
curl -i -c cookies.txt -X POST http://127.0.0.1:27149/login \
  -d "username=admin&password=admin123&redirect=/"

# 3. 带 Cookie 访问 /authorize -> 302 到 callback 并携带 code
curl -i -b cookies.txt "http://127.0.0.1:27149/authorize?client_id=fastapi-app&redirect_uri=http://your-domain.com/callback&response_type=code&scope=openid%20profile&state=abc123"

# 4. 用 code 换 token
curl -s -X POST http://127.0.0.1:27149/token \
  -d "grant_type=authorization_code" \
  -d "code=<上一步拿到的 code>" \
  -d "redirect_uri=http://your-domain.com/callback" \
  -d "client_id=fastapi-app" \
  -d "client_secret=fastapi-app-secret"

# 5. 用 token 访问 /userinfo
curl -s http://127.0.0.1:27149/userinfo -H "Authorization: Bearer <access_token>"

# 6. 登出
curl -i -b cookies.txt http://127.0.0.1:27149/logout

# 目录结构
/opt/uac/
├── uac.pro
├── config.json
├── main.cpp
├── templates/
│   └── login.html
├── src/
│   ├── models.h
│   ├── bcrypt.h / bcrypt.cpp
│   ├── crypto.h / crypto.cpp
│   ├── jwt.h / jwt.cpp
│   ├── store.h / store.cpp
│   ├── store_pg.h / store_pg.cpp
│   ├── store_redis.h / store_redis.cpp
│   ├── plugins.h / plugins.cpp
│   └── authserver.h / authserver.cpp
├── deploy/
│   ├── Dockerfile
│   ├── docker-compose.yml
│   ├── .env.example
│   ├── install-docker.sh
│   ├── deploy.sh
│   ├── backup.sh
│   ├── nginx/conf.d/default.conf
│   └── postgres/init.sql