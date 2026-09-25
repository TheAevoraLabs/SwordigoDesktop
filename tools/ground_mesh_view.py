#!/usr/bin/env python3
"""Look at two ground meshes side by side.

The numbers in tests/ground_mesh_compare_test.cpp can say a Z envelope grew or a
per-node depth moved, but not *which* face did it — and the complaints about a
generated ground mesh are all shape complaints ("every side is wrapped", "the
boundary distorts", "it balloons"). So this takes two OBJ files, the one
tools/ground_mesh_truth.py obj writes from a shipped bake and the one
ground_mesh_compare_test --obj writes from the regenerated mesh, and draws them
in the same four orthographic views at the same scale.

    python3 tools/ground_mesh_view.py vanilla.obj generated.obj --out cmp.html

Families are coloured by what the face *is*, not by which OBJ group it came
from, because vanilla chunks its bake into ~20-triangle submeshes while the
generator emits one submesh per strip — grouping by `g` would compare the
chunker against the generator instead of the shapes. A face is a CAP when its
normal is Z-dominant, and a WALL otherwise; a wall is RIM when it reaches the
mesh's outermost |z| (the brim, half + HatWidthOffset) and RIBBON when it does
not. Backfaces are culled, so what is drawn is what a viewer would see.
"""

import argparse
import html
import math
import os
import sys

