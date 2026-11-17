#!/usr/bin/env python3
"""The release guard (public release Task 14): fails on any listed term, personal string, secret, excluded path or
private overlay file in an exported tree, in text and in binaries (their printable runs).

It ships with no list. Its terms come from a file kept outside the tree (refused if it lies inside), with one
tab-separated entry per line ('#' starts a comment line, blank lines are skipped):

    term   <words>        case-insensitive; between two words any run of blanks, _ . + / -, quotes, commas and colons
                          (or none), so "a " "b" and a, b hit; across a line break also behind comment and quote
                          leaders (* # > ; % | // -- /* <!-- " ')
    word   <token>        case-insensitive, bounded on [A-Za-z0-9_] (so a hex string or a hash never hits); in a
                          binary, only in a printable run of 8 or more characters
    regex  <python re>    case-insensitive
    allow  <relpath> <sha256> <reason>
                          that file, with exactly that content, is exempt from the term and piece scans; every allow
                          used is printed with its reason

The scans: every path as text; every text file (no NUL in its first 8 KiB) over its whole text, a line break and the
next line's indent and leaders folded to one space first, so a term wrapped across lines is found at the line where it
starts; every binary over its printable runs of 4 or more (term, regex) or 8 or more (word); each --also file as a
binary. TREE/.git is skipped.

The built-in checks, generic: no path matching the tree's own tools/public_exclude.txt; a PUBLIC_TREE marker; no PEM
private key, GitHub token or AWS access key id; with --overlay, no file whose git blob id equals that of any non-empty
file under ART (its .git and out/ left out); no quoted piece pair in .py/.js/.mjs/.sh files and no shell append of
lower-case letters in .sh files (a term assembled from pieces), each failing unless an allow covers that file.

    python3 tools/release_check.py --terms FILE [--overlay ART] [--also FILE ...] TREE
                                     exit 0 pass, 1 hits, 2 refused (no --terms, a terms file inside TREE, an
                                     unparsable line, a missing TREE or ART)
    python3 tools/release_check.py --selftest

A report names the file, the line (or byte offset) and the terms file's line that matched, never the term itself.
"""
import hashlib
import os
import re
import sys
import tempfile

SEP = r'''[\s_.+/\-"',:]*'''   # blanks, _ . + / -, quotes, commas and colons (a term split across literals)
WORD_L, WORD_R = r'(?<![A-Za-z0-9_])', r'(?![A-Za-z0-9_])'
# a line break, the next line's indent, then any run of comment or quote leaders, each with the blanks after it
FOLD = re.compile(r'\r?\n[ \t]*(?:(?:[*#>;%|"\']|//|--|/\*|<!--)[ \t]*)*')
PRINTABLE_RUN = re.compile(rb'[\x20-\x7e\t]{4,}')
WORD_RUN_MIN = 8
TEXT_PROBE = 8192

SECRETS = [
    ('a PEM private key', re.compile(r'-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY-----')),
    ('a GitHub token', re.compile(r'(?<![A-Za-z0-9_])(?:ghp_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{22,})')),
    ('an AWS access key id', re.compile(r'(?<![A-Z0-9])AKIA[0-9A-Z]{16}(?![A-Z0-9])')),
]
# preflight K1: the pieces a term can be assembled from, which the assembled-term scans cannot see
PIECE_QUOTED = re.compile(r'''b?['"][A-Za-z.]{1,6}['"] *\+ *b?['"][A-Za-z.]{1,6}['"]''')
PIECE_SHELL = re.compile(r'\b[a-z]+[0-9]?\+=[a-z]+')
PIECE_QUOTED_EXT = ('.py', '.js', '.mjs', '.sh')
EXCLUDE_LIST = 'tools/public_exclude.txt'
MARKER = 'PUBLIC_TREE'


class Refused(Exception):
    pass


class Terms:
    """terms: the term and regex patterns (every scan); words: the word patterns; allow: 'relpath\\tsha256' -> reason.
    term_labels / word_labels name each pattern by its kind and its line in the terms file; used collects the allows
    a scan applied."""

    def __init__(self):
        self.terms, self.term_labels = [], []
        self.words, self.word_labels = [], []
        self.allow = {}
        self.used = []


