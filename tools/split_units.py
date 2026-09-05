#!/usr/bin/env python3
"""Move whole top-level function definitions between C++ translation units.

Used once to split the seed's largest files along their seams; kept so the
operation is reproducible and reviewable. Usage:

    tools/split_units.py SRC DST NAME [NAME...]   move NAME... from SRC to DST
    tools/split_units.py --list SRC               print every function with its lines
"""
import re, sys

SIG = re.compile(r'^(?:auto [A-Za-z_]+::([A-Za-z_0-9]+)\(|(?:static |inline )?[A-Za-z_][A-Za-z_0-9:<>*& ]*? ([A-Za-z_][A-Za-z_0-9]*)\([^;]*\) (?:const )?(?:-> [^{]+)?\{$)')

def functions(text):
    """Yield (name, start, end) line indices for each top-level definition,
    where a definition begins at a line matching SIG and ends at the matching
    close brace, skipping braces inside strings, chars, and comments."""
    lines = text.split('\n')
    i = 0
    out = []
    while i < len(lines):
        m = SIG.match(lines[i])
        if not m or lines[i].startswith(' '):
            i += 1
            continue
        name = m.group(1) or m.group(2)
        depth = 0; j = i; in_block = False; done = False
        while j < len(lines) and not done:
            s = lines[j]; k = 0; in_str = None
            while k < len(s):
                c = s[k]
                if in_block:
                    if s.startswith('*/', k): in_block = False; k += 2; continue
                    k += 1; continue
                if in_str:
                    if c == '\\': k += 2; continue
                    if c == in_str: in_str = None
                    k += 1; continue
                if s.startswith('//', k): break
                if s.startswith('/*', k): in_block = True; k += 2; continue
                if c in '"\'': in_str = c; k += 1; continue
                if c == '{': depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0: done = True; break
                k += 1
            j += 1
        # include the comment lines immediately above the signature
        start = i
        while start > 0 and lines[start-1].startswith('//'):
            start -= 1
        out.append((name, start, j))
        i = j
    return lines, out

def main():
    if sys.argv[1] == '--list':
        lines, fns = functions(open(sys.argv[2]).read())
        for n, a, b in fns: print(f'{a+1:5d}-{b:<5d} {b-a:4d}  {n}')
        return
    src, dst, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    lines, fns = functions(open(src).read())
    want = {n: (a, b) for n, a, b in fns if n in names}
    missing = [n for n in names if n not in want]
    if missing: sys.exit(f'not found in {src}: {missing}')
    moved = []
    keep = []
    ranges = sorted(want.values())
    cursor = 0
    for a, b in ranges:
        keep.extend(lines[cursor:a]); moved.append('\n'.join(lines[a:b]).rstrip('\n')); cursor = b
        while cursor < len(lines) and lines[cursor].strip() == '': cursor += 1
    keep.extend(lines[cursor:])
    open(src, 'w').write('\n'.join(keep))
    body = '\n\n'.join(moved) + '\n'
    dtext = open(dst).read() if __import__('os').path.exists(dst) else ''
    if dtext.rstrip().endswith('} // namespace lucb'):
        idx = dtext.rstrip().rfind('} // namespace lucb')
        dtext = dtext[:idx].rstrip('\n') + '\n\n' + body + '\n} // namespace lucb\n'
    else:
        dtext = dtext.rstrip('\n') + ('\n\n' if dtext else '') + body
    open(dst, 'w').write(dtext)
    print(f'moved {len(moved)} functions {src} -> {dst}')

main()
