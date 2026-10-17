"""net_check's cleanup (Codex r1): when a step leaves the board at an address the bench no longer reaches, the
finally block must still put the whole saved network document back (over the console). Mocked; no board needed."""
import json
import os
import sys
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
HOME = {'priority': 0, 'ipMode': 1, 'ip': '192.168.0.7', 'netmask': '255.255.255.0', 'gateway': '192.168.0.1',
        'dnsMode': 1, 'dns1': '8.8.8.8', 'dns2': '1.1.1.1', 'apn': 'internet'}


def test_restored_when_the_board_is_lost():
    import net_check as nc
    state = {'net': dict(HOME), 'lost': False, 'console': []}

    def get_json(path):
        if state['lost']:
            raise OSError(113, 'No route to host')
        return 200, (dict(state['net']) if path.endswith('network') else {'eth': {}})

    def req(method, path, body=None, **kw):
        state['net'] = dict(json.loads(body), ipMode=1, ip='10.9.9.9')  # saved, and the board moves away
        return 200, {}, b'{"ok":true}'

    def listen(secs, sends=(), during=None):
        if during:
            state['lost'] = state['net'] != HOME
        for _, text in sends:
            if text.startswith('{'):
                state['console'].append(json.loads(text))
                state['net'] = dict(json.loads(text))
        return []

    nc.w = types.SimpleNamespace(get_json=get_json, req=req)
    nc.board = types.SimpleNamespace(listen=listen, hard_reset=lambda: None)
    nc.ping = lambda *a, **k: 0

    def no_flood(*a, **k):  # a host test never sends LAN traffic (Codex r2)
        raise OSError('no flood in a host test')
    nc.subprocess = types.SimpleNamespace(Popen=no_flood, PIPE=-1)
    nc.follow = lambda: None  # the search never finds it at the bench's address
    nc.failures.clear()
    try:
        nc.main()
    except Exception:
        pass
    assert state['console'] and state['console'][-1] == HOME and state['net'] == HOME


if __name__ == '__main__':
    test_restored_when_the_board_is_lost()
    print('test_net_cleanup: ok')
