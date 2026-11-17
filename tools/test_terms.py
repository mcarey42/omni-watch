#!/usr/bin/env python3
"""Host tests for tools/terms.py: how the tools find the private term modules.

Run from the repo root: `python -m unittest tools/test_terms.py` (ctest's test_terms). Scratch repos only, and every
module written here is a neutral fixture: no real term is written anywhere.
"""
import os
import shutil
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import terms  # noqa: E402


class Terms(unittest.TestCase):
    def setUp(self):
        self.repo = tempfile.mkdtemp(prefix='terms-test-')
        os.makedirs(os.path.join(self.repo, 'tools'))

    def tearDown(self):
        shutil.rmtree(self.repo, ignore_errors=True)

    def put(self, rel, text):
        with open(os.path.join(self.repo, rel), 'w') as f:
            f.write(text)

    def test_neutral_mark(self):
        self.assertEqual(terms.NEUTRAL_MARK, 'zzneutralmarkzz')

    def test_private_requires_module(self):
        self.assertFalse(terms.public_tree(self.repo))
        with self.assertRaises(terms.TermsMissing) as cm:
            terms.load(self.repo, 'x_terms')
        self.assertIn('tools/x_terms.py', str(cm.exception))

    def test_public_tree_skips(self):
        self.put('PUBLIC_TREE', 'a public tree\n')
        self.assertTrue(terms.public_tree(self.repo))
        self.assertIsNone(terms.load(self.repo, 'x_terms'))

    def test_loads_module(self):
        self.put('tools/x_terms.py', 'WORDS = ("zzneutralmarkzz",)\n')
        m = terms.load(self.repo, 'x_terms')
        self.assertEqual(m.WORDS, ('zzneutralmarkzz',))
        self.assertFalse(os.path.exists(os.path.join(self.repo, 'tools', '__pycache__')), 'no bytecode left behind')

    def test_marker_never_hides_a_module(self):
        # preflight M14: a stray marker never disables a scan; a module present is always used
        self.put('PUBLIC_TREE', 'a public tree\n')
        self.put('tools/x_terms.py', 'WORDS = ("zzneutralmarkzz",)\n')
        self.assertEqual(terms.load(self.repo, 'x_terms').WORDS, ('zzneutralmarkzz',))


if __name__ == '__main__':
    unittest.main()
