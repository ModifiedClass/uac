# uac —— Qt C++ 统一认证中心 (Unified Authentication Center)

基于 **Qt 6 (QHttpServer)** 实现的 OAuth2 授权码模式 + JWT(HS256) 单点登录(SSO)认证中心。
所有业务系统不再各自维护账号密码，统一由认证中心完成身份认证；新业务系统无需再实现用户权限模块，直接接入即可。

```
┌──────────┐     ┌──────────────┐     ┌───────────────┐     ┌──────────────┐
│  浏览器   │     │   Nginx      │     │ Qt C++ 认证中心 │     │  FastAPI     │
│  (用户)   │     │ (反向代理)    │     │  (QHttpServer) │     │  业务应用     │
└────┬─────┘     └──────┬───────┘     └───────┬───────┘     └──────┬───────┘
     │                  │                     │                    │
     │ 1. 访问 /login   │                     │                    │
     │─────────────────>│                     │                    │
     │                  │ 2. 转发 127.0.0.1:27149                   │
     │                  │────────────────────>│                    │
     │                  │                     │                    │
     │ 3. 显示登录页    │                     │                    │
     │<─────────────────│<────────────────────│                    │
     │                  │                     │                    │
     │ 4. 提交账号密码  │                     │                    │
     │─────────────────>│────────────────────>│                    │
     │                  │                     │                    │
     │ 5. 重定向回业务系统 /callback           │                    │
     │<─────────────────│<────────────────────│                    │
     │                  │                     │                    │
     │ 6. 前端携带 code 请求后端                │                    │
     │─────────────────────────────────────────────────────────────>│
     │                  │                     │                    │
     │                  │ 7. 后端用 code 换 token │                    │
     │                  │<────────────────────────────────────────────│
     │                  │ 8. 转发 /token       │                    │
     │                  │────────────────────>│                    │
     │                  │                     │                    │
     │                  │ 9. 返回 JWT token   │                    │
     │                  │────────────────────────────────────────────>│
     │                  │                     │                    │
     │ 10. 携带 token 访问受保护接口            │                    │
     │─────────────────────────────────────────────────────────────>│
     │                  │                     │                    │
     │ 11. 返回受保护数据                     │                    │
     │<─────────────────────────────────────────────────────────────│
```

## 特性

- **OAuth2 授权码模式**: `/authorize` → `/token` → `/userinfo`, 授权码一次性使用、5 分钟有效(可配置)
- **JWT(HS256)**: 手写 HMAC-SHA256 实现(示例), 密钥从 `JWT_SECRET` 环境变量注入; 生产建议 RS256(jwt-cpp)
- **会话管理**: 随机会话 ID + HttpOnly/SameSite=Lax Cookie, 24 小时有效(可配置)
- **密码哈希**: SHA-256+盐(示例, 代码中明确标注生产必须替换 bcrypt/argon2), 提供 `bcryptHash/bcryptVerify` 接口
- **插件化安全(需求 4.7)**: 按 `config.json` 配置装配 —— 登录防抖限流(失败锁定)、IP/账号/令牌黑名单, 易于扩展
- **存储抽象**: 用户/客户端/授权码/会话均为接口, 示例内存实现, 生产替换 PostgreSQL/Redis
- **部署**: 监听 127.0.0.1:27149, Nginx 反向代理对外 HTTPS, systemd / Docker(开机自启、崩溃重启), 支持 Debian 11/12/13 与 FreeBSD 15

## 目录结构

```
uac/
├── README.md                      本文件
├── auth-center/                   Qt C++ 认证中心
│   ├── uac.pro                    qmake 工程
│   ├── main.cpp                   入口
│   ├── config.json                配置(用户/客户端/插件/TTL)
│   ├── templates/login.html       登录页模板
│   └── src/
│       ├── authserver.h/.cpp      路由与业务逻辑
│       ├── jwt.h/.cpp             JWT HS256 签发/校验
│       ├── passwordhash.h/.cpp    密码哈希(bcrypt 接口预留)
│       ├── storage.h/.cpp         存储抽象 + 内存实现
│       ├── config.h/.cpp          配置加载 + 环境变量覆盖
│       ├── plugins.h/.cpp         防抖限流/黑名单插件
│       └── util.h                 随机数/常量时间比较
├── client-example/                FastAPI 业务应用接入示例
│   ├── app.py
│   └── requirements.txt
├── db/                            PostgreSQL 脚本(生产)
│   ├── 00_create_db.sql
│   └── init.sql                   建表 + bcrypt 初始化数据
├── deploy/
│   ├── auth-center.service        systemd 服务
│   ├── nginx-auth-center.conf     Nginx 反代(宿主机)
│   └── docker/
│       ├── Dockerfile             多阶段构建
│       ├── docker-compose.yml     生产编排(开机自启/崩溃重启)
│       ├── docker-compose.prod.yml PostgreSQL + Redis 附加
│       ├── nginx-auth-center.conf 容器内 Nginx
│       └── .env.example
└── docs/
    ├── DEPLOYMENT.md              Debian/FreeBSD/Docker 部署
    ├── TESTING.md                 浏览器 + curl 测试流程
    └── PRODUCTION-CHECKLIST.md    生产增强建议清单
```

## API 汇总

| 方法 | 路径          | 说明                          | 认证 |
|------|---------------|-------------------------------|------|
| GET  | /             | 首页(登录状态展示用户信息)     | 可选 |
| GET  | /login        | 登录页(支持 redirect 参数)     | 否   |
| POST | /login        | 提交登录(创建会话 Cookie)      | 否   |
| GET  | /authorize    | 授权端点(响应类型仅 code)      | 会话 |
| POST | /token        | 令牌端点(换 JWT)               | 客户端凭据 |
| GET  | /userinfo     | 用户信息                       | Bearer Token |
| GET  | /logout       | 登出(清除会话与 Cookie)        | 可选 |
| GET  | /healthz      | 健康检查(扩展端点)             | 否   |

## 快速开始(测试环境: 无 docker、无数据库)

```bash
# 1. 编译(Debian 示例; 依赖安装见 docs/DEPLOYMENT.md)
sudo apt-get install -y build-essential qt6-base-dev qt6-base-dev-tools libqt6httpserver6
cd auth-center
qmake6 uac.pro CONFIG+=release && make -j"$(nproc)"

# 2. 运行(演示默认密钥仅限开发; 生产必须注入 JWT_SECRET)
export JWT_SECRET=$(openssl rand -hex 32)
./auth-center
# [startup] 监听成功: 127.0.0.1:27149

# 3. 演示账号
# admin / admin123  (角色 admin, user)
# alice / alice123  (角色 user)
# 客户端: client_id=fastapi-app  client_secret=fastapi-app-secret
```

完整测试流程(浏览器 + curl + FastAPI 联调)见 [docs/TESTING.md](docs/TESTING.md)。

## 文档索引

- [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) —— Debian 11/12/13、FreeBSD 15、Docker、systemd、Nginx、环境变量总表
- [docs/TESTING.md](docs/TESTING.md) —— 浏览器流程、curl 全流程、负面测试、FastAPI 联调、验收标准对照
- [docs/PRODUCTION-CHECKLIST.md](docs/PRODUCTION-CHECKLIST.md) —— 上线前必读(RS256/bcrypt/Redis/TLS/MFA…)
