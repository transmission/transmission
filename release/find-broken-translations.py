#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# find-broken-translations.py
#
# Scans po/*.po for translated strings whose libfmt placeholders reference
# argument names that the source string (msgid / msgid_plural) does not
# supply. Because Transmission builds libfmt with FMT_USE_EXCEPTIONS=0
# (see cmake/FindFmt.cmake), such a mismatch is fatal at runtime: when
# fmt::vformat hits the missing arg it calls fmt::detail::assert_fail()
# which abort()s the process. See issue #8766 for an example
# (fr.po `{time_span}` typo crashing the GTK client).
#
# Usage:
#     cd <transmission-source-root>
#     python3 release/find-broken-translations.py
#
# Output is a Markdown table with one row per offending translation,
# including a deep link to the string in Transifex for quick editing.
#
# Notes:
# - Only argument *names* are checked. Format-specs like `{count:L}` are
#   stripped before comparison, so `{count}` vs `{count:L}` is not flagged.
# - For ngettext entries, the union of msgid and msgid_plural names is
#   treated as "allowed" because the runtime call passes `count` for both
#   singular and plural cases.
# - Only the first offending msgstr per entry is reported (translators
#   typically copy the same mistake into every plural form).

import glob
import os
import re
import urllib.parse

# Matches `{name}` and `{name:format-spec}`. Captures just the name.
NAME_RE = re.compile(r'\{([A-Za-z_][A-Za-z0-9_]*)(?::[^{}]*)?\}')


def names(s):
    """Return the set of libfmt argument names referenced in `s`."""
    return set(NAME_RE.findall(s))


def parse_po(path):
    """Yield (msgid, msgid_plural, [msgstr, ...]) tuples from a .po file.

    Uses a tiny state machine; relies on .po quoted strings being valid
    Python string literals (which they are, per gettext spec) so `eval`
    handles escapes (\\n, \\", \\\\, etc.) for us.
    """
    entries = []
    msgid = msgid_plural = ""
    msgstrs = {}
    state = None

    def flush():
        if msgid:
            entries.append((msgid, msgid_plural, list(msgstrs.values())))

    with open(path, encoding='utf-8') as f:
        for raw in f:
            line = raw.rstrip('\n')
            if not line.strip():
                flush()
                msgid = msgid_plural = ""
                msgstrs = {}
                state = None
                continue
            if line.startswith('#'):
                continue
            if line.startswith('msgid '):
                flush()
                msgid_plural = ""
                msgstrs = {}
                msgid = eval(line[6:])
                state = 'msgid'
            elif line.startswith('msgid_plural '):
                msgid_plural = eval(line[13:])
                state = 'msgid_plural'
            elif line.startswith('msgstr '):
                msgstrs[0] = eval(line[7:])
                state = ('msgstr', 0)
            elif (m := re.match(r'msgstr\[(\d+)\] (.*)', line)):
                i = int(m.group(1))
                msgstrs[i] = eval(m.group(2))
                state = ('msgstr', i)
            elif line.startswith('"'):
                # Continuation line; append to whatever we're building.
                s = eval(line)
                if state == 'msgid':
                    msgid += s
                elif state == 'msgid_plural':
                    msgid_plural += s
                elif isinstance(state, tuple):
                    msgstrs[state[1]] += s
    flush()
    return entries


def tx_url(lang, msgid):
    """Build a Transifex deep link that filters strings by msgid text."""
    q = urllib.parse.quote(f"text:'{msgid[:60]}'", safe="")
    return (f"https://app.transifex.com/transmissionbt/transmissionbt"
            f"/viewstrings/#{lang}/gtk/?q={q}")


def main():
    rows = []
    for path in sorted(glob.glob('po/*.po')):
        lang = os.path.basename(path)[:-3]
        for msgid, msgid_plural, msgstrs in parse_po(path):
            allowed = names(msgid) | names(msgid_plural)
            for ms in msgstrs:
                if not ms:
                    continue
                extra = names(ms) - allowed
                if extra:
                    rows.append((lang, sorted(extra), msgid, ms))
                    break  # one report per entry is enough

    print("| Lang | Bad placeholder(s) | msgid | msgstr (offending) | Edit |")
    print("|------|---------------------|-------|---------------------|------|")
    for lang, extra, msgid, ms in rows:
        ph = ', '.join(f'`{{{x}}}`' for x in extra)
        sm = msgid if len(msgid) < 50 else msgid[:47] + '...'
        st = ms if len(ms) < 60 else ms[:57] + '...'
        print(f"| {lang} | {ph} | `{sm}` | `{st}` | [tx]({tx_url(lang, msgid)}) |")
    langs = len({r[0] for r in rows})
    print(f"\n{len(rows)} crash-causing translations across {langs} languages.")


if __name__ == '__main__':
    main()
