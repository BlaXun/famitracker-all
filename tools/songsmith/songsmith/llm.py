"""An OpenAI-compatible chat client (llama.cpp's llama-server or llama-swap, with --jinja for tools)."""

import json
import re
import time

from .net import AuthError, Patience, RETRYABLE, Unreachable, post_json


class ModelClient:
    def __init__(self, base_url, model, api_key="", timeout=900, temperature=0.6, max_tokens=4096, extra=None,
                 patience=None):
        self.base = base_url.rstrip("/")
        if self.base.endswith("/chat/completions"):
            self.base = self.base[: -len("/chat/completions")]
        self.model = model
        self.api_key = api_key
        self.timeout = timeout
        self.temperature = temperature
        self.max_tokens = max_tokens
        self.extra = extra or {}
        self.patience = patience or Patience()

    def headers(self):
        out = {"Accept": "application/json"}
        if self.api_key:
            out["Authorization"] = "Bearer " + self.api_key
        return out

    def chat(self, messages, tools=None):
        """one completion: (message dict, usage dict). Waits out a server that's down or loading (503); a request
        that timed out is sent again twice, then raises"""
        body = {"model": self.model, "messages": messages, "temperature": self.temperature,
                "max_tokens": self.max_tokens, "stream": False}
        if tools:
            body["tools"] = tools
            body["tool_choice"] = "auto"
        body.update(self.extra)
        waited = 0
        delay = 5
        timeouts = 0
        while True:
            try:
                status, _, text = post_json(self.base + "/chat/completions", body, self.headers(), self.timeout)
            except RETRYABLE as error:
                if isinstance(error, (TimeoutError,)) or "timed out" in str(error):
                    timeouts += 1
                    if timeouts > 2:
                        raise Unreachable("the model didn't answer within %d s, three times" % self.timeout)
                waited = self.patience.wait(waited, delay, "the model can't be reached (%s)" % error)
                delay = min(delay * 2, 120)
                continue
            if status in (401, 403):
                raise AuthError("the model server refused the key (HTTP %d)" % status)
            if status in (429, 502, 503, 504) or status >= 500:
                waited = self.patience.wait(waited, delay, "the model server answered HTTP %d (%s)" % (status, text[:120]))
                delay = min(delay * 2, 120)
                continue
            if status >= 400:
                raise RuntimeError("the model server answered HTTP %d: %s" % (status, text[:500]))
            try:
                reply = json.loads(text)
                message = reply["choices"][0]["message"]
            except (ValueError, KeyError, IndexError):
                waited = self.patience.wait(waited, delay, "the model's reply couldn't be read: %s" % text[:120])
                continue
            return message, reply.get("usage") or {}


    def models(self):
        """the model names the server lists (a test of the connection)"""
        import urllib.request
        request = urllib.request.Request(self.base + "/models", headers=self.headers())
        with urllib.request.urlopen(request, timeout=30) as response:
            data = json.loads(response.read().decode("utf-8", "replace"))
        return [m.get("id", "") for m in data.get("data", [])]


def strip_thinking(text):
    """a reply's words without <think> blocks"""
    if not text:
        return ""
    text = re.sub(r"<think>.*?</think>", "", text, flags=re.S)
    if "<think>" in text:
        text = text.split("<think>")[0]
    return text.strip()


def tool_schema(tool):
    """an MCP tool as an OpenAI function"""
    return {"type": "function", "function": {
        "name": tool["name"], "description": (tool.get("description") or "")[:1500],
        "parameters": tool.get("inputSchema") or {"type": "object", "properties": {}}}}
