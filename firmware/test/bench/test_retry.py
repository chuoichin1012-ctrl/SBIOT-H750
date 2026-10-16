#!/usr/bin/env python3
"""web_check's connect retry, against local servers (no board): only a connect that times out is tried again.

    python3 stm32/firmware/test/bench/test_retry.py
"""
import os
import socket
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_check as w  # noqa: E402


def server(reply):
    """A local server that accepts each connection and sends `reply` (None: never answers)."""
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    s.listen(4)
    held = []

    def run():
        while True:
            c, _ = s.accept()
            held.append(c)
            c.recv(4096)
            if reply:
                c.sendall(reply)
                c.close()
    threading.Thread(target=run, daemon=True).start()
    return s.getsockname()[1]


def main():
    bad = 0
    w.HOST, w.RETRY_PAUSE = '127.0.0.1', 0
    # A connect that times out once: tried again, and the request answered.
    w.PORT = server(b'HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
    real, calls = socket.create_connection, []

    def flaky(addr, timeout=None):
        calls.append(addr)
        if len(calls) == 1:
            raise TimeoutError('timed out')
        return real(addr, timeout)
    socket.create_connection = flaky
    try:
        s, again = w.req_retry('GET', '/')
    finally:
        socket.create_connection = real
    if not (s == 200 and again and len(calls) == 2):
        bad += 1
        print('FAIL a connect timeout: status %s, tried again %s, connects %d' % (s, again, len(calls)))
    # A board that takes the request and never answers: a failure, not tried again.
    w.PORT = server(None)
    try:
        r = w.req_retry('GET', '/', timeout=1)
        bad += 1
        print('FAIL a reply that never comes: returned %s' % (r,))
    except (socket.timeout, TimeoutError):
        pass
    print('test_retry: %s' % ('ok' if not bad else 'FAILED (%d)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
