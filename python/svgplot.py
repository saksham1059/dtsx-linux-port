#!/usr/bin/env python3
"""Tiny dependency-free SVG plotting for frequency-response figures."""
import math


def _x(f, fmin, fmax, left, width):
    return left + width * (math.log10(f) - math.log10(fmin)) / (
        math.log10(fmax) - math.log10(fmin))


def _y(db, dmin, dmax, top, height):
    return top + height * (dmax - db) / (dmax - dmin)


def response_svg(path, title, series, fmin=20, fmax=20000, dmin=-30, dmax=20,
                 width=900, height=420):
    """series: list of (label, freqs, dbs, color, dash)."""
    left, right, top, bottom = 60, 20, 48, 48
    pw, ph = width - left - right, height - top - bottom
    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
               'viewBox="0 0 %d %d">' % (width, height, width, height))
    out.append('<rect width="100%%" height="100%%" fill="#ffffff"/>')
    out.append('<text x="%d" y="26" font-family="sans-serif" font-size="16" '
               'font-weight="bold" fill="#111">%s</text>' % (left, _esc(title)))
    # grid
    for f in (20, 30, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000):
        x = _x(f, fmin, fmax, left, pw)
        out.append('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="#e2e2e2"/>'
                   % (x, top, x, top + ph))
        out.append('<text x="%.1f" y="%d" font-family="sans-serif" font-size="11" '
                   'text-anchor="middle" fill="#555">%s</text>'
                   % (x, top + ph + 16, _hz(f)))
    db = dmax
    while db >= dmin:
        y = _y(db, dmin, dmax, top, ph)
        out.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e2e2e2"/>'
                   % (left, y, left + pw, y))
        out.append('<text x="%d" y="%.1f" font-family="sans-serif" font-size="11" '
                   'text-anchor="end" fill="#555">%+d</text>'
                   % (left - 8, y + 4, db))
        db -= 10
    out.append('<text x="14" y="%d" font-family="sans-serif" font-size="12" '
               'transform="rotate(-90 14 %d)" fill="#333">magnitude (dB)</text>'
               % (top + ph // 2, top + ph // 2))
    for idx, (label, freqs, dbs, color, dash) in enumerate(series):
        pts = []
        for f, d in zip(freqs, dbs):
            if f < fmin or f > fmax:
                continue
            pts.append('%.1f,%.1f' % (_x(f, fmin, fmax, left, pw),
                                      _y(d, dmin, dmax, top, ph)))
        if len(pts) > 1:
            out.append('<polyline fill="none" stroke="%s" stroke-width="2"%s '
                       'points="%s"/>' % (color,
                                          ' stroke-dasharray="6 4"' if dash else '',
                                          ' '.join(pts)))
        ly = top + 16 + 18 * idx
        out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" '
                   'stroke-width="3"%s/>' % (left + pw - 190, ly, left + pw - 160,
                                             ly, color,
                                             ' stroke-dasharray="6 4"' if dash else ''))
        out.append('<text x="%d" y="%d" font-family="sans-serif" font-size="12" '
                   'fill="#222">%s</text>' % (left + pw - 154, ly + 4, _esc(label)))
    out.append('</svg>')
    open(path, 'w').write('\n'.join(out))


def _hz(f):
    if f >= 1000:
        v = f / 1000.0
        return ('%gk' % v) if v != int(v) else '%dk' % int(v)
    return '%d' % f


def _esc(s):
    return (s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;'))
