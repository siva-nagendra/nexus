"""
ClaudeShell Relay v2 — WebSocket + HTTP server with ConPTY.

Single-port server using the ``websockets`` library:
  - HTTP: serves static files (terminal.html, JS, CSS) and /status endpoint
  - WebSocket: real-time bidirectional terminal I/O

Usage:
    python relay.py --port 19220 --web-dir ../unreal_plugin/ClaudeShell/Content/Web --cwd "D:/ue_data/MyProject"
"""

import argparse
import asyncio
import base64
import json
import mimetypes
import os
import threading
import time

from winpty import PtyProcess
from websockets.asyncio.server import serve as ws_serve
from websockets.datastructures import Headers
from websockets.http11 import Response

# ---------------------------------------------------------------------------
# Globals
# ---------------------------------------------------------------------------
pty_process = None
exit_code = None
scrollback_buffer = bytearray()
SCROLLBACK_MAX = 65536
scrollback_lock = threading.Lock()
ws_clients = set()
_event_loop = None

WEB_DIR = ""
WORKING_DIR = ""
CLAUDE_EXE = ""


# ---------------------------------------------------------------------------
# Claude executable discovery
# ---------------------------------------------------------------------------
def find_claude():
    """Locate the claude executable."""
    candidates = [
        r"C:\ProgramData\chocolatey\bin\claude.exe",
        os.path.expanduser(r"~\.claude\local\claude.exe"),
        os.path.expanduser(r"~\AppData\Roaming\npm\claude.cmd"),
        os.path.expanduser(r"~\AppData\Local\Programs\claude\claude.exe"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return "claude"  # fallback to PATH


# ---------------------------------------------------------------------------
# PTY management
# ---------------------------------------------------------------------------
def start_pty():
    """Start Claude CLI inside a ConPTY."""
    global pty_process, exit_code

    exit_code = None

    env = os.environ.copy()
    env["FORCE_COLOR"] = "1"
    env["TERM"] = "xterm-256color"

    try:
        pty_process = PtyProcess.spawn(
            CLAUDE_EXE,
            cwd=WORKING_DIR,
            dimensions=(24, 80),
        )
        print(f"[relay] Claude PTY started (PID: {pty_process.pid})")

        reader = threading.Thread(target=_read_pty, daemon=True)
        reader.start()

    except Exception as e:
        print(f"[relay] Error starting PTY: {e}")
        err_msg = f"\r\n\x1b[31m[Error starting claude: {e}]\x1b[0m\r\n"
        raw = err_msg.encode("utf-8")
        with scrollback_lock:
            scrollback_buffer.extend(raw)
        _schedule_broadcast(json.dumps({
            "type": "output",
            "data": base64.b64encode(raw).decode("ascii"),
        }))
        exit_code = -1


def _read_pty():
    """Background thread: read PTY output, broadcast to WebSocket clients."""
    global exit_code
    try:
        while pty_process is not None and pty_process.isalive():
            try:
                data = pty_process.read(4096)
                if data:
                    raw = data.encode("utf-8") if isinstance(data, str) else data
                    with scrollback_lock:
                        scrollback_buffer.extend(raw)
                        if len(scrollback_buffer) > SCROLLBACK_MAX:
                            scrollback_buffer[:] = scrollback_buffer[-SCROLLBACK_MAX:]
                    _schedule_broadcast(json.dumps({
                        "type": "output",
                        "data": base64.b64encode(raw).decode("ascii"),
                    }))
            except EOFError:
                break
            except Exception:
                time.sleep(0.01)
    except Exception:
        pass
    finally:
        if pty_process is not None:
            try:
                exit_code = pty_process.exitstatus or 0
            except Exception:
                exit_code = -1
        print(f"[relay] Claude exited with code {exit_code}")
        _schedule_broadcast(json.dumps({"type": "exit", "code": exit_code}))


def stop_pty():
    """Stop the PTY process."""
    global pty_process
    if pty_process is not None:
        try:
            if pty_process.isalive():
                pty_process.terminate(force=True)
        except Exception:
            pass
        pty_process = None


def _schedule_broadcast(msg: str):
    """Thread-safe: schedule a broadcast on the asyncio event loop."""
    if _event_loop and not _event_loop.is_closed():
        asyncio.run_coroutine_threadsafe(_broadcast(msg), _event_loop)


async def _broadcast(msg: str):
    """Send message to all connected WebSocket clients."""
    dead = set()
    for ws in ws_clients:
        try:
            await ws.send(msg)
        except Exception:
            dead.add(ws)
    ws_clients -= dead


# ---------------------------------------------------------------------------
# WebSocket handler
# ---------------------------------------------------------------------------
async def ws_handler(websocket):
    """Handle a single WebSocket connection."""
    ws_clients.add(websocket)
    print(f"[relay] WebSocket client connected ({len(ws_clients)} total)")

    try:
        # Send scrollback snapshot on connect
        with scrollback_lock:
            sb = bytes(scrollback_buffer)
        if sb:
            await websocket.send(json.dumps({
                "type": "scrollback",
                "data": base64.b64encode(sb).decode("ascii"),
            }))

        # Send current status
        alive = pty_process is not None and pty_process.isalive()
        await websocket.send(json.dumps({
            "type": "status",
            "running": alive,
            "pid": pty_process.pid if pty_process else None,
            "cwd": WORKING_DIR,
        }))

        # Handle incoming messages
        async for raw_msg in websocket:
            try:
                msg = json.loads(raw_msg)
                msg_type = msg.get("type", "")

                if msg_type == "input":
                    if pty_process and pty_process.isalive():
                        pty_process.write(msg.get("data", ""))

                elif msg_type == "resize":
                    if pty_process and pty_process.isalive():
                        rows = msg.get("rows", 24)
                        cols = msg.get("cols", 80)
                        pty_process.setwinsize(rows, cols)

                elif msg_type == "restart":
                    stop_pty()
                    await asyncio.sleep(0.3)
                    start_pty()

                elif msg_type == "ping":
                    await websocket.send(json.dumps({"type": "pong"}))

            except json.JSONDecodeError:
                pass

    except Exception as e:
        print(f"[relay] WebSocket error: {e}")
    finally:
        ws_clients.discard(websocket)
        print(f"[relay] WebSocket client disconnected ({len(ws_clients)} remaining)")


# ---------------------------------------------------------------------------
# HTTP handler (websockets 14 API)
# ---------------------------------------------------------------------------
def _http_response(status_code, content_type, body):
    """Build a websockets.http11.Response for HTTP responses."""
    if isinstance(body, str):
        body = body.encode("utf-8")
    hdrs = Headers({
        "Content-Type": content_type,
        "Content-Length": str(len(body)),
        "Access-Control-Allow-Origin": "*",
        "Cache-Control": "no-cache, no-store, must-revalidate",
    })
    reason = "OK" if status_code == 200 else "Error"
    return Response(status_code, reason, hdrs, body)


async def process_request(connection, request):
    """Handle non-WebSocket HTTP requests (websockets 14 API).

    Returns a Response for HTTP requests.
    Returns None to proceed with WebSocket upgrade.
    """
    path = request.path

    # Let WebSocket upgrade requests pass through
    if request.headers.get("Upgrade", "").lower() == "websocket":
        return None

    # API: /status
    if path == "/status":
        alive = pty_process is not None and pty_process.isalive()
        body = json.dumps({
            "running": alive,
            "pid": pty_process.pid if pty_process else None,
            "cwd": WORKING_DIR,
        })
        return _http_response(200, "application/json", body)

    # API: /scrollback
    if path == "/scrollback":
        with scrollback_lock:
            data = bytes(scrollback_buffer)
        body = json.dumps({
            "data": base64.b64encode(data).decode("ascii") if data else "",
        })
        return _http_response(200, "application/json", body)

    # Static files
    serve_path = path
    if serve_path in ("", "/"):
        serve_path = "/terminal.html"

    # Strip query string
    serve_path = serve_path.split("?")[0]

    file_path = os.path.normpath(os.path.join(WEB_DIR, serve_path.lstrip("/")))
    if not file_path.startswith(os.path.normpath(WEB_DIR)):
        return _http_response(403, "text/plain", "Forbidden")

    if os.path.isfile(file_path):
        ct, _ = mimetypes.guess_type(file_path)
        with open(file_path, "rb") as f:
            content = f.read()
        return _http_response(200, ct or "application/octet-stream", content)

    return _http_response(404, "text/plain", "Not Found")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
async def async_main(port):
    """Run the WebSocket+HTTP server."""
    global _event_loop
    _event_loop = asyncio.get_running_loop()

    async with ws_serve(
        ws_handler,
        "127.0.0.1",
        port,
        process_request=process_request,
        max_size=2**20,  # 1 MB max message
        ping_interval=30,
        ping_timeout=10,
    ) as server:
        print(f"[relay] Server listening on http://127.0.0.1:{port}")
        print(f"[relay] WebSocket at ws://127.0.0.1:{port}")
        await server.serve_forever()


def main():
    global WEB_DIR, CLAUDE_EXE, WORKING_DIR

    parser = argparse.ArgumentParser(description="ClaudeShell Relay v2")
    parser.add_argument("--port", type=int, default=19220)
    parser.add_argument("--web-dir", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "unreal_plugin", "ClaudeShell", "Content", "Web"))
    parser.add_argument("--cwd", default=os.getcwd())
    parser.add_argument("--claude", default="")
    args = parser.parse_args()

    WEB_DIR = os.path.abspath(args.web_dir)
    CLAUDE_EXE = args.claude or find_claude()
    WORKING_DIR = args.cwd

    # Write per-project state file
    state_dir = os.path.join(WORKING_DIR, ".claudeshell")
    os.makedirs(state_dir, exist_ok=True)
    state_file = os.path.join(state_dir, "relay.json")
    state = {
        "port": args.port,
        "pid": os.getpid(),
        "project_dir": WORKING_DIR,
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    with open(state_file, "w") as f:
        json.dump(state, f, indent=2)

    print(f"[relay] PID:       {os.getpid()}")
    print(f"[relay] Port:      {args.port}")
    print(f"[relay] Web dir:   {WEB_DIR}")
    print(f"[relay] Claude:    {CLAUDE_EXE}")
    print(f"[relay] Working:   {WORKING_DIR}")
    print(f"[relay] State:     {state_file}")

    # Start the PTY
    start_pty()

    # Run async server
    try:
        asyncio.run(async_main(args.port))
    except KeyboardInterrupt:
        print("\n[relay] Shutting down...")
    finally:
        stop_pty()
        try:
            os.remove(state_file)
        except OSError:
            pass


if __name__ == "__main__":
    main()
