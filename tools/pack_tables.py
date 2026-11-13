"""Pack RADS's 512 wave tables for the Digitakt (whose mods share 256 KB of RAM): each table as its
first value, then the steps between neighbours in a canonical Huffman code (steps beyond +-LIM: an
escape, then the value in 8 bits). Writes src/rads_pack.h. Usage: pack_tables.py src/rads_data.h out.h"""
import re, sys, heapq, collections
LIM = 15
src, out = sys.argv[1], sys.argv[2]
s = open(src).read()
i = s.index('rd_tables'); j = s.index('{', i); k = s.index('};', j)
nums = list(map(int, re.findall(r'\d+', s[j:k])))[:512 * 256]
T = [nums[t * 256:(t + 1) * 256] for t in range(512)]
ESC = 2 * LIM + 1                                   # symbols 0..2LIM: step + LIM; ESC
def syms(t):
    for n in range(1, 256):
        d = t[n] - t[n - 1]
        yield (d + LIM, None) if -LIM <= d <= LIM else (ESC, t[n])
freq = collections.Counter(sy for t in T for sy, _ in syms(t))
# Huffman code lengths (limited to 16 by construction here: checked)
h = [[f, [sy]] for sy, f in freq.items()]
heapq.heapify(h); L = collections.Counter()
while len(h) > 1:
    a = heapq.heappop(h); b = heapq.heappop(h)
    for sy in a[1] + b[1]: L[sy] += 1
    heapq.heappush(h, [a[0] + b[0], a[1] + b[1]])
assert max(L.values()) <= 16
# canonical codes: by length, then symbol
order = sorted(L, key=lambda sy: (L[sy], sy))
code, prev, codes = 0, 0, {}
for sy in order:
    code <<= (L[sy] - prev); prev = L[sy]; codes[sy] = (code, L[sy]); code += 1
count = [0] * 17
for sy in order: count[L[sy]] += 1
bits, offs = [], []
for t in T:
    while len(bits) % 8: bits.append(0)             # each table from a byte
    offs.append(len(bits) // 8)
    for b in range(7, -1, -1): bits.append((t[0] >> b) & 1)
    for sy, raw in syms(t):
        c, l = codes[sy]
        for b in range(l - 1, -1, -1): bits.append((c >> b) & 1)
        if raw is not None:
            for b in range(7, -1, -1): bits.append((raw >> b) & 1)
while len(bits) % 8: bits.append(0)
data = bytes(int(''.join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))
# check: decode all
def dec(t):
    p = offs[t] * 8
    def rd(n):
        nonlocal p
        v = 0
        for _ in range(n): v = (v << 1) | ((data[p >> 3] >> (7 - (p & 7))) & 1); p += 1
        return v
    v = rd(8); o = [v]
    for n in range(255):
        c = first = idx = 0
        for l in range(1, 17):
            c |= rd(1); cnt = count[l]
            if c - first < cnt: sy = order[idx + c - first]; break
            idx += cnt; first = (first + cnt) << 1; c <<= 1
        v = rd(8) if sy == ESC else v + sy - LIM
        o.append(v)
    return o
assert all(dec(t) == T[t] for t in range(512))
with open(out, 'w') as f:
    f.write('/* RADS\'s 512 wave tables, packed for the Digitakt by tools/pack_tables.py (do not edit): each\n'
            ' * table from a byte, its first value in 8 bits, then the steps between neighbours in a canonical\n'
            ' * Huffman code (%d symbols: steps -%d..+%d, and an escape followed by the value in 8 bits). The\n'
            ' * tables are GESTALT\'s own, made by tools/make_tables.py. */\n' % (ESC + 1, LIM, LIM))
    f.write('#define RD_PK_LIM %d\n#define RD_PK_ESC %d\n' % (LIM, ESC))
    f.write('static const unsigned char rd_pk_count[17] = {%s};\n' % ','.join(map(str, count)))
    f.write('static const unsigned char rd_pk_sym[%d] = {%s};\n' % (len(order), ','.join(map(str, order))))
    f.write('static const unsigned int rd_pk_off[512] = {%s};\n' % ','.join(map(str, offs)))
    f.write('static const unsigned char rd_pk_bits[%d] = {\n' % len(data))
    for i in range(0, len(data), 32): f.write(','.join(map(str, data[i:i + 32])) + ',\n')
    f.write('};\n')
print('packed', len(data), 'bytes (+', 512 * 4, 'offsets); was', 512 * 256)
