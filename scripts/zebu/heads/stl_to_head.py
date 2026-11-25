#!/usr/bin/env python3
"""ZEBU Slicer: builds a toolhead underside model (resources/zebu/heads/<id>.json) from a CAD export.

    python3 stl_to_head.py <toolhead.stl> <id> "<name>" [--png underside.png]

The STL is the complete toolhead (nozzle, heater, ducts, probe, carriage...) in millimetres, Z up, as
mounted on the printer: +X to the right, +Y toward the rear. The nozzle tip must be its lowest point.
For every 0.1 mm cell the exact lowest point of the toolhead is computed (triangles sampled at cell
centres, plus their vertices), then each 0.2 mm cell of the file keeps the lowest of its 0.1 mm cells
and is rounded down to 0.01 mm: the file is never less safe than the CAD.
Needs numpy (and matplotlib for --png).
"""
import argparse, base64, json, struct
import numpy as np

def load_stl(path):
    with open(path, 'rb') as f:
        head = f.read(80)
        n = struct.unpack('<I', f.read(4))[0]
        data = f.read()
    if len(data) < n * 50 or head.lstrip().lower().startswith(b'solid') and len(data) != n * 50:
        raise SystemExit('binary STL expected')
    dt = np.dtype([('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')])
    return np.frombuffer(data[:n * 50], dtype=dt)['v'].astype(np.float64)

def underside(v, c=0.1):
    pts = v.reshape(-1, 3)
    zmin = pts[:, 2].min()
    low = pts[pts[:, 2] < zmin + 1e-6]
    tip = np.array([low[:, 0].mean(), low[:, 1].mean(), zmin])
    v = v - tip
    x0, y0 = v[:, :, 0].min(), v[:, :, 1].min()
    nx = int((v[:, :, 0].max() - x0) / c) + 2
    ny = int((v[:, :, 1].max() - y0) / c) + 2
    H = np.full(ny * nx, np.inf)
    i0 = np.ceil((v[:, :, 0].min(1) - x0) / c - 0.5).astype(int); i1 = np.floor((v[:, :, 0].max(1) - x0) / c - 0.5).astype(int)
    j0 = np.ceil((v[:, :, 1].min(1) - y0) / c - 0.5).astype(int); j1 = np.floor((v[:, :, 1].max(1) - y0) / c - 0.5).astype(int)
    d = (v[:, 1, 1] - v[:, 2, 1]) * (v[:, 0, 0] - v[:, 2, 0]) + (v[:, 2, 0] - v[:, 1, 0]) * (v[:, 0, 1] - v[:, 2, 1])
    ok = (np.abs(d) > 1e-12) & (i1 >= i0) & (j1 >= j0)
    w = i1 - i0 + 1; ncell = np.where(ok, w * (j1 - j0 + 1), 0)
    order = np.where(ok)[0]; order = order[np.argsort(ncell[order])]
    cum = np.cumsum(ncell[order]); s = 0
    while s < len(order):
        e = max(np.searchsorted(cum, (cum[s - 1] if s else 0) + 20_000_000, side='right'), s + 1)
        t = order[s:e]; n = ncell[t]
        tt = np.repeat(t, n); k = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
        ii = i0[tt] + k % w[tt]; jj = j0[tt] + k // w[tt]
        X = x0 + (ii + 0.5) * c; Y = y0 + (jj + 0.5) * c
        a = v[tt]; dd = d[tt]
        l1 = ((a[:, 1, 1] - a[:, 2, 1]) * (X - a[:, 2, 0]) + (a[:, 2, 0] - a[:, 1, 0]) * (Y - a[:, 2, 1])) / dd
        l2 = ((a[:, 2, 1] - a[:, 0, 1]) * (X - a[:, 2, 0]) + (a[:, 0, 0] - a[:, 2, 0]) * (Y - a[:, 2, 1])) / dd
        l3 = 1 - l1 - l2
        m = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
        np.minimum.at(H, jj[m] * nx + ii[m], (l1 * a[:, 0, 2] + l2 * a[:, 1, 2] + l3 * a[:, 2, 2])[m])
        s = e
    H = H.reshape(ny, nx)
    I = np.clip(np.floor((v[:, :, 0].ravel() - x0) / c).astype(int), 0, nx - 1)
    J = np.clip(np.floor((v[:, :, 1].ravel() - y0) / c).astype(int), 0, ny - 1)
    np.minimum.at(H, (J, I), v[:, :, 2].ravel())
    return H, x0, y0, c

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stl'); ap.add_argument('id'); ap.add_argument('name')
    ap.add_argument('--cell', type=float, default=0.2); ap.add_argument('--cap', type=float, default=8.0)
    ap.add_argument('--png')
    a = ap.parse_args()
    H, x0, y0, c = underside(load_stl(a.stl))
    f = int(round(a.cell / c)); ny2, nx2 = H.shape[0] // f, H.shape[1] // f
    Hp = H[:ny2 * f, :nx2 * f].reshape(ny2, f, nx2, f).min(axis=(1, 3))
    jj, ii = np.where(Hp < a.cap)
    j0, j1, i0, i1 = jj.min(), jj.max() + 1, ii.min(), ii.max() + 1
    R = Hp[j0:j1, i0:i1]
    q = np.where(R < a.cap, np.floor(np.maximum(R, 0) * 100 + 1e-9), 65535).astype('<u2')
    out = {
        "schema": "zebu.head_underside.v1", "id": a.id, "name": a.name,
        "units": "mm; frame: nozzle tip at origin, +X right, +Y toward the printer's rear, +Z up",
        "source": "scripts/zebu/heads/stl_to_head.py from the toolhead's CAD (STL): exact lowest point per 0.1 mm cell",
        "cell": a.cell, "origin": [round(x0 + i0 * a.cell, 4), round(y0 + j0 * a.cell, 4)], "size": [int(i1 - i0), int(j1 - j0)],
        "cap": a.cap,
        "comment": "heights_u16: row-major from origin (x fastest), height of the toolhead's underside above the nozzle tip "
                   "in 0.01 mm, rounded down; 65535 = at least 'cap' (outside the raster too)",
        "heights_u16_b64": base64.b64encode(q.tobytes()).decode(),
    }
    with open(a.id + '.json', 'w') as fh:
        json.dump(out, fh, indent=1)
    print('%s.json: %d x %d cells of %.2f mm' % (a.id, q.shape[1], q.shape[0], a.cell))
    if a.png:
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        ny, nx = H.shape
        fig, ax = plt.subplots(figsize=(8, 6.6))
        im = ax.imshow(np.clip(np.where(np.isfinite(H), H, np.nan), 0, 12), origin='lower', cmap='turbo_r', vmin=0, vmax=12,
                       extent=[x0, x0 + nx * c, y0, y0 + ny * c], interpolation='nearest')
        ax.set_xlim(-60, 60); ax.set_ylim(-45, 45); ax.set_aspect('equal'); ax.set_title(a.name)
        fig.colorbar(im, ax=ax, label='underside height above the nozzle tip (mm, clipped at 12)')
        plt.savefig(a.png, dpi=110, bbox_inches='tight')

if __name__ == '__main__':
    main()
