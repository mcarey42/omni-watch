#!/usr/bin/env python3
"""The release term list against the Mining Ship list (public release Task 14, preflight M13).

Every text sample of tools/rd_terms.py's calibration_cases(), rd_selftest_corpus() and test_corpus() gets the same
verdict from release_check.text_hits, with the terms file named by $RELEASE_TERMS, as from rd_terms.rd_text_hits. The
byte samples (the binary rules) are held to "never weaker": every sample rd_terms.rd_bytes_hits flags,
release_check.bytes_hits flags too.

This file holds no term. It skips (exit 77) without tools/rd_terms.py (a public tree) or without RELEASE_TERMS; the
private gates run it with RELEASE_TERMS pointing at the art repo's release/terms.txt.

    RELEASE_TERMS=<art>/release/terms.txt python tools/test_release_parity.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SKIP = 77


def samples(rt):
    """(texts, byte samples): every sample of the three corpora."""
    texts, runs = rt.calibration_cases()
    corpus = rt.test_corpus()
    txt = [t for t, _, _ in texts] + list(rt.rd_selftest_corpus())
    txt += list(corpus['sep_hits']) + [corpus['sep_miss'], corpus['short_text']]
    byts = [b for b, _, _ in runs] + [b for b, _, _ in corpus['short_runs']]
    return txt, byts


def main():
    path = os.environ.get('RELEASE_TERMS')
    if not os.path.isfile(os.path.join(HERE, 'rd_terms.py')) or not path:
        print('SKIP: test_release_parity needs tools/rd_terms.py and RELEASE_TERMS')
        return SKIP
    os.environ.pop('RD_TERMS_TEST_MARKER', None)
    sys.path.insert(0, HERE)
    import rd_terms as rt
    import release_check as rc
    t = rc.load_terms(path)
    texts, byts = samples(rt)
    bad = []
    for i, s in enumerate(texts):
        want, got = bool(rt.rd_text_hits(s)), bool(rc.text_hits(s, t))
        if want != got:
            bad.append('text sample %d: the Mining Ship list says %s, the release list %s' % (i, want, got))
    for i, b in enumerate(byts):
        if rt.rd_bytes_hits(b) and not rc.bytes_hits(b, t):
            bad.append('byte sample %d: the Mining Ship list flags it, the release list does not' % i)
    for line in bad:
        print('FAIL: ' + line)
    print('release parity: %s (%d text samples, %d byte samples)' % ('FAIL' if bad else 'OK', len(texts), len(byts)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
