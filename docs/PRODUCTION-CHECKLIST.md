# uac 认证中心 生产环境增强建议清单

上线前逐项确认; 示例实现为满足验收标准的最小实现, 以下各项为生产必备或强烈建议。

## 密码与密钥

- [ ] **密码哈希替换为 bcrypt/argon2** —— 示例为 SHA-256+盐(快速哈希, 不抗爆破)。
      实现 `passwordhash.cpp` 中的 `bcryptHash/bcryptVerify`(libsodium argon2 或 OpenBSD bcrypt),
      并支持校验历史 `sha256$` 格式以平滑迁移; DB 侧用 pgcrypto 的 `crypt()/gen_salt('bf')`(见 db/init.sql)。
- [ ] **JWT 改用 RS256** —— 引入 jwt-cpp(header-only), 认证中心持私钥签发, 业务系统只持公钥验签,
      避免共享对称密钥扩散; 令牌头加入 `kid` 支持密钥轮换。
- [ ] **JWT_SECRET/私钥管理** —— 通过环境变量/密钥管理服务注入, 禁止入库入 git; 定期轮换并支持双密钥平滑过渡。
- [ ] 客户端 secret 存储哈希化; 弱 secret 强制更换。

## 存储与高可用

- [ ] **用户/客户端 -> PostgreSQL**(db/init.sql 已备); **授权码/会话 -> Redis**;
      实现 `storage.h` 中四个接口的 PG/Redis 版本, 并在 `AuthServer::init()` 切换。
- [ ] 多副本部署: 会话/授权码外置 Redis 后, Nginx upstream 配置多实例 + keepalive; 配置健康检查 `/healthz`。
- [ ] 数据库连接池、索引维护、备份与恢复演练; Redis 持久化(AOF)+ 主从/哨兵。

## 传输与边界安全

- [ ] **全站 HTTPS**(certbot), Nginx 打开 `Strict-Transport-Security`(仅 HTTPS 时), 80 跳 443。
- [ ] 安全头已双层配置(Nginx + 应用 afterRequest): `X-Content-Type-Options` / `X-Frame-Options` / `Referrer-Policy`。
- [ ] 会话 Cookie 保持 HttpOnly + SameSite; 评估 `Secure` 标志(HTTPS 下必加)。
- [ ] 认证中心仅监听回环/内网, 不直接暴露公网; 防火墙收紧(仅 80/443)。
- [ ] 若认证中心必须直连可信代理之外, 移除对 X-Forwarded-For 的信任逻辑(authserver.cpp `clientIp()`)。

## 协议与接入安全

- [ ] 公开客户端/前端换码场景启用 **PKCE**(授权码 + code_challenge/code_verifier)。
- [ ] 业务侧 state 使用随机值并校验(示例中为固定 demo-state, 生产必须替换)。
- [ ] redirect_uri 保持精确匹配(已实现), 禁止通配符。
- [ ] 令牌吊销: 黑名单从配置文件迁移到 Redis(支持动态吊销、按 jti 记录), 或缩短 TTL + refresh token。
- [ ] 评估 refresh token 与令牌绑定(binding to client/user)。

## 防滥用与监控

- [ ] 登录防抖限流(rate_limit 插件)阈值按业务调优; 前端增加验证码/WAF 兜底。
- [ ] **MFA**: 对 admin 角色启用 TOTP(如 oathtool/authenticator 库)或短信二次验证。
- [ ] 日志: 确认无密码/密钥/完整令牌输出(已按此设计); 集中采集(如 Loki/ELK)、脱敏、保留策略。
- [ ] 监控告警: /healthz 存活探测、登录失败率、令牌签发量、P95 延迟、进程内存(内存存储上限)。
- [ ] 定期 CVE 跟踪(Qt、Nginx、基础镜像), 更新镜像并重建。

## 其他

- [ ] 数据库初始化账号使用最小权限; 定期清理 auth_codes/sessions 过期数据(pg_cron, 见 db/init.sql)。
- [ ] 崩溃自启已由 systemd `Restart=on-failure` / compose `restart: unless-stopped` 覆盖; 验证 KillMode 与优雅退出。
- [ ] 压测(如 wrk/ab)确认限流与 keepalive 配置满足目标 QPS。
- [ ] 制定密钥泄露应急流程(全局吊销 + 轮换 + 强制下线会话)。
