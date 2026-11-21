#!/usr/bin/env python3
"""Decode the OEM GPEQ coefficient blobs.

Format recovered by static analysis of dtstech64.dll (see
docs/REVERSE_ENGINEERING_FINDINGS.md):

  half struct: u32 count(n); then ceil(n/2) records of 24 bytes
  record:      i32 exponent; i32 c1..c5
  float coefficient ci = ci / (2**(32 - (exponent & 31)) - 1)

The runtime consumer (FUN_18009f0b0) implements a transposed-direct-form-II
biquad cascade with five coefficients per section and two state words, so:

  b0 = c1, b1 = c2, b2 = c3, a1 = -c4, a2 = -c5
  H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)

The file holds two identical channel halves at 0x000 and 0x938 for GPEQ.
"""
import math
import struct

GPEQ_HALF = 0x938
GPEQ_SIZE = 0x1270


def decode_half(data, base):
    n = struct.unpack_from("<I", data, base)[0]
    sections = []
    for k in range((n + 1) // 2):
        off = base + 8 + 24 * k
        exp, c1, c2, c3, c4, c5 = struct.unpack_from("<6i", data, off)
        shift = (32 - (exp & 0x1F)) & 31
        scale = float((1 << shift) - 1)
        if scale <= 0:
            continue
        sections.append([c1 / scale, c2 / scale, c3 / scale,
                         -c4 / scale, -c5 / scale])
    return n, sections


def decode(path):
    data = open(path, "rb").read()
    if len(data) != GPEQ_SIZE:
        raise ValueError("not a 4720-byte GPEQ blob: %s (%d)" % (path, len(data)))
    return decode_half(data, 0), decode_half(data, GPEQ_HALF)


def biquad_db(section, fs, freqs):
    b0, b1, b2, a1, a2 = section
    out = []
    for f in freqs:
        w = 2.0 * math.pi * f / fs
        c1, s1 = math.cos(w), math.sin(w)
        c2, s2 = math.cos(2 * w), math.sin(2 * w)
        nr = b0 + b1 * c1 + b2 * c2
        ni = -(b1 * s1 + b2 * s2)
        dr = 1.0 + a1 * c1 + a2 * c2
        di = -(a1 * s1 + a2 * s2)
        out.append(10.0 * math.log10((nr * nr + ni * ni + 1e-30) /
                                     (dr * dr + di * di + 1e-30)))
    return out


def cascade_db(sections, fs, freqs):
    acc = [0.0] * len(freqs)
    for s in sections:
        d = biquad_db(s, fs, freqs)
        for i, v in enumerate(d):
            acc[i] += v
    return acc


def peaking_db(f, fs, f0, q, gain_db):
    """RBJ peaking magnitude in dB."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * f0 / fs
    alpha = math.sin(w0) / (2.0 * q)
    b0 = 1 + alpha * A
    b1 = -2 * math.cos(w0)
    b2 = 1 - alpha * A
    a0 = 1 + alpha / A
    a1 = -2 * math.cos(w0)
    a2 = 1 - alpha / A
    w = 2.0 * math.pi * f / fs
    c1, s1 = math.cos(w), math.sin(w)
    c2, s2 = math.cos(2 * w), math.sin(2 * w)
    nr = b0 + b1 * c1 + b2 * c2
    ni = -(b1 * s1 + b2 * s2)
    dr = a0 + a1 * c1 + a2 * c2
    di = -(a1 * s1 + a2 * s2)
    return 10.0 * math.log10((nr * nr + ni * ni + 1e-30) /
                             (dr * dr + di * di + 1e-30))


def eq_model_db(gains, centers, q, fs, freqs):
    acc = [0.0] * len(freqs)
    for g, fc in zip(gains, centers):
        for i, f in enumerate(freqs):
            acc[i] += peaking_db(f, fs, fc, q, g)
    return acc


def log_grid(f_lo=20.0, f_hi=20000.0, n=420):
    return [f_lo * (f_hi / f_lo) ** (i / (n - 1.0)) for i in range(n)]
