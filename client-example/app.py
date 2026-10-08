"""
uac 统一认证中心 —— FastAPI 业务系统接入示例。

OAuth2 授权码模式接入流程:
  1. 用户访问本应用 /login  ->  302 跳转认证中心 /authorize
  2. 认证中心校验会话/授权  ->  302 跳回本应用 /callback?code=xxx&state=xxx
  3. /callback 后端用授权码向认证中心 /token 换取 JWT(不经浏览器)
  4. 前端携带 access_token 访问 /protected、/protected/admin

运行(测试环境, 与认证中心直连):
  pip install -r requirements.txt
  export JWT_SECRET=dev-only-insecure-secret-change-me   # 与认证中心一致
  uvicorn app:app --host 127.0.0.1 --port 8000

生产环境: AUTH_BASE 指向 https://auth.your-domain.com; 签名建议改用 RS256 公钥验签。
"""

import os
from typing import Optional
from urllib.parse import urlencode

import httpx
import jwt as pyjwt
from fastapi import Depends, FastAPI, HTTPException, Request
from fastapi.responses import RedirectResponse

# ---------------- 配置(生产环境全部通过环境变量注入) ----------------
AUTH_BASE = os.getenv("AUTH_BASE", "http://127.0.0.1:27149")            # 认证中心地址
CLIENT_ID = os.getenv("APP_CLIENT_ID", "fastapi-app")
CLIENT_SECRET = os.getenv("APP_CLIENT_SECRET", "fastapi-app-secret")
# 回调地址必须已登记在认证中心客户端白名单(redirect_uris)中
REDIRECT_URI = os.getenv("APP_REDIRECT_URI", "http://127.0.0.1:8000/callback")
# HS256 验签密钥, 必须与认证中心 JWT_SECRET 一致; 生产建议 RS256 公钥验签
JWT_SECRET = os.getenv("JWT_SECRET", "dev-only-insecure-secret-change-me")
JWT_ALGORITHMS = os.getenv("JWT_ALGORITHMS", "HS256").split(",")

app = FastAPI(title="uac demo business app", version="1.0.0")


@app.get("/")
def index():
    return {
        "应用": "uac 业务系统接入示例",
        "登录入口": "/login (跳转认证中心)",
        "受保护接口": "/protected (Authorization: Bearer <token>)",
        "管理员接口": "/protected/admin (仅 admin 角色)",
    }


@app.get("/login")
def login():
    """跳转认证中心授权端点(授权码模式)。"""
    params = {
        "client_id": CLIENT_ID,
        "redirect_uri": REDIRECT_URI,
        "response_type": "code",
        "scope": "openid profile",
        "state": "demo-state",  # 生产必须使用随机 state 并在回调时校验, 防 CSRF
    }
    return RedirectResponse(f"{AUTH_BASE}/authorize?{urlencode(params)}")


@app.get("/callback")
async def callback(request: Request, code: str, state: Optional[str] = None):
    """认证中心回调: 后端用授权码换令牌(授权码一次性, 勿重复使用)。"""
    if state != "demo-state":
        raise HTTPException(status_code=400, detail="state 校验失败")

    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.post(
            f"{AUTH_BASE}/token",
            data={
                "grant_type": "authorization_code",
                "code": code,
                "redirect_uri": REDIRECT_URI,
                "client_id": CLIENT_ID,
                "client_secret": CLIENT_SECRET,
            },
        )
    if resp.status_code != 200:
        raise HTTPException(status_code=400, detail=f"令牌交换失败: {resp.status_code} {resp.text}")

    tokens = resp.json()
    try:
        # pyjwt 默认校验签名与 exp 有效期
        claims = pyjwt.decode(tokens["access_token"], JWT_SECRET, algorithms=JWT_ALGORITHMS)
    except pyjwt.PyJWTError as exc:
        raise HTTPException(status_code=401, detail=f"JWT 校验失败: {exc}")

    return {
        "access_token": tokens["access_token"],
        "token_type": tokens["token_type"],
        "expires_in": tokens["expires_in"],
        "claims": claims,
        "提示": "将 access_token 放入 Authorization: Bearer <token> 请求头访问 /protected",
    }


def get_current_user(request: Request) -> dict:
    """FastAPI 依赖: 从 Authorization 头解析并校验 JWT(签名 + 有效期)。"""
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="缺少 Bearer 令牌",
                            headers={"WWW-Authenticate": "Bearer"})
    token = auth[len("Bearer "):].strip()
    try:
        return pyjwt.decode(token, JWT_SECRET, algorithms=JWT_ALGORITHMS)
    except pyjwt.PyJWTError as exc:
        raise HTTPException(status_code=401, detail=f"令牌无效: {exc}",
                            headers={"WWW-Authenticate": "Bearer"})


@app.get("/protected")
def protected(user: dict = Depends(get_current_user)):
    """任意已认证用户可访问。"""
    return {
        "message": "欢迎",
        "sub": user.get("sub"),
        "name": user.get("name"),
        "roles": user.get("roles"),
    }


@app.get("/protected/admin")
def admin_only(user: dict = Depends(get_current_user)):
    """仅 admin 角色可访问。"""
    if "admin" not in user.get("roles", []):
        raise HTTPException(status_code=403, detail="需要 admin 角色")
    return {"message": "管理员面板", "user": user}
