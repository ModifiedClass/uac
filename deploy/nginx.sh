mkdir -p /etc/docker/nginx/logs
chown -R 101:101 /etc/docker/nginx/logs
chmod 755 /etc/docker/nginx/logs

tee /etc/docker/nginx/uac.conf << 'EOF'
# ---------- 真实 IP 识别 ----------
# 如果你的 Nginx 前面还有一层代理/负载均衡（例如 F5、LVS、另一台 Nginx），
# 需要在这里声明可信代理 IP 段，Nginx 才能从 X-Forwarded-For 中提取真实客户端 IP。
# 若 Nginx 直接对外，可只保留 set_real_ip_from 127.0.0.1; 或全部注释掉。
set_real_ip_from 127.0.0.1;
set_real_ip_from 192.168.0.0/16;
set_real_ip_from 10.0.0.0/8;
set_real_ip_from 172.16.0.0/12;
real_ip_header X-Forwarded-For;
real_ip_recursive on;

# ---------- 上游应用 ----------
upstream auth_center {
    server 192.168.201.189:27149 max_fails=3 fail_timeout=10s;
    keepalive 32;
    keepalive_requests 1000;
    keepalive_timeout 60s;
}

# ---------- 日志格式（可选，增强真实 IP 记录）----------
log_format uac_main '$remote_addr - $remote_user [$time_local] "$request" '
                '$status $body_bytes_sent "$http_referer" '
                '"$http_user_agent" "$http_x_forwarded_for" '
                'real_ip=$remote_addr';

server {
    listen 80;
    server_name 192.168.201.189 auth.your-domain.com _;

    # ---------- 安全响应头 ----------
    add_header X-Content-Type-Options nosniff always;
    add_header X-Frame-Options DENY always;
    add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;
    add_header Referrer-Policy "strict-origin-when-cross-origin" always;
    add_header X-XSS-Protection "1; mode=block" always;

    # ---------- 日志 ----------
    access_log /var/log/nginx/auth-access.log uac_main;
    error_log  /var/log/nginx/auth-error.log warn;

    # ---------- 反向代理 ----------
    location / {
        proxy_pass http://auth_center;
        proxy_http_version 1.1;

        # ---------- 真实 IP 与请求头传递 ----------
        proxy_set_header Host              $host;
        proxy_set_header X-Real-IP         $remote_addr;
        proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header X-Forwarded-Host  $host;
        proxy_set_header X-Forwarded-Port  $server_port;
        proxy_set_header X-Forwarded-Server $host;

        # 传递原始客户端信息（可选）
        proxy_set_header X-Client-IP       $remote_addr;
        proxy_set_header X-Original-URI    $request_uri;
        proxy_set_header X-Original-Method $request_method;

        # ---------- 其他常用头 ----------
        proxy_set_header User-Agent        $http_user_agent;
        proxy_set_header Referer           $http_referer;
        proxy_set_header Accept-Encoding   "";

        # ---------- 长连接 ----------
        proxy_set_header Connection        "";

        # ---------- 超时 ----------
        proxy_connect_timeout 10s;
        proxy_send_timeout    30s;
        proxy_read_timeout    30s;

        proxy_next_upstream error timeout http_502 http_503 http_504;
    }
}
EOF


docker service create \
  --name uac-nginx \
  --replicas 1 \
  --constraint 'node.hostname == manager1' \
  --network uac-network \
  --publish published=80,target=80,mode=host \
  --mount type=bind,source=/etc/docker/nginx/uac.conf,target=/etc/nginx/conf.d/uac.conf,readonly \
  --mount type=bind,source=/etc/docker/nginx/logs,target=/var/log/nginx \
  --with-registry-auth \
  192.168.201.194:5000/nginx:1.28
