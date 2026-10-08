# uac 认证中心部署文档 (Debian / FreeBSD / Docker)

- **测试环境**: 不用 docker、不用数据库 —— 直接本机编译运行二进制(内存存储)。
- **生产环境**: docker + 数据库(PostgreSQL 用户/客户端, Redis 授权码/会话), Nginx 对外 HTTPS。

---

## 1. 环境变量总表

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `UAC_CONFIG` | `./config.json` | 配置文件路径 |
| `UAC_HOST` | `127.0.0.1` | 监听地址(仅本地回环, 由 Nginx 对外) |
| `UAC_PORT` | `27149` | 监听端口 |
| `JWT_SECRET` | 无(演示默认值+告警) | ★JWT 签名密钥, 生产必须注入, 禁止硬编码 |
| `UAC_SESSION_TTL` | `86400` | 会话有效期(秒) |
| `UAC_CODE_TTL` | `300` | 授权码有效期(秒) |
| `UAC_TOKEN_TTL` | `3600` | 访问令牌有效期(秒) |

生成强密钥: `openssl rand -hex 32`

---

## 2. 编译(通用)

```bash
cd auth-center
qmake6 uac.pro CONFIG+=release
make -j"$(nproc)"
# 产物: ./auth-center(单一可执行文件)
# 可选安装: sudo make install  ->  /opt/auth-center/{auth-center,config.json,templates/}
```

---

## 3. Debian 11 / 12 / 13 部署

### 3.1 安装依赖

```bash
# Debian 12 (bookworm) / Debian 13 (trixie):
sudo apt-get update
sudo apt-get install -y build-essential qt6-base-dev qt6-base-dev-tools libqt6httpserver6 nginx
# 注: 若仓库缺少 libqt6httpserver6(如旧版 Debian 12 快照), 使用 Qt 在线安装器
#     安装 Qt 6.12(含 Qt HttpServer 模块)并配置 qmake6 路径。

# Debian 11 (bullseye): 官方仓库无 Qt6, 必须使用 Qt 在线安装器安装 Qt 6.12:
#   https://www.qt.io/download-qt-installer
#   安装后: export PATH=$HOME/Qt/6.12.*/gcc_64/bin:$PATH
```

### 3.2 编译与安装

```bash
cd /path/to/uac/auth-center
qmake6 uac.pro CONFIG+=release && make -j"$(nproc)"
sudo mkdir -p /opt/auth-center/templates
sudo cp auth-center config.json /opt/auth-center/
sudo cp templates/login.html /opt/auth-center/templates/
sudo chown -R www-data:www-data /opt/auth-center
```

### 3.3 systemd 服务

```bash
sudo cp deploy/auth-center.service /etc/systemd/system/
sudo nano /etc/systemd/system/auth-center.service   # 替换 JWT_SECRET
sudo systemctl daemon-reload
sudo systemctl enable --now auth-center             # 开机自启
systemctl status auth-center
journalctl -u auth-center -f                        # 日志(输出到 journal)
```

`auth-center.service` 要点: 以 `www-data` 运行、工作目录 `/opt/auth-center`、`Environment=JWT_SECRET=...` 注入、
`Restart=on-failure`(崩溃自动重启)、`ProtectSystem=full` 等加固项。全文见 `deploy/auth-center.service`。

### 3.4 Nginx 反向代理

```bash
sudo cp deploy/nginx-auth-center.conf /etc/nginx/sites-available/auth-center
sudo ln -s /etc/nginx/sites-available/auth-center /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

配置要点: 监听 80、反代 `127.0.0.1:27149`、传递 `Host/X-Real-IP/X-Forwarded-For/X-Forwarded-Proto`、
keepalive 连接池、安全头(`X-Content-Type-Options`/`X-Frame-Options`)、访问/错误日志。全文见 `deploy/nginx-auth-center.conf`。

### 3.5 启用 HTTPS(生产必须)

```bash
sudo apt-get install -y certbot python3-certbot-nginx
sudo certbot --nginx -d auth.your-domain.com
# 然后在 Nginx 配置中打开 443 ssl 段与 Strict-Transport-Security 头
```

### 3.6 验证

```bash
curl -i http://127.0.0.1:27149/healthz          # 本地直连
curl -i https://auth.your-domain.com/login      # 经 Nginx
```

---

## 4. FreeBSD 15 部署

### 4.1 依赖与编译

```sh
pkg install -y qt6-base qt6-httpserver gmake nginx
# 若 pkg 无 qt6-httpserver, 用 ports 编译: cd /usr/ports/devel/qt6-httpserver && make install clean

