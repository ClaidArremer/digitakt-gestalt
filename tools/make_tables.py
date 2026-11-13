#!/usr/bin/env python3
"""GESTALT's own 512 wave tables: 32 families of 16, each family from mild to wild, all worked out
here from simple maths (no outside data). A table maps the 8-bit converter's value (0..255, 128 the
middle) to a new one. Bank 1 (00-FF) leans musical, bank 2 (00.-FF.) leans broken.

Usage: make_tables.py src/rads_data.h      (rewrites rd_tables in place)
       make_tables.py --json out.json      (the tables as JSON, for previews)"""
import math, random, sys, json

N = 256
X = [(i - 127.5) / 127.5 for i in range(N)]          # -1..1


def clamp(v, lo=-1.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def norm(ys, peak=True):
    """-1..1 floats to 0..255; scaled so the loudest point reaches full scale."""
    m = max(abs(y) for y in ys) or 1.0
    if not peak:
        m = max(m, 1.0)
    return [max(0, min(255, int(round(127.5 + 127.5 * y / m)))) for y in ys]


def tri(p):                                           # triangle wave, period 4, -1..1
    p = (p + 1) % 4
    return 1 - abs(p - 2)


def fam(f):                                           # f(x, a) with a = 0..1 across the family
    return [norm([f(x, k / 15) for x in X]) for k in range(16)]


def seeded(seed):
    return random.Random(seed * 7919 + 17)


def smooth_noise(r, pts, x):
    """A smooth random curve through `pts` knots, cosine-interpolated."""
    p = (x + 1) / 2 * (len(pts) - 1)
    i = min(int(p), len(pts) - 2)
    u = (1 - math.cos((p - i) * math.pi)) / 2
    return pts[i] * (1 - u) + pts[i + 1] * u


def cheb(n, x):
    return math.cos(n * math.acos(clamp(x)))


families = []

# ---- bank 1: musical ----
# 0 warm drive: identity into a soft tanh, harder each step
families.append(fam(lambda x, a: math.tanh(x * (1 + 14 * a ** 1.6))))
# 1 sine fold: more folds each step
families.append(fam(lambda x, a: math.sin(x * math.pi / 2 * (1 + 7 * a))))
# 2 triangle fold
families.append(fam(lambda x, a: tri(x * (1 + 7 * a))))
# 3 lopsided: even harmonics growing
families.append(fam(lambda x, a: math.tanh(2 * (x + a * 0.9 * (x * x - 0.5)))))
# 4 hard clip, rising drive
families.append(fam(lambda x, a: clamp(x * (1 + 23 * a ** 1.5))))
# 5 stairs: fewer levels each step, 64 down to 2
families.append([norm([round(x * L) / L for x in X])
                 for L in [int(round(32 * (1 / 32) ** (k / 15))) or 1 for k in range(16)]])
# 6 harmonics: Chebyshev T2..T17 blended with the line, the line fading out
families.append([norm([(1 - k / 20) * x + (0.3 + 0.7 * k / 15) * cheb(2 + k, x) for x in X]) for k in range(16)])
# 7 wrap: overdriven and wrapped round
families.append(fam(lambda x, a: ((x * (1 + 5 * a) + 1) % 2) - 1))
# 8 rectify: from the line to full-wave, folding the bottom up
families.append(fam(lambda x, a: (1 - a) * x + a * (2 * abs(x) - 1)))
# 9 crossover: a dead zone round the middle, widening
families.append(fam(lambda x, a: math.copysign(max(0.0, abs(x) - 0.6 * a) / (1 - 0.6 * a + 1e-9), x)))
# 10 ripple: the line with a sine riding on it, faster and deeper
families.append(fam(lambda x, a: x + (0.15 + 0.5 * a) * math.sin(x * math.pi * (3 + 13 * a))))
# 11 expander / compressor: from x^3 through the line to the cube root
families.append(fam(lambda x, a: math.copysign(abs(x) ** (3 ** (1 - 2 * a)), x)))
# 12 double fold: sine of a sine
families.append(fam(lambda x, a: math.sin(math.pi * (1 + 2 * a) * math.sin(x * math.pi / 2 * (1 + 2 * a)))))
# 13 pulse: a comparator with more edges each step
families.append(fam(lambda x, a: 1.0 if math.sin(x * math.pi * (0.5 + 4 * a)) >= 0 else -1.0))
# 14 bells: odd and even partials with a decaying tail
families.append(fam(lambda x, a: sum(math.sin(n * x * math.pi / 2 * (1 + a)) / n ** (1.5 - a)
                                     for n in range(1, 7))))
# 15 chirp: folds that speed up away from the middle
families.append(fam(lambda x, a: math.sin(math.pi / 2 * x * (1 + (2 + 10 * a) * x * x))))

# ---- bank 2: broken ----
xi = list(range(N))


def byfn(g):                                          # g(i, k) -> 0..255 directly
    return [[max(0, min(255, int(g(i, k)))) for i in xi] for k in range(16)]


# 16 XOR masks: the value with bits flipped, more bits each step
families.append(byfn(lambda i, k: i ^ [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 112, 120, 124][k]))
# 17 AND masks: low bits dropped, uneven patterns
families.append(byfn(lambda i, k: (i & (0xFF ^ [1, 2, 3, 5, 7, 9, 11, 15, 21, 27, 31, 45, 63, 85, 119, 127][k]))
                     + [0, 1, 1, 2, 3, 4, 5, 7, 10, 13, 15, 22, 31, 42, 59, 63][k]))
# 18 Gray code, blended in
families.append([[int(round((1 - k / 15) * i + (k / 15) * (i ^ (i >> 1)))) for i in xi] for k in range(16)])
# 19 bit reverse of the low bits: more bits each step
def brev(i, b):
    lo = i & ((1 << b) - 1)
    r = int(format(lo, f'0{b}b')[::-1], 2) if b else 0
    return (i & ~((1 << b) - 1)) | r
families.append(byfn(lambda i, k: brev(i, 1 + k // 2) if k < 15 else brev(i, 8)))
# 20 drift: smooth random curves round the line, wilder each step
fam20 = []
for k in range(16):
    r = seeded(20 * 16 + k)
    pts = [(-1 + 2 * j / 8) + r.uniform(-1, 1) * (0.1 + 0.9 * k / 15) for j in range(9)]
    fam20.append(norm([smooth_noise(r, pts, x) for x in X]))
families.append(fam20)
# 21 jumble: random steps, fewer and bigger each step
fam21 = []
for k in range(16):
    r = seeded(21 * 16 + k)
    n = int(round(64 * (4 / 64) ** (k / 15)))
    lv = [r.uniform(-1, 1) for _ in range(n)]
    lv.sort(key=lambda v: v + r.uniform(-1, 1) * (k / 15) * 2)
    fam21.append(norm([lv[min(n - 1, int((x + 1) / 2 * n))] for x in X]))
families.append(fam21)
# 22 shattered: the line with random spikes
fam22 = []
for k in range(16):
    r = seeded(22 * 16 + k)
    ys = list(X)
    for _ in range(2 + 6 * k):
        c = r.randrange(N); w = r.randrange(1, 6); h = r.uniform(-1, 1) * (0.3 + 0.7 * k / 15)
        for j in range(max(0, c - w), min(N, c + w)):
            ys[j] = clamp(ys[j] + h)
    fam22.append(norm(ys))
families.append(fam22)
# 23 holes: bands of silence cut out of a soft drive
families.append(fam(lambda x, a: 0.0 if math.sin(x * math.pi * (2 + 10 * a)) > 0.8 - 1.2 * a
                    else math.tanh(2 * x)))
# 24 skew wrap: wrap with the fold point sliding
families.append(fam(lambda x, a: (((x + 1) * (1.5 + 3 * a) + a) % 2) - 1))
# 25 parabolas: a chain of arches, more each step
families.append(fam(lambda x, a: 1 - 2 * (((x + 1) * (1 + 3.5 * a)) % 1 * 2 - 1) ** 2 * (1 if x >= 0 else 1)))
# 26 interleave: odd and even inputs pulled apart
families.append(byfn(lambda i, k: i + (1 if i % 2 else -1) * int(round(k * 4.2))))
# 27 sinc: a ringing curve, tighter each step
families.append(fam(lambda x, a: (lambda z: math.sin(z) / z if z else 1.0)(x * math.pi * (1 + 6 * a)) * math.copysign(1, x)))
# 28 random partials: a few sines with seeded phases
fam28 = []
for k in range(16):
    r = seeded(28 * 16 + k)
    parts = [(n, r.uniform(0, 2 * math.pi), r.uniform(0.2, 1) / n) for n in range(1, 3 + k)]
    fam28.append(norm([x * (1 - k / 15) + sum(g * math.sin(n * math.pi * x + p) for n, p, g in parts)
                       for x in X]))
families.append(fam28)
# 29 fractal fold: a triangle fold folded again, deeper each step
families.append(fam(lambda x, a: tri(tri(x * (1 + 3 * a)) * (1 + 6 * a))))
# 30 sample-and-hold crush with a slope: stairs that lean
families.append(fam(lambda x, a: (lambda L: math.floor(x * L) / L + (x * L - math.floor(x * L)) / L * (1 - a) * 0.5)
                    (int(2 + 30 * (1 - a)))))
# 31 noise into sine: hash noise fading into a clean sine fold
fam31 = []
for k in range(16):
    r = seeded(31 * 16 + k)
    nz = [r.uniform(-1, 1) for _ in range(N)]
    a = k / 15
    fam31.append(norm([(1 - a) * nz[i] * 0.8 + (1 - a) * 0.2 * X[i] + a * math.sin(X[i] * math.pi * 3)
                       for i in xi]))
families.append(fam31)

tables = [t for f in families for t in f]
assert len(tables) == 512 and all(len(t) == N and 0 <= min(t) and max(t) <= 255 for t in tables)

if sys.argv[1] == '--json':
    json.dump(tables, open(sys.argv[2], 'w'))
    sys.exit()

path = sys.argv[1]
s = open(path).read()
i = s.index('rd_tables'); j = s.index('{', i); k = s.index('};', j)
body = '\n'.join('    {' + ', '.join(map(str, t)) + '},' for t in tables)
s = s[:j] + '{\n' + body + '\n' + s[k:]
open(path, 'w').write(s)
