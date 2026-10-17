"""bench_env: the addresses come from the environment, else a FinderTool search; never a guess (Plan 11b task 1)."""
import importlib
import json
import os
import sys
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def load(env):
    for k in ('SBIOT_GW', 'SBIOT_WIN', 'SBIOT_SN'):
        os.environ.pop(k, None)
    os.environ.update(env)
    import bench_env
    return importlib.reload(bench_env)


def test_env_wins():
    be = load({'SBIOT_GW': '10.0.0.5', 'SBIOT_WIN': '10.0.0.2'})
    assert be.GW == '10.0.0.5' and be.WIN == '10.0.0.2'


def test_defaults():
    be = load({'SBIOT_GW': '192.168.1.9'})
    assert be.WIN == '192.168.0.10' and be.SN == 'SB2610000001'


def test_reply_checked():
    be = load({'SBIOT_GW': '192.168.1.9'})
    ok = {'sbiot': 'disc', 'v': 1, 'op': 'found', 'id': 'abcd0123', 'sn': 'SB2610000001',
          'eth': {'ip': '192.168.1.42', 'netmask': '255.255.255.0'}}
    wide = dict(ok, eth={'ip': '192.168.0.255', 'netmask': '255.255.254.0'})
    assert be.pick([json.dumps(wide)], 'abcd0123', 'SB2610000001') is not None  # a host address in a /23 (Codex)
    assert be.pick(['junk', json.dumps(ok)], 'abcd0123', 'SB2610000001')['eth']['ip'] == '192.168.1.42'
    for bad in (dict(ok, id='ffff0000'), dict(ok, sn='SB2610000002'), dict(ok, op='search'), dict(ok, eth={'ip': '0.0.0.0'}),
                dict(ok, eth={'ip': 'x'}), dict(ok, sbiot='usr'), dict(ok, eth={'ip': '192.168.1.255', 'netmask': '255.255.255.0'}),
                dict(ok, eth={'ip': '224.0.0.1'}), dict(ok, eth={'ip': '192.168.01.42'}), dict(ok, eth={'ip': '192.168.1.٤'})):
        assert be.pick([json.dumps(bad)], 'abcd0123', 'SB2610000001') is None, bad


def test_search_failure_is_an_error():
    be = load({'SBIOT_GW': '192.168.1.9'})
    be.find_board = lambda win, sn: None
    os.environ.pop('SBIOT_GW')
    try:
        be.resolve_gw()
    except RuntimeError as e:
        assert 'SB2610000001' in str(e)
    else:
        raise AssertionError('no fallback address')


def test_changed_lease():
    be = load({'SBIOT_GW': '192.168.1.9'})
    w, bc = types.ModuleType('web_check'), types.ModuleType('bridge_check')
    w.HOST, bc.GW = '192.168.1.9', '192.168.1.9'
    sys.modules['web_check'], sys.modules['bridge_check'] = w, bc
    be.set_board('192.168.1.50')
    assert be.GW == w.HOST == bc.GW == '192.168.1.50'


def test_judge():
    be = load({'SBIOT_GW': '192.168.1.9'})
    ok = {'boot': 5, 'iwdgMaxGapMs': 50, 'wdiMaxGapMs': 500, 'hbMissing': 0, 'poolsFull': ['console_log'],
          'uart': [{'overrun': 0}, {'overrun': 0}]}
    assert be.judge(ok, dict(ok)) == []
    assert any('restarted' in p for p in be.judge(ok, dict(ok, boot=6)))
    assert any('iwdg' in p for p in be.judge(ok, dict(ok, iwdgMaxGapMs=400)))
    assert any('wdi' in p for p in be.judge(ok, dict(ok, wdiMaxGapMs=600)))
    assert any('hbMissing' in p for p in be.judge(ok, dict(ok, hbMissing=1)))
    assert any('overrun' in p for p in be.judge(ok, dict(ok, uart=[{'overrun': 0}, {'overrun': 3}])))
    assert any('tcp_pcb' in p for p in be.judge(ok, dict(ok, poolsFull=['console_log', 'tcp_pcb'])))


def gates_with(be, boots, status):
    """gates() with the search answering `boots` in turn and /api/status answering `status`."""
    seq = iter(boots)
    be.find_board = lambda win, sn: {'boot': next(seq)}
    w = types.ModuleType('web_check')
    w.get_json = lambda path: (200, status)
    sys.modules['web_check'] = w
    return be.gates('t', path=os.devnull)


STATUS = {'system': {'uptime': 9, 'iwdgMaxGapMs': 50, 'wdiMaxGapMs': 500, 'hbMissing': 0, 'heapLargest': 1},
          'mem': {'heap': {}, 'stackMin': {'task': 'IDLE', 'words': 293}, 'uart': [{}, {}], 'pools': []}}


def raises(fn):
    try:
        fn()
    except RuntimeError:
        return True
    return False


def test_gates_one_boot():
    be = load({'SBIOT_GW': '192.168.1.9'})
    assert gates_with(be, [7, 7], STATUS)['boot'] == 7
    assert raises(lambda: gates_with(be, [7, 8], STATUS)), 'a restart between the search and the status (Codex r1)'
    no_pools = {'system': STATUS['system'], 'mem': {k: v for k, v in STATUS['mem'].items() if k != 'pools'}}
    assert raises(lambda: gates_with(be, [7, 7], no_pools)), 'no pools is not "no pool full" (Codex r1)'


if __name__ == '__main__':
    for t in (test_env_wins, test_defaults, test_reply_checked, test_search_failure_is_an_error, test_changed_lease, test_judge,
              test_gates_one_boot):
        t()
    print('test_bench_env: ok')