def load_terms_text(text, name):
    t = Terms()
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip('\r')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        kind, _, rest = line.partition('\t')
        if not rest.strip():
            raise Refused('%s:%d: no value after %r' % (name, n, kind))
        label = '%s:%d' % (kind, n)
        if kind == 'term':
            words = rest.split()
            pat = SEP.join(re.escape(w) for w in words)
            t.terms.append(re.compile(pat, re.IGNORECASE))
            t.term_labels.append(label)
        elif kind == 'word':
            if len(rest.split()) != 1 or not re.fullmatch(r'[A-Za-z0-9_]+', rest.strip()):
                raise Refused('%s:%d: a word is one token of letters, digits and underscores' % (name, n))
            t.words.append(re.compile(WORD_L + re.escape(rest.strip()) + WORD_R, re.IGNORECASE))
            t.word_labels.append(label)
        elif kind == 'regex':
            try:
                t.terms.append(re.compile(rest, re.IGNORECASE))
            except re.error as e:
                raise Refused('%s:%d: a bad regex (%s)' % (name, n, e))
            t.term_labels.append(label)
        elif kind == 'allow':
            parts = rest.split('\t', 2)
            if len(parts) != 3 or not parts[0] or not re.fullmatch(r'[0-9a-f]{64}', parts[1]) or not parts[2].strip():
                raise Refused('%s:%d: allow needs <relpath> <sha256> <reason>, tab-separated' % (name, n))
            t.allow[parts[0] + '\t' + parts[1]] = parts[2].strip()
        else:
            raise Refused('%s:%d: unknown kind %r (term, word, regex or allow)' % (name, n, kind))
    if not t.terms and not t.words:
        raise Refused('%s: no term, word or regex entry (an empty or truncated list would pass everything)' % name)
    return t


def load_terms(path):
    try:
        with open(path, encoding='utf-8') as f:
            text = f.read()
    except (OSError, UnicodeDecodeError) as e:
        raise Refused('the terms file %s: %s' % (path, e))
    return load_terms_text(text, path)


def fold(text):
    """(text with every line break folded to one space, the folded offsets of those spaces in order)."""
    out, spaces, at, n = [], [], 0, 0
    for m in FOLD.finditer(text):
        out.append(text[at:m.start()])
        n += m.start() - at
        spaces.append(n)
        out.append(' ')
        n += 1
        at = m.end()
    out.append(text[at:])
    return ''.join(out), spaces


def text_hits(text, t):
    """(line of the match start, the pattern's kind:line in the terms file) for every term, regex and word in text."""
    import bisect
    folded, spaces = fold(text)
    got = set()
    for pats, labels in ((t.terms, t.term_labels), (t.words, t.word_labels)):
        for p, label in zip(pats, labels):
            for m in p.finditer(folded):
                got.add((bisect.bisect_right(spaces, m.start()) + 1, label))
    return sorted(got)


def bytes_hits(data, t):
    """(byte offset, kind:line) in data's printable runs: terms and regexes in every run of 4 or more, words only in a
    run of WORD_RUN_MIN or more."""
    got = set()
    for run in PRINTABLE_RUN.finditer(data):
        s = run.group(0).decode('ascii')
        sets = [(t.terms, t.term_labels)]
        if len(s) >= WORD_RUN_MIN:
            sets.append((t.words, t.word_labels))
        for pats, labels in sets:
            for p, label in zip(pats, labels):
                for m in p.finditer(s):
                    got.add((run.start() + m.start(), label))
    return sorted(got)


def glob_regex(g):
    """A git glob pathspec (`*` and `?` within one path component, `**/`, `/**` and `/**/` across them) as a regex."""
    out, i = [], 0
    while i < len(g):
        if g.startswith('**/', i) and (i == 0 or g[i - 1] == '/'):
            out.append('(?:.*/)?')
            i += 3
        elif g.startswith('/**', i) and i + 3 == len(g):
            out.append('/.*')
            i += 3
        elif g[i] == '*':
            out.append('[^/]*')
            i += 1
        elif g[i] == '?':
            out.append('[^/]')
            i += 1
        elif g[i] == '[':
            j = g.find(']', i + 2)
            if j < 0:
                out.append(re.escape(g[i]))
                i += 1
            else:
                body = g[i + 1:j]
                out.append('[' + ('^' + body[1:] if body.startswith('!') else body) + ']')
                i = j + 1
        else:
            out.append(re.escape(g[i]))
            i += 1
    return re.compile(''.join(out))


def exclude_lines(text):
    """The exclude list's globs: one per line, '#' comments (whole-line or after blanks) and blank lines dropped."""
    out = []
    for line in text.splitlines():
        g = re.sub(r'(^|\s+)#.*$', '', line).strip()
        if g:
            out.append(g)
    return out


def blob_id(data):
    return hashlib.sha1(b'blob %d\0' % len(data) + data).hexdigest()


