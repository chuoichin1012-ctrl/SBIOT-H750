"""disc_check.case_noaddr's cleanup (Codex): when FinderTool cannot bring the board back, the console puts the whole
saved network document back, also when the failure comes right after the DHCP save. Mocked; no board needed."""
import json
import os
import sys
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
HOME = {'priority': 0, 'ipMode': 1, 'ip': '192.168.0.7', 'netmask': '255.255.255.0', 'gateway': '192.168.0.1',
        'dnsMode': 1, 'dns1': '8.8.8.8', 'dns2': '1.1.1.1', 'apn': 'internet'}


def run(fail_at):
    import disc_check as dc
    state = {'net': dict(HOME), 'console': []}

    def get_json(path):
        if state['net']['ipMode'] == 0:
            raise OSError(113, 'No route to host')
        return 200, dict(state['net'])

    def req(method, path, body=None, **kw):
        if fail_at == 'save':
            state['net'] = dict(json.loads(body))
            raise OSError(104, 'reset')
        state['net'] = dict(json.loads(body))
        return 200, {}, b'{"ok":true}'

    def listen(secs, sends=(), during=None):
        for _, text in sends:
            if text.startswith('{'):
                state['console'].append(json.loads(text))
                state['net'] = dict(json.loads(text))
        return []

    dc.w = types.SimpleNamespace(get_json=get_json, req=req)
    dc.board = types.SimpleNamespace(listen=listen, hard_reset=lambda: None)
    dc.search = lambda mac=None: [{'sn': dc.SN, 'mac': '02:95:b8:e9:a7:9f', 'boot': 1}]
    dc.command = lambda op, mac, net=None: {'ok': True}
    dc.back = lambda mac, boot, limit=120: None  # FinderTool never sees the board come back
    dc.failures.clear()
    try:
        dc.case_noaddr()
    except Exception:
        pass
    return state


def test_restored_when_never_found():
    st = run('back')
    assert st['console'] and st['console'][-1] == HOME and st['net'] == HOME


def test_restored_when_the_save_fails():
    st = run('save')
    assert st['console'] and st['net'] == HOME


if __name__ == '__main__':
    test_restored_when_never_found()
    test_restored_when_the_save_fails()
    print('test_disc_noaddr: ok')
