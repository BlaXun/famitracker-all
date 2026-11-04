"""A small MCP client for FamiTracker's connection (MCP.md): JSON-RPC over HTTP POST with the access code."""

import json
import re
import time
import urllib.parse

from .net import (AuthError, Patience, RETRYABLE, Unreachable, get_bytes, never_sent, post_json)

PROTOCOL = "2025-06-18"


class ToolReply:
    """a tool's reply: its text, whether it's an error, and the files it links"""

    def __init__(self, text, is_error=False, content=None, seconds=0.0):
        self.text = text
        self.is_error = is_error
        self.content = content or []
        self.seconds = seconds

    def files(self):
        """the OGG files the reply links: its resource links (2025-06-18), then /files/ links in its text"""
        names = []
        for item in self.content:
            uri = item.get("uri") or (item.get("resource") or {}).get("uri") or ""
            if uri.startswith("famitracker://files/"):
                names.append(urllib.parse.unquote(uri[len("famitracker://files/"):]))
        for match in re.finditer(r"/files/([^\s?\"')]+\.ogg)", self.text):
            names.append(urllib.parse.unquote(match.group(1)))
        out = []
        for name in names:
            name = name.split("\\")[-1].strip()
            if name and name not in out:
                out.append(name)
        return out


class McpClient:
    def __init__(self, url, token, core=False, timeout=900, patience=None):
        self.base = url.rstrip("/")
        for suffix in ("/mcp/core", "/mcp"):
            if self.base.endswith(suffix):
                self.base = self.base[: -len(suffix)]
        self.route = "/mcp/core" if core else "/mcp"
        self.token = token
        self.timeout = timeout
        self.patience = patience or Patience()
        self.next_id = 1
        self.ready = False
        self.instructions = ""

    def headers(self):
        out = {"Accept": "application/json, text/event-stream", "MCP-Protocol-Version": PROTOCOL}
        if self.token:
            out["Authorization"] = "Bearer " + self.token
        return out

    def rpc(self, method, params=None, timeout=None, safe=True):
        """one JSON-RPC request, waiting out a FamiTracker that's down (safe requests) or busy (503).
        Returns the result; raises Unreachable, AuthError or RuntimeError (a JSON-RPC error)"""
        body = {"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}}
        self.next_id += 1
        waited = 0
        delay = 5
        while True:
            try:
                status, _, text = post_json(self.base + self.route, body, self.headers(), timeout or self.timeout)
            except RETRYABLE as error:
                if not safe and not never_sent(error):
                    raise TimeoutError("FamiTracker didn't answer %s in time (%s)" % (method, error))
                waited = self.patience.wait(waited, delay, "FamiTracker can't be reached (%s)" % error)
                delay = min(delay * 2, 120)
                continue
            if status in (401, 403):
                raise AuthError("FamiTracker refused the access code (HTTP %d): %s" % (status, text[:200]))
            if status == 503:
                waited = self.patience.wait(waited, 10, "FamiTracker is busy: %s" % text[:120])
                continue
            if status >= 500 or status == 0:
                waited = self.patience.wait(waited, delay, "FamiTracker answered HTTP %d" % status)
                delay = min(delay * 2, 120)
                continue
            if status >= 400:
                raise RuntimeError("FamiTracker answered HTTP %d: %s" % (status, text[:300]))
            reply = parse_reply(text)
            if "error" in reply:
                raise RuntimeError("MCP error: %s" % reply["error"].get("message", reply["error"]))
            return reply.get("result", {})


    def connect(self):
        result = self.rpc("initialize", {"protocolVersion": PROTOCOL, "capabilities": {},
                                         "clientInfo": {"name": "songsmith", "version": "1"}}, timeout=60)
        self.instructions = result.get("instructions", "")
        self.ready = True
        return result

    def list_tools(self):
        if not self.ready:
            self.connect()
        return self.rpc("tools/list", {}, timeout=60).get("tools", [])

    def call(self, name, arguments=None):
        """a tool call; a timeout after it was sent comes back as an error reply rather than a second call"""
        if not self.ready:
            self.connect()
        started = time.time()
        try:
            result = self.rpc("tools/call", {"name": name, "arguments": arguments or {}}, safe=False)
        except TimeoutError as error:
            self.settle()
            return ToolReply("Error: %s. It may still have changed the song: look with get_song before calling it again."
                             % error, True, [], time.time() - started)
        except RuntimeError as error:
            return ToolReply("Error: %s" % error, True, [], time.time() - started)
        content = result.get("content", [])
        text = "\n".join(item.get("text", "") for item in content if item.get("type") == "text")
        return ToolReply(text, bool(result.get("isError")), content, time.time() - started)

    def settle(self):
        """after a call timed out: wait until FamiTracker answers again (it says 503 while it's still busy)"""
        try:
            self.rpc("tools/list", {}, timeout=60)
        except (RuntimeError, AuthError):
            pass

    def text(self, name, arguments=None):
        """a call whose reply must work: its text, or RuntimeError"""
        reply = self.call(name, arguments)
        if reply.is_error:
            raise RuntimeError("%s: %s" % (name, reply.text[:400]))
        return reply.text

    def list_files(self):
        try:
            result = self.rpc("resources/list", {}, timeout=60)
        except RuntimeError:
            return []
        names = []
        for item in result.get("resources", []):
            uri = item.get("uri", "")
            if uri.startswith("famitracker://files/"):
                names.append(urllib.parse.unquote(uri[len("famitracker://files/"):]))
        return names

    def download(self, name, path):
        """fetch an OGG file next to the song through /files/ into path; True when it came"""
        url = self.base + "/files/" + urllib.parse.quote(name)
        waited = 0
        while True:
            try:
                status, data = get_bytes(url, {"Authorization": "Bearer " + self.token}, self.timeout)
            except RETRYABLE as error:
                waited = self.patience.wait(waited, 10, "FamiTracker can't be reached for the file (%s)" % error)
                continue
            if status == 503:
                waited = self.patience.wait(waited, 10, "FamiTracker is busy")
                continue
            if status != 200 or not data:
                return False
            with open(path, "wb") as f:
                f.write(data)
            return True


def parse_reply(text):
    """a JSON-RPC reply, as JSON or as the last data line of an event stream"""
    text = text.strip()
    if text.startswith("{"):
        return json.loads(text)
    last = None
    for line in text.splitlines():
        if line.startswith("data:"):
            last = line[5:].strip()
    if last:
        return json.loads(last)
    raise RuntimeError("FamiTracker's reply couldn't be read: %s" % text[:200])
