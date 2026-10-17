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
    assert be.WIN == '192.168.1.3' and be.SN == 'SB2610000001'


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


if __name__ == '__main__':
    for t in (test_env_wins, test_defaults, test_reply_checked, test_search_failure_is_an_error, test_changed_lease):
        t()
    print('test_bench_env: ok')
