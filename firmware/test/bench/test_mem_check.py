"""Host tests of mem_check's scenario B evidence: tlsset events to handshake intervals, and their overlap."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mem_check as m  # noqa: E402


def ev(kind, port, t):
    return {'ev': kind, 'port': port, 't': t}


class Handshakes(unittest.TestCase):
    def test_each_accept_ends_at_its_handshake_or_failure(self):
        iv = m.handshakes([ev('accept', 1, 0), ev('accept', 1, 50), ev('hs', 1, 900), ev('hsfail', 1, 950), ev('listen', 0, 0)])
        self.assertEqual(iv, {1: [(0, 900), (50, 950)]})

    def test_three_in_flight_at_once_overlap(self):
        iv = {1: [(0, 900)], 2: [(100, 1200)], 3: [(500, 1300)]}
        self.assertTrue(m.overlapping(iv, (1, 2, 3)))

    def test_one_after_another_do_not(self):
        iv = {1: [(0, 100)], 2: [(200, 300)], 3: [(0, 400)]}
        self.assertFalse(m.overlapping(iv, (1, 2, 3)))

    def test_a_port_never_dialled_does_not(self):
        self.assertFalse(m.overlapping({1: [(0, 900)], 2: [(0, 900)]}, (1, 2, 3)))

    def test_a_retry_that_overlaps_counts(self):
        iv = {1: [(0, 100), (2000, 2900)], 2: [(2100, 3000)], 3: [(2500, 2600)]}
        self.assertTrue(m.overlapping(iv, (1, 2, 3)))


class Criteria(unittest.TestCase):
    ST = {'system': {'heapMin': 200000, 'heapLargest': 150000},
          'mem': {'stackMin': {'task': 'IDLE', 'words': 293}, 'pools': [{'name': 'tcp_pcb', 'used': 1, 'size': 8}]}}

    def test_a_good_read_passes(self):
        self.assertEqual(m.criteria(self.ST, 'B'), [])

    def test_a_failed_read_cannot_pass(self):  # Codex r2: a 500 under memory pressure is not a pass
        self.assertTrue(m.criteria(None, 'B'))

    def test_missing_fields_cannot_pass(self):
        for st in ({'system': self.ST['system']}, {'mem': self.ST['mem']},
                   {'system': self.ST['system'], 'mem': {'stackMin': self.ST['mem']['stackMin']}}):
            self.assertTrue(m.criteria(st, 'B'), st)


if __name__ == '__main__':
    unittest.main()