def overlay_blobs(art):
    ids = {}
    for d, dirs, files in os.walk(art):
        dirs[:] = sorted(x for x in dirs if x != '.git' and not (d == art and x == 'out'))
        for f in sorted(files):
            p = os.path.join(d, f)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            with open(p, 'rb') as fh:
                data = fh.read()
            if data:
                ids.setdefault(blob_id(data), os.path.relpath(p, art))
    return ids


def tree_files(tree):
    """(relpath, path) of every file and symlink under tree, TREE/.git left out. A symlink is never followed: a
    symlinked directory is yielded as a file too, so its name and its target string are scanned."""
    for d, dirs, files in os.walk(tree):
        dirs[:] = sorted(x for x in dirs if not (d == tree and x == '.git'))
        links = [x for x in dirs if os.path.islink(os.path.join(d, x))]
        dirs[:] = [x for x in dirs if x not in links]
        for f in sorted(files + links):
            if d == tree and f == '.git':
                continue
            p = os.path.join(d, f)
            yield os.path.relpath(p, tree).replace(os.sep, '/'), p


def is_text(data):
    return b'\0' not in data[:TEXT_PROBE]


def secret_hits(s):
    return [what for what, p in SECRETS if p.search(s)]


def content_problems(rel, data, t, allowed):
    """The term, secret and piece problems of one file's bytes (an allowed file skips the term and piece scans)."""
    out = []
    if is_text(data):
        text = data.decode('utf-8', errors='replace')
        for what in secret_hits(text):
            out.append('%s: secret: %s' % (rel, what))
        if allowed:
            return out
        for line, label in text_hits(text, t):
            out.append('%s:%d: %s' % (rel, line, label))
        low = rel.lower()
        pieces = []
        if low.endswith(PIECE_QUOTED_EXT):
            pieces += [('quoted', m) for m in PIECE_QUOTED.finditer(text)]
        if low.endswith('.sh'):
            pieces += [('shell append', m) for m in PIECE_SHELL.finditer(text)]
        for what, m in pieces:
            out.append('%s:%d: piece: a %s pair (allow it with a reason if it is an ordinary concatenation)'
                       % (rel, text.count('\n', 0, m.start()) + 1, what))
    else:
        runs = ' '.join(r.group(0).decode('ascii') for r in PRINTABLE_RUN.finditer(data))
        for what in secret_hits(runs):
            out.append('%s: secret: %s' % (rel, what))
        if allowed:
            return out
        for off, label in bytes_hits(data, t):
            out.append('%s@%d: %s' % (rel, off, label))
    return out


def tree_problems(tree, t, overlay):
    """Every problem in TREE, as 'relpath[:line|@offset]: what' strings, in path order."""
    problems = []
    excl_path = os.path.join(tree, EXCLUDE_LIST)
    globs = []
    if os.path.isfile(excl_path):
        with open(excl_path, encoding='utf-8', errors='replace') as f:
            globs = [(g, glob_regex(g)) for g in exclude_lines(f.read())]
    else:
        problems.append('%s: missing (the exclude list the export used)' % EXCLUDE_LIST)
    if not os.path.isfile(os.path.join(tree, MARKER)):
        problems.append('%s: missing (an exported tree carries the marker)' % MARKER)
    blobs = overlay_blobs(overlay) if overlay else {}
    for rel, p in tree_files(tree):
        for line, label in text_hits(rel, t):
            problems.append('%s: path: %s' % (rel, label))
        for g, rx in globs:
            if rx.fullmatch(rel):
                problems.append('%s: exclude: matches %r in %s' % (rel, g, EXCLUDE_LIST))
        if os.path.islink(p):
            data = os.readlink(p).encode()
        else:
            with open(p, 'rb') as f:
                data = f.read()
        key = rel + '\t' + hashlib.sha256(data).hexdigest()
        allowed = key in t.allow
        if allowed:
            t.used.append('%s (%s)' % (rel, t.allow[key]))
        problems += content_problems(rel, data, t, allowed)
        if data and blob_id(data) in blobs:
            problems.append('%s: overlay: the same blob as the overlay\'s %s' % (rel, blobs[blob_id(data)]))
    return problems


def also_problems(paths, t):
    out = []
    for p in paths:
        with open(p, 'rb') as f:
            data = f.read()
        name = os.path.basename(p)
        runs = ' '.join(r.group(0).decode('ascii') for r in PRINTABLE_RUN.finditer(data))
        out += ['%s (--also): secret: %s' % (name, w) for w in secret_hits(runs)]
        out += ['%s (--also)@%d: %s' % (name, off, label) for off, label in bytes_hits(data, t)]
    return out