# view name -> (screen u axis, screen v axis, painter depth)
# depth is a dot product with the camera's *towards-viewer* axis, so larger is
# nearer and sorting ascending draws far faces first.
VIEWS = [
    ("top (x,y)", (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
    ("front (x,z)", (1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0)),
    ("side (y,z)", (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
    ("iso", (0.866, -0.866, 0.0), (0.5, 0.5, 1.0), (0.577, -0.577, 0.577)),
]

FAMILIES = ["cap", "ribbon", "rim"]
BASE_RGB = {"cap": (0.42, 0.78, 0.45), "ribbon": (0.55, 0.62, 0.74), "rim": (0.90, 0.60, 0.28)}


def read_obj(path):
    """Vertices and faces, with each face tagged by family.

    Normals come from the geometry rather than the file's `vn` lines: a bake and
    a generator can carry different normal conventions, and the family a face
    belongs to must not depend on whose convention it is.
    """
    verts = []
    faces = []            # (i0, i1, i2, family, n)
    groups = 0
    with open(path) as fh:
        for line in fh:
            if line.startswith("v "):
                p = line.split()
                verts.append((float(p[1]), float(p[2]), float(p[3])))
            elif line.startswith("g "):
                groups += 1
            elif line.startswith("f "):
                idx = []
                for tok in line.split()[1:]:
                    idx.append(int(tok.split("/")[0]) - 1)
                for k in range(1, len(idx) - 1):
                    faces.append((idx[0], idx[k], idx[k + 1]))
    if not verts or not faces:
        raise SystemExit("no geometry in %s" % path)

    out = []
    for a, b, c in faces:
        va, vb, vc = verts[a], verts[b], verts[c]
        n = _normal(va, vb, vc)
        if n is None:
            continue                      # degenerate triangle: nothing to draw
        fam = "cap" if abs(n[2]) > max(abs(n[0]), abs(n[1])) else "wall"
        zmax = max(abs(va[2]), abs(vb[2]), abs(vc[2]))
        out.append((va, vb, vc, n, fam, zmax))

    # The outermost |z| is the rim's brim; anything lower is the plain ribbon.
    ztop = max(f[5] for f in out)
    tagged = []
    for va, vb, vc, n, fam, zmax in out:
        if fam == "wall":
            fam = "rim" if zmax >= ztop - 1e-3 else "ribbon"
        tagged.append((va, vb, vc, n, fam))
    return {"verts": verts, "faces": tagged, "groups": groups, "path": path,
            "zlo": min(v[2] for v in verts), "zhi": max(v[2] for v in verts)}


def _normal(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    ln = math.sqrt(nx * nx + ny * ny + nz * nz)
    if ln < 1e-12:
        return None
    return (nx / ln, ny / ln, nz / ln)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def node_z_by_xy(mesh, tol=1e-3):
    """(x, y, back, front, hits) per outline XY, from the mesh itself.

    `back`/`front` are the PLAIN plane — the deepest non-positive z and the
    shallowest non-negative one on that column — not the extreme pair. On a
    MeshType 1 object the extremes on a rim node are the RIM's planes
    (SurfaceWidth/2 +- HatWidthOffset1), so comparing extremes reports a phantom
    HatWidthOffset1 of error on a mesh that is exact. This is the same rule
    boulderx::recover_node_depths and tools/ground_mesh_truth.py's
    ribbon_depths() use, and matching it is what makes this table and the C++
    harness agree.

    Columns are bucketed at `tol` (engine-written floats), so two nodes closer
    together than that are one node here — which is why the harness matches at
    1e-3 and this does too rather than at a loose tolerance that would merge a
    node with its inset rim ring.
    """
    buckets = {}
    for va, vb, vc, _n, _fam in mesh["faces"]:
        for v in (va, vb, vc):
            key = (round(v[0] / tol), round(v[1] / tol))
            b = buckets.setdefault(key, {"x": v[0], "y": v[1], "pos": [], "neg": []})
            if v[2] > 0:
                b["pos"].append(v[2])
            elif v[2] < 0:
                b["neg"].append(v[2])
            else:
                b["pos"].append(0.0)
                b["neg"].append(0.0)
    rows = []
    for key, b in buckets.items():
        back = max(b["neg"]) if b["neg"] else None
        front = min(b["pos"]) if b["pos"] else None
        rows.append((b["x"], b["y"], back, front, len(b["pos"]) + len(b["neg"])))
    rows.sort(key=lambda r: (r[0], r[1]))
    return rows


def render_panel(mesh, view, box, w, h, pad=6.0):
    """One SVG <g> for one mesh in one view."""
    (u_ax, v_ax, d_ax) = view
    umin, umax, vmin, vmax = box
    su = (w - 2 * pad) / max(1e-9, umax - umin)
    sv = (h - 2 * pad) / max(1e-9, vmax - vmin)
    s = min(su, sv)
    # Centre the drawing inside the panel.
    ou = pad + ((w - 2 * pad) - (umax - umin) * s) / 2.0
    ov = pad + ((h - 2 * pad) - (vmax - vmin) * s) / 2.0

    def project(v):
        u = _dot(v, u_ax)
        vv = _dot(v, v_ax)
        return ou + (u - umin) * s, ov + (vmax - vv) * s

    light = (0.34, -0.52, 0.78)
    ln = math.sqrt(sum(c * c for c in light))
    light = tuple(c / ln for c in light)

    drawn = []
    for va, vb, vc, n, fam in mesh["faces"]:
        if _dot(n, d_ax) <= 0.0:
            continue                       # backface
        depth = (_dot(va, d_ax) + _dot(vb, d_ax) + _dot(vc, d_ax)) / 3.0
        drawn.append((depth, va, vb, vc, n, fam))
    drawn.sort(key=lambda t: t[0])

    parts = []
    for depth, va, vb, vc, n, fam in drawn:
        albedo = BASE_RGB[fam]
        lam = max(0.18, _dot(n, light))
        face = _dot(n, d_ax)
        # Face-on faces read brightest; the exponent keeps glancing faces from
        # washing out the family colour that makes them identifiable.
        shade = 0.35 + 0.65 * lam * (0.55 + 0.45 * face)
        rgb = tuple(min(255, int(255 * c * shade)) for c in albedo)
        pts = " ".join("%.1f,%.1f" % project(v) for v in (va, vb, vc))
        parts.append('<polygon points="%s" fill="#%02x%02x%02x" stroke="none"/>'
                     % (pts, rgb[0], rgb[1], rgb[2]))
    return "\n".join(parts)


def bounds_for(meshes, view):
    (u_ax, v_ax, _d) = view
    us, vs = [], []
    for m in meshes:
        for v in m["verts"]:
            us.append(_dot(v, u_ax))
            vs.append(_dot(v, v_ax))
    return min(us), max(us), min(vs), max(vs)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("objs", nargs="+", help="OBJ files, first is the reference")
    ap.add_argument("--labels", default=None,
                    help="comma-separated panel labels, in the same order")
    ap.add_argument("--out", default="ground_mesh_view.html")
    ap.add_argument("--width", type=int, default=520)
    ap.add_argument("--height", type=int, default=420)
    args = ap.parse_args(argv)

    meshes = [read_obj(p) for p in args.objs]
    labels = (args.labels.split(",") if args.labels
              else [os.path.basename(p) for p in args.objs])
    if len(labels) != len(meshes):
        raise SystemExit("--labels needs one name per OBJ")

    # One scale for every panel: a per-panel fit would hide exactly the
    # size difference this is meant to show.
    # One scale for every panel: a per-panel fit would hide exactly the size
    # difference this is meant to show. The box is shared across the meshes too,
    # so the two columns are directly comparable.
    panels = []
    for name, u_ax, v_ax, d_ax in VIEWS:
        view = (u_ax, v_ax, d_ax)
        box = bounds_for(meshes, view)
        row = [render_panel(m, view, box, args.width, args.height) for m in meshes]
        panels.append((name, row))

    # The per-node boundary numbers are NOT computed here.
    #
    # They need the polygon, and only the harness has it: a ground object's baked
    # vertices include the rim's inset ring and its 3-unit corner shifts, so a
    # node set inferred from XY columns mixes outline nodes with rim interior and
    # then reports the rim's plane as a node's depth. tools/ground_mesh_truth.py
    # emits the polygon as `#section polygon` precisely so the C++ side can match
    # against it. One implementation, in the place that has the input.
    report = ""
    if len(meshes) >= 2:
        report += ("<h2>the numbers</h2><p>This sheet is the picture. The per-node "
                   "boundary comparison, the UV residuals and the strip routing are "
                   "reported by the harness, which is the side that has the polygon "
                   "to match against:</p>"
                   "<pre>python3 tools/ground_mesh_truth.py tsv --scene &lt;scene&gt; "
                   "--object &lt;id&gt; --out /tmp/o.tsv\n"
                   "./bin/tests/ground_mesh_compare_test /tmp/o.tsv "
                   "[--depth-source engine|outline] [--obj out.obj]</pre>"
                   "<p>Exit status is the verdict: the corpus run is 12 shipped cave "
                   "objects, each regenerated and diffed against its own bake.</p>")

    legend = "  ".join(
        '<span class="sw" style="background:#%02x%02x%02x"></span>%s'
        % (int(255 * BASE_RGB[f][0]), int(255 * BASE_RGB[f][1]), int(255 * BASE_RGB[f][2]), f)
        for f in FAMILIES)

    head = "".join(
        "<th>%s</th>" % html.escape(html.escape(l)) for l in labels)
    rows = ""
    for name, row in panels:
        rows += "<tr><th>%s</th>" % html.escape(name)
        for svg in row:
            # Responsive: the viewBox plus a max-width keeps both columns on
            # screen at any panel size, which matters because the whole point is
            # comparing the two columns at one scale.
            rows += ('<td><svg viewBox="0 0 %d %d" preserveAspectRatio="xMidYMid meet"'
                     ' style="width:100%%;max-width:%dpx">%s</svg></td>'
                     % (args.width, args.height, args.width, svg))
        rows += "</tr>"

    stats = ""
    for m, l in zip(meshes, labels):
        stats += ("<tr><td>%s</td><td>%d</td><td>%d</td><td>%.2f..%.2f</td>"
                  "<td>%d</td></tr>"
                  % (html.escape(l), len(m["verts"]), len(m["faces"]),
                     m["zlo"], m["zhi"], m["groups"]))

    doc = """<!doctype html>
<html><head><meta charset="utf-8"><title>ground mesh comparison</title>
<style>
 body { background:#14161a; color:#dfe3ea; font:13px/1.5 ui-monospace,monospace; margin:16px; }
 h1,h2 { font-size:15px; font-weight:600; margin:14px 0 6px; }
 table { border-collapse:collapse; }
 th,td { border:1px solid #2a2f38; padding:2px 7px; text-align:right; }
 th { text-align:left; color:#9aa4b2; font-weight:500; }
 tr.bad td { background:#3a1d1d; color:#ffb4a8; }
 svg { background:#0d0f12; border:1px solid #252b34; display:block; height:auto; }
 table.panels { width:100%%; }
 table.panels td { width:45%%; }
 table.panels th:first-child { width:6%%; vertical-align:middle; }
 .sw { display:inline-block; width:11px; height:11px; margin-right:5px;
       vertical-align:-1px; border:1px solid #000; }
 table.panels th:first-child, table.panels td { vertical-align:top; }
</style></head>
<body>
<h1>ground mesh comparison</h1>
<p>Same four orthographic views, one shared scale per view, backfaces culled.
%s</p>
<p>%s</p>
<table>%s</table>
<table class="panels"><tr><th></th>%s</tr>%s</table>
%s
</body></html>
""" % (legend, " · ".join(
        '%s: %d verts in %d faces, z %.2f..%.2f, %d obj groups'
        % (html.escape(l), len(m["verts"]), len(m["faces"]), m["zlo"], m["zhi"], m["groups"])
        for m, l in zip(meshes, labels)), stats, head, rows, report)

    with open(args.out, "w") as fh:
        fh.write(doc)
    print(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