cd /path/to/uac/auth-center
/usr/local/lib/qt6/bin/qmake6 uac.pro CONFIG+=release   # 或 export PATH=/usr/local/lib/qt6/bin:$PATH
gmake -j"$(sysctl -n hw.ncpu)"
```

### 4.2 安装与 rc.d 自启

```sh
mkdir -p /usr/local/etc/uac/templates
cp auth-center config.json /usr/local/sbin/   # 或 /usr/local/etc/uac/ 下自定
cp templates/login.html /usr/local/etc/uac/templates/

# rc.d 脚本: /usr/local/etc/rc.d/auth_center
cat > /usr/local/etc/rc.d/auth_center <<'EOF'
#!/bin/sh
# PROVIDE: auth_center
# REQUIRE: LOGIN NETWORKING
# KEYWORD: shutdown

. /etc/rc.subr
name=auth_center
rcvar=auth_center_enable
command=/usr/local/sbin/auth-center
start_precmd="auth_center_prestart"

auth_center_prestart() {
    export JWT_SECRET="$(cat /usr/local/etc/uac/jwt_secret)"   # 文件注入, 权限 600
    export UAC_CONFIG="/usr/local/etc/uac/config.json"
    cd /usr/local/etc/uac
}

load_rc_config $name
: ${auth_center_enable:=NO}
run_rc_command "$1"
EOF
chmod 555 /usr/local/etc/rc.d/auth_center

# 密钥与配置
openssl rand -hex 32 > /usr/local/etc/uac/jwt_secret && chmod 600 /usr/local/etc/uac/jwt_secret
cp /path/to/uac/auth-center/config.json /usr/local/etc/uac/

# 开机自启
sysrc auth_center_enable=YES
service auth_center start
tail -f /var/log/messages
```

### 4.3 Nginx(与 Debian 一致)

Nginx 配置复用 `deploy/nginx-auth-center.conf`; FreeBSD 下站点目录为
`/usr/local/etc/nginx/`, 通过 `/usr/local/etc/nginx/nginx.conf` 的 `include` 加载, 然后
`service nginx reload`。

---

## 5. Docker 生产部署

测试环境不用 docker; docker 面向生产(开机自启 + 崩溃重启由 `restart: unless-stopped` 保证,
前提是 `systemctl enable docker`)。

```bash
cd deploy/docker
cp .env.example .env
nano .env                                   # JWT_SECRET=$(openssl rand -hex 32)

# 基础(认证中心 + Nginx)
docker compose up -d --build

# 生产附加 PostgreSQL + Redis(挂载 db/init.sql 自动初始化)
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d

docker compose ps                            # 状态
curl -i http://127.0.0.1:80/healthz          # 经容器内 Nginx 验证
```

> 切换外部存储: `auth-center/src/storage.h` 定义了 `UserStore/ClientStore/AuthCodeStore/SessionStore`
> 四个接口, 实现 `PostgresUserStore`、`PostgresClientStore`、`RedisAuthCodeStore`、`RedisSessionStore`
> 后在 `AuthServer::init()` 中替换实例即可, 路由逻辑零改动。多副本部署的前提正是将
> 授权码/会话外置到 Redis(见生产清单)。

---

## 6. 常见问题

| 问题 | 处理 |
|------|------|
| `Project ERROR: Unknown module(s) in QT: httpserver` | Qt 版本过低或未装 httpserver 模块; 需要 Qt ≥ 6.5(目标 6.12) |
| Debian 仓库无 `libqt6httpserver6` | 用 Qt 在线安装器安装 Qt 6.12 后以 `qmake6` 全路径编译 |
| Qt 6.5/6.6 编译报错 `headers()` | 本项目按 Qt 6.7+ API 编写(`QHttpHeaders`); 旧版本 `request.headers()` 返回 `QList<QPair<QByteArray,QByteArray>>`, 需自行遍历取值, 或将 Qt 升级到 ≥ 6.7 |
| 启动告警“使用内置演示密钥” | 未设置 `JWT_SECRET`, 生产必须注入 |
| 登录后 302 到错误地址 | 应用侧只允许站内相对路径 redirect, 检查传入的 redirect 参数 |
| 授权码换令牌报 `redirect_uri 与签发时不一致` | `/token` 提交的 `redirect_uri` 必须与 `/authorize` 完全一致(含大小写) |
| 多副本会话丢失 | 会话在内存; 生产需 Redis(见 §5) |