def inside(path, top):
    path, top = os.path.realpath(path), os.path.realpath(top)
    return path == top or path.startswith(top.rstrip(os.sep) + os.sep)


def check(argv):
    terms, overlay, also, tree = None, None, [], None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('--terms', '--overlay', '--also'):
            if i + 1 >= len(argv):
                raise Refused('%s needs a value' % a)
            v = argv[i + 1]
            if a == '--terms':
                terms = v
            elif a == '--overlay':
                overlay = v
            else:
                also.append(v)
            i += 2
            continue
        if a.startswith('-') or tree is not None:
            raise Refused('usage: release_check.py --terms FILE [--overlay ART] [--also FILE ...] TREE')
        tree = a
        i += 1
    if not terms:
        raise Refused('no --terms: the guard ships with no list and refuses to run without one')
    if not tree or not os.path.isdir(tree):
        raise Refused('the tree %r is missing' % tree)
    if inside(terms, tree):
        raise Refused('the terms file lies inside the tree it checks (%s)' % terms)
    if overlay is not None and not os.path.isdir(overlay):
        raise Refused('the overlay %r is missing' % overlay)
    for p in also:
        if not os.path.isfile(p):
            raise Refused('the --also file %r is missing' % p)
    t = load_terms(terms)
    problems = tree_problems(tree, t, overlay) + also_problems(also, t)
    for p in problems:
        print('FAIL: ' + p)
    for u in t.used:
        print('ALLOW: ' + u)
    nfiles = sum(1 for _ in tree_files(tree))
    print('release_check: %s (%d files%s, %d terms, %d words, %d allows used)'
          % ('FAIL, %d problems' % len(problems) if problems else 'PASS', nfiles,
             ', %d --also' % len(also) if also else '', len(t.terms), len(t.words), len(t.used)))
    return 1 if problems else 0


def selftest():
    """The guard against an invented list on a temp tree: one clean tree passes, each planted problem is found."""
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        tree, art = os.path.join(tmp, 'tree'), os.path.join(tmp, 'art')

        def put(base, rel, data):
            p = os.path.join(base, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(data)

        t = load_terms_text('term\tzebra quokka\nword\tzzq\n', 'selftest')
        put(tree, MARKER, b'x\n')
        put(tree, EXCLUDE_LIST, b'docs/private/**\n')
        put(tree, 'src/a.c', b'int a; /* a+=b */\n')
        put(tree, 'assets/a.bin', b'\x00\x01xzzqx\x00the zzqs\x00')
        put(art, 'x.bin', b'\x19art')
        put(art, 'empty', b'')
        put(tree, 'src/empty', b'')
        if tree_problems(tree, t, art):
            bad.append('a clean tree: %r' % tree_problems(tree, t, art))
        q = chr(39)
        planted = [
            ('src/b.c', b'/* the zebra\n * quokka */\n', 'src/b.c:1: term'),
            ('src/c.c', b'the ZZQ said\n', 'src/c.c:1: word'),
            ('src/zebra_quokka.h', b'\n', 'src/zebra_quokka.h: path'),
            ('assets/b.bin', b'\x00xx zebra-quokka\x00', 'assets/b.bin@4: term'),
            ('docs/private/x.md', b'\n', 'docs/private/x.md: exclude'),
            ('k.pem', ('-' * 5 + 'BEGIN PRIVATE KEY' + '-' * 5 + '\n').encode(), 'k.pem: secret'),
            ('t/a.py', ('x = ' + q + 'zeb' + q + ' + ' + q + 'ra' + q + '\n').encode(), 't/a.py:1: piece'),
            ('t/b.sh', b'n1+=ab\n', 't/b.sh:1: piece'),
            ('firmware/same.bin', b'\x19art', 'firmware/same.bin: overlay'),
        ]
        for rel, data, want in planted:
            put(tree, rel, data)
            got = tree_problems(tree, t, art)
            if not any(p.startswith(want) for p in got):
                bad.append('%s not found (%r)' % (want, got))
            os.unlink(os.path.join(tree, rel))
        os.unlink(os.path.join(tree, MARKER))
        if not any(p.startswith(MARKER) for p in tree_problems(tree, t, art)):
            bad.append('a missing marker not found')
    for b in bad:
        print('FAIL: release_check selftest: ' + b)
    print('release_check selftest: %s (%d planted problems)' % ('FAIL' if bad else 'OK', len(planted) + 1))
    return 1 if bad else 0


def main(argv):
    if argv == ['--selftest']:
        return selftest()
    try:
        return check(argv)
    except Refused as e:
        print('REFUSED: %s' % e, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
