# WebSocket 局域网通信测试（A/B 两机）

## 目的
验证两台机器在同一局域网内：
- TCP 端口可达（8766）
- WebSocket 握手成功
- WebSocket 双向收发正常（echo 回显）

## 机器信息（示例）
- A 机（服务端）：192.168.31.246
- B 机（客户端）：192.168.31.199
- 端口：8766

## 前置条件
- A/B 都装有 Python（建议用 `py` 命令可用）
- B 机安装 websockets（如未安装）：
  - `py -m pip install websockets`

## A 机要做什么（启动 WebSocket Echo 服务）
1) 写入并启动 echo 服务脚本（监听 0.0.0.0:8766）：

```powershell
New-Item -ItemType Directory -Path C:\Temp -Force | Out-Null

@'
import asyncio
import websockets

async def echo(ws):
    ra = ws.remote_address
    print(f"[OPEN] {ra}")
    try:
        async for msg in ws:
            print(f"[RECV] {ra} len={len(msg) if hasattr(msg,'__len__') else '??'}")
            await ws.send(msg)
    except websockets.ConnectionClosed as e:
        print(f"[CLOSE] {ra} code={e.code} reason={e.reason!r}")
    except Exception as e:
        print(f"[ERR] {ra} {e!r}")

async def main():
    async with websockets.serve(echo, "0.0.0.0", 8766):
        print("WS LISTEN 0.0.0.0:8766 (echo)")
        await asyncio.Future()

asyncio.run(main())
'@ | Set-Content -Encoding UTF8 C:\Temp\ws_echo.py

py C:\Temp\ws_echo.py
```

2) 可选：确认正在监听（会看到 LISTENING + PID）：

```powershell
netstat -ano | findstr ":8766"
```

A 机预期输出（示例）：
- `WS LISTEN 0.0.0.0:8766 (echo)`
- 当 B 机连入并发送消息后：
  - `[OPEN] ('192.168.31.199', xxxxx)`
  - `[RECV] ... len=5`

## B 机要做什么（连接并验证回显）
### 1) 先做 TCP 端口可达性测试（推荐）
```powershell
Test-NetConnection 192.168.31.246 -Port 8766
```
预期：
- `TcpTestSucceeded : True`

### 2) 再做 WebSocket 收发测试（echo 回显）
用系统临时目录写脚本（不依赖 C:\Temp）：

```powershell
$fp = Join-Path $env:TEMP ws_client.py

@'
import asyncio
import websockets

async def main():
    uri = "ws://192.168.31.246:8766/"
    async with websockets.connect(uri) as ws:
        await ws.send("hello")
        print("recv:", await ws.recv())

asyncio.run(main())
'@ | Set-Content -Encoding UTF8 $fp

py $fp
```

B 机预期输出：
- `recv: hello`

## 结论判定
- 若 B 机 `recv: hello`，且 A 机看到 `[OPEN]`、`[RECV] len=5`：
  - 说明 A↔B 局域网连通、8766 端口可达、WebSocket 协议层收发正常
- 若 `Test-NetConnection` 为 False：
  - 优先排查防火墙/网络隔离/端口监听是否存在
- 若 Python 报 `ModuleNotFoundError: websockets`：
  - 在 B 机执行：`py -m pip install websockets`