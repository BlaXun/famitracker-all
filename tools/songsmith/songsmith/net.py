"""HTTP with patience: waiting out a server that is down or busy, without ever waiting past a stop."""

import http.client
import json
import socket
import time
import urllib.error
import urllib.request


class Stopped(Exception):
    """the queue was paused or the job cancelled while waiting"""


class Unreachable(Exception):
    """a server stayed down or busy longer than the settings allow"""


class AuthError(Exception):
    """a server refused the access code or key"""


class Patience:
    """how long to wait for a server, and how to hear a stop while waiting"""

    def __init__(self, wait_minutes=120, should_stop=None, log=None, sleep=time.sleep):
        self.wait_seconds = wait_minutes * 60
        self.should_stop = should_stop or (lambda: False)
        self.log = log or (lambda text: None)
        self.sleep = sleep

    def pause(self, seconds):
        """sleep a while, a second at a time, so a stop is heard"""
        slept = 0.0
        while slept < seconds:
            if self.should_stop():
                raise Stopped()
            step = min(1.0, seconds - slept)
            self.sleep(step)
            slept += step
        if self.should_stop():
            raise Stopped()

    def wait(self, waited, seconds, why):
        """one wait for a server, counted against the settings' patience: the seconds waited so far after it"""
        if waited + seconds > self.wait_seconds:
            raise Unreachable(why)
        self.log("%s; trying again in %d s" % (why, seconds))
        self.pause(seconds)
        return waited + seconds


def never_sent(error):
    """True when the request surely never reached the server (so sending it again is safe)"""
    reason = getattr(error, "reason", error)
    if isinstance(reason, (ConnectionRefusedError, socket.gaierror)):
        return True
    if isinstance(reason, OSError) and getattr(reason, "errno", None) in (101, 111, 113, 10061, 10051, 10065):
        return True
    return False


def post_json(url, body, headers, timeout):
    """POST a JSON body; the reply's status, headers and text"""
    data = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(url, data=data, method="POST", headers=dict(headers, **{
        "Content-Type": "application/json"}))
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, dict(response.headers), response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        text = error.read().decode("utf-8", "replace") if error.fp else ""
        return error.code, dict(error.headers or {}), text


def get_bytes(url, headers, timeout):
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read() if error.fp else b""


RETRYABLE = (urllib.error.URLError, ConnectionError, socket.timeout, TimeoutError, http.client.HTTPException, OSError)
