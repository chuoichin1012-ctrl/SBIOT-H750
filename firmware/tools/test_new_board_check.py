"""new_board_check.judge on console lines of a new board, a reworked one and failed starts. No board needed."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import new_board_check as nb  # noqa: E402

NEW = ['SBIOT-H750 boot 0.7.0', 'boot: qspi id c84016', 'bind: ok', 'boot: install seeded-factory',
       'boot: jump msp 24080000 pc 9000xxxx', 'SBIOT-H750 app 0.7.0 rsr=00460000', 'app: SN SB2610000002, MAC 02:..',
       'app: store mounted in 890 ms, boot 1, heap free 360440']


class Judge(unittest.TestCase):
    def test_a_new_board_passes(self):
        self.assertTrue(all(nb.judge(NEW).values()))

    def test_a_reworked_board_is_stopped(self):
        for install in ('boot: install', 'boot: install installed-staging', 'boot: install seeded-factory installed-staging'):
            self.assertFalse(all(nb.judge([install if l.startswith('boot: install') else l for l in NEW]).values()), install)

    def test_a_failed_start_is_stopped(self):
        for bad in (['app: anti-clone record INVALID, nothing started'], ['app: store FAILED in 10 ms'],
                    ['boot: no valid image, halted']):
            self.assertFalse(all(nb.judge(NEW + bad).values()), bad)
        self.assertFalse(all(nb.judge([l for l in NEW if not l.startswith('app: store')]).values()), 'no store line')
        self.assertFalse(all(nb.judge([l for l in NEW if l != 'bind: ok']).values()), 'no bind line')


if __name__ == '__main__':
    unittest.main()
