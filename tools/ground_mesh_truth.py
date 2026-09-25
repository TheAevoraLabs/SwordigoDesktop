#!/usr/bin/env python3
"""ground_mesh_truth.py — the ground truth for a vanilla ground mesh.

The generator in src/tools/boulderx.cpp claims engine parity, and the only
authority for that claim is the meshes the shipped engine baked. Those are
present in full inside Scener/data/decoded_scenes/*.scene: every ground object
there carries its GroundPolygon (the outline the modder drew, X/Y only), its
GroundMeshGeneratorComponent (the parameters), and the baked GroundMesh — every
submesh's vertices, normals, UVs, indices and material *as bytes*, in
VertexData/IndexData. So the outline, the parameters and the result are all in
one file, which is exactly what a comparison needs.

This tool decodes that and reports it. What it reports is chosen by what the
port keeps getting wrong, so each section answers a question:

  * ``submesh`` — the strip *order*, each strip's kind (SurfaceMesh/FrontMesh),
    its material, and **which of the two TextureMappings its UVs actually
    follow**. That last one is the honest test of "the surface texture is applied
    to every face": the two mappings in an object usually carry the *same texture
    name*, so comparing material names cannot tell them apart. The UVs can: a
    mapping with Scale S is consumed as ``u = x / S``, so the regression slope of
    u against x separates them even when the names are identical.
  * ``nodez`` — for each polygon node, the Z of every baked vertex sitting on
    that node's XY. The engine bakes one vertex per node at the node's exact XY,
    so this *is* the engine's per-node depth array (see boulderx.h). It is the
    relief a regenerated mesh has to reproduce, and the numbers that decide
    whether a generator "over-increases the Z distance".
  * ``envelope`` — |Z| against the polygon's own MinDepth/MaxDepth and the
    generator's SurfaceWidth/HatHeight. The complaint that a regenerated mesh
    balloons in Z is only checkable against these.

Usage
-----
    # the biggest ground meshes in the caves series
    python3 tools/ground_mesh_truth.py caves --top 5

    # one object, in full
    python3 tools/ground_mesh_truth.py report \
        --scene Scener/data/decoded_scenes/thecave_part1.scene --object 101

    # machine-readable, for the C++ side of the harness
    python3 tools/ground_mesh_truth.py tsv \
        --scene Scener/data/decoded_scenes/thecave_part1.scene --object 101 \
        --out .scratch/truth_thecave_part1_101.tsv

    # a .swdm sheet boulderx can regenerate the same object from
    python3 tools/ground_mesh_truth.py swdm \
        --scene ... --object 101 --out /tmp/obj101.swdm

The TSV is read by ``ground_mesh_compare`` (tests/ground_mesh_compare_test.cpp),
which runs boulderx::generate on the same outline and parameters and diffs the
two. The Python half is deliberately self-contained rather than importing the
scratch census in .scratch/: a harness the parity test depends on should not live
in a directory whose name says it is disposable.
"""
from __future__ import annotations

import argparse
import glob
import math
import os
import re
import struct
import sys

# ── FileRift markup decoding ────────────────────────────────────────────────
# The decoder in src/tools/filerift.cpp writes byte strings escaped; the format
# is its own, and these four helpers are the whole of it for our purposes.

TOKEN = re.compile(rb"\\(?:x[0-9a-fA-F]{2}|['\"\\nrt\\])|.", re.S)


def unescape(tok: bytes) -> bytes:
    if tok[:1] in (b"'", b'"'):
        tok = tok[1:-1]
    out = bytearray()
    i = 0
    while i < len(tok):
        c = tok[i]
        if c == 0x5C and i + 1 < len(tok):
            n = tok[i + 1]
            if n == ord("n"):
                out.append(10); i += 2; continue
            if n == ord("r"):
                out.append(13); i += 2; continue
            if n == ord("t"):
                out.append(9); i += 2; continue
            if n in (0x27, 0x22, 0x5C):
                out.append(n); i += 2; continue
            if n == ord("x") and i + 3 < len(tok):
                try:
                    out.append(int(tok[i + 2:i + 4], 16)); i += 4; continue
                except ValueError:
                    pass
            out.append(0x5C); i += 1; continue
        out.append(c); i += 1
    return bytes(out)


def obj_blocks(raw: bytes):
    """Split a decoded scene into its top-level Object{...} blocks."""
    return raw.split(b"Object{")[1:]


def read_str(block: bytes, key: bytes):
    m = re.search(rb"\b" + key + rb"\s*:\s*'", block)
    if not m:
        return None
    i = m.end() - 1
    j = i + 1
    while j < len(block):
        if block[j:j + 1] == b"\\":
            j += 2
            continue
        if block[j:j + 1] == b"'":
            break
        j += 1
    return unescape(block[i:j + 1])


def read_num(block: bytes, key: bytes, start: int = 0):
    m = re.search(rb"\b" + key + rb"\s*:\s*(-?[\d.eE+-]+)", block[start:])
    if not m:
        return None
    try:
        return float(m.group(1))
    except ValueError:
        return None


def find_block(block: bytes, name: bytes, start: int = 0):
    """(body, end) of name{...} by brace matching, or (None, start).

    Single-quoted regions are skipped, and they have to be: VertexData carries
    escaped bytes and filerift passes 32..126 through literally, so a '{' or '}'
    inside one is data. Counting it as structure mis-delimits the block and
    yields an empty mesh rather than an error.
    """
    m = re.search(rb"\b" + name + rb"\s*\{", block[start:])
    if not m:
        return None, start
    b = start + m.end()
    depth = 1
    i = b
    while i < len(block) and depth:
        c = block[i:i + 1]
        if c == b"'":
            i += 1
            while i < len(block):
                if block[i:i + 1] == b"\\":
                    i += 2
                    continue
                if block[i:i + 1] == b"'":
                    i += 1
                    break
                i += 1
            continue
        if c == b"{":
            depth += 1
        elif c == b"}":
            depth -= 1
        i += 1
    return block[b:i - 1], i


def _escaped_blob(body: bytes, key: bytes):
    m = re.search(key + rb"\s*:\s*'", body)
    if not m:
        return None
    i = m.end() - 1
    j = i + 1
    while j < len(body) and body[j:j + 1] != b"'":
        if body[j:j + 1] == b"\\":
            j += 2
            continue
        j += 1
    return unescape(body[i:j + 1])


def submesh_info(body: bytes) -> dict:
    """One Mesh submessage → vertices/normals/uvs/indices + material name."""
    nv = read_num(body, b"NumVertices")
    nf = read_num(body, b"NumFaces")
    voff = re.search(rb"\bVertices\s*\{", body)
    stride = read_num(body, b"Stride", voff.end() if voff else 0)
    noff = re.search(rb"\bNormals\s*\{", body)
    toff = re.search(rb"\bTexCoordSet\s*\{", body)
    nofs = read_num(body, b"DataOffset", noff.end() if noff else 0)
    tofs = read_num(body, b"DataOffset", toff.end() if toff else 0)
    tex = None
    tm = re.search(rb"Texture\s*\{", body)
    if tm:
        tb, _ = find_block(body, b"Texture", tm.start())
        if tb is not None:
            nm = read_str(tb, b"Name")
            tex = nm.decode("latin1") if nm else None

    verts = []
    blob = _escaped_blob(body, rb"VertexData")
    if blob is not None and nv and stride:
        for k in range(int(nv)):
            o = k * int(stride)
            if o + 12 > len(blob):
                break
            px, py, pz = struct.unpack_from("<fff", blob, o)
            nx = ny = nz = 0.0
            if noff is not None and o + int(nofs) + 12 <= len(blob):
                nx, ny, nz = struct.unpack_from("<fff", blob, o + int(nofs))
            u = v = 0.0
            if toff is not None and o + int(tofs) + 8 <= len(blob):
                u, v = struct.unpack_from("<ff", blob, o + int(tofs))
            verts.append((px, py, pz, nx, ny, nz, u, v))

    ind = []
    ib = _escaped_blob(body, rb"IndexData")
    if ib is not None and nf:
        n = int(nf) * 3
        if len(ib) >= n * 2:
            ind = list(struct.unpack_from("<%dH" % n, ib, 0))
    return {
        "nv": int(nv) if nv else 0,
        "nf": int(nf) if nf else 0,
        "stride": int(stride) if stride else 0,
        "tex": tex,
        "verts": verts,
        "indices": ind,
    }


def parse_polygon(gp: bytes):
    pts = []
    for vm in re.finditer(rb"Vertex\s*\{", gp):
        body, _ = find_block(gp, b"Vertex", vm.start())
        if body is None:
            continue
        pts.append((read_num(body, b"X"), read_num(body, b"Y")))
    return pts


# ── one ground object, fully decoded ────────────────────────────────────────

def load_object(scene_path: str, want_ident=None, min_nodes: int = 0):
    """Decode every ground object in a decoded scene. Returns a list of dicts."""
    raw = open(scene_path, "rb").read()
    out = []
    for blk in obj_blocks(raw):
        if b"GroundMeshGenerator" not in blk:
            continue
        ident = read_str(blk, b"Identifier")
        ident = ident.decode("latin1") if ident else "?"
        if want_ident is not None and str(ident) != str(want_ident):
            continue
        gp, _ = find_block(blk, b"GroundPolygonComponent")
        pts = parse_polygon(gp) if gp else []
        if len(pts) < min_nodes:
            continue
        gen, _ = find_block(blk, b"GroundMeshGeneratorComponent")
        gm, _ = find_block(blk, b"GroundMeshComponent")

        tm = {}
        for m in re.finditer(rb"ClassName\s*:\s*'TextureMapping'", blk):
            seg = blk[m.start():m.start() + 400]
            tmid = read_num(seg, b"Identifier")
            tname = read_str(seg, b"TextureName")
            tscl = read_num(seg, b"Scale")
            tox = read_num(seg, b"Offset")
            tm[int(tmid) if tmid is not None else -1] = {
                "name": tname.decode("latin1") if tname else None,
                "scale": tscl,
            }

        subs = []
        if gm:
            for name in (b"SurfaceMesh", b"FrontMesh"):
                pos = 0
                while True:
                    body, end = find_block(gm, name, pos)
                    if body is None:
                        break
                    info = submesh_info(body)
                    info["kind"] = name.decode()
                    subs.append(info)
                    pos = end

        params = {}
        if gen:
            for k in (b"GroundPolygonId", b"TargetMeshId", b"FrontTextureMappingId",
                      b"SurfaceTextureMappingId", b"RandomSeed", b"HorizNoise",
                      b"MeshType", b"SurfaceWidth", b"HatHeight",
                      b"HatWidthOffset1", b"HatWidthOffset2"):
                params[k.decode()] = read_num(gen, k)

        aabb = {}
        for k in (b"X", b"Y", b"Width", b"Height"):
            aabb[k.decode()] = read_num(gm, k)

        out.append({
            "scene": scene_path,
            "identifier": ident,
            "polygon": pts,
            "min_depth": read_num(gp, b"MinDepth") if gp else None,
            "max_depth": read_num(gp, b"MaxDepth") if gp else None,
            "convex": read_num(gp, b"Closed") if gp else None,
            "closed": read_num(gp, b"Closed") if gp else None,
            "params": params,
            "mappings": tm,
            "mesh_aabb": aabb,
            "submeshes": subs,
        })
    return out


# ── the analysis ────────────────────────────────────────────────────────────

def uv_slopes(sm: dict):
    """Least-squares slope of u and v against x and y, over the submesh.

    A mapping with Scale S is consumed as `u = x / S` (equivalently `u = <distance
    along the strip> / S`), so the slope identifies which mapping a strip follows
    even when both mappings in the object name the same texture.
    """
    vs = sm["verts"]
    if len(vs) < 2:
        return None
    n = len(vs)

    def slope(get_y, get_x):
        sx = sum(get_x(v) for v in vs)
        sy = sum(get_y(v) for v in vs)
        sxx = sum(get_x(v) ** 2 for v in vs)
        sxy = sum(get_x(v) * get_y(v) for v in vs)
        den = n * sxx - sx * sx
        if abs(den) < 1e-9:
            return None
        return (n * sxy - sx * sy) / den

    return {
        "du_dx": slope(lambda v: v[6], lambda v: v[0]),
        "du_dy": slope(lambda v: v[6], lambda v: v[1]),
        "du_dz": slope(lambda v: v[6], lambda v: v[2]),
        "dv_dx": slope(lambda v: v[7], lambda v: v[0]),
        "dv_dy": slope(lambda v: v[7], lambda v: v[1]),
        "dv_dz": slope(lambda v: v[7], lambda v: v[2]),
    }


def classify_mapping(sm: dict, mappings: dict, params: dict) -> str:
    """Which TextureMapping this strip's UVs follow, by slope then by name.

    Name first, because when the two mappings differ the material name is
    definitive; slope second, because shipped objects routinely set both mappings
    to the *same* texture, and then only the UVs can say.

    The slope of 1/S is the signature: a strip laid out as `u = x / S` has
    du/dx = 1/S. So each mapping is scored by how close the strip's dominant
    horizontal slope is to its own 1/S.
    """
    sl = uv_slopes(sm)
    if sl is None:
        return "?"
    sid = params.get("SurfaceTextureMappingId")
    fid = params.get("FrontTextureMappingId")
    sname = mappings.get(int(sid), {}).get("name") if sid is not None else None
    fname = mappings.get(int(fid), {}).get("name") if fid is not None else None
    tex = sm.get("tex")

    # A definitive name difference settles it.
    if tex and fname and sname and fname != sname:
        if tex == sname and tex != fname:
            return "surface"
        if tex == fname and tex != sname:
            return "front"

    scales = []
    for which, mid in (("surface", sid), ("front", fid)):
        if mid is None:
            continue
        s = mappings.get(int(mid), {}).get("scale")
        if not s:
            continue
        want = 1.0 / s
        got = max(abs(sl["du_dx"] or 0.0), abs(sl["du_dy"] or 0.0),
                  abs(sl["dv_dx"] or 0.0), abs(sl["dv_dy"] or 0.0))
        if got > 1e-9:
            scales.append((abs(got - want) / want, which))
    if not scales:
        return "?"
    scales.sort()
    if len(scales) == 2 and abs(scales[0][0] - scales[1][0]) < 0.02:
        return "same-scale"
    return scales[0][1]


def node_depths(obj: dict, tol: float = 1e-3):
    """Per polygon node: the Z of every baked vertex on that node's own XY.

    The engine bakes one vertex per node at the node's exact XY (the front cap
    emits each ear corner there; the ribbon emits a back-plane and a front-plane
    vertex there), so a match is exact and the *set* of Z at one XY is the
    engine's depth stack for that node: the lowest is the back plane, the highest
    the front. Anything the tolerance excludes is rim/bevel geometry, which sits
    at the node's XY shifted inward and is deliberately not counted.
    """
    rows = []
    for idx, (nx, ny) in enumerate(obj["polygon"]):
        hits = []
        for sm in obj["submeshes"]:
            for v in sm["verts"]:
                if abs(v[0] - nx) <= tol and abs(v[1] - ny) <= tol:
                    hits.append((v[2], sm["kind"]))
        zs = sorted(h[0] for h in hits)
        rows.append({
            "node": idx,
            "x": nx,
            "y": ny,
            "back": zs[0] if zs else None,
            "front": zs[-1] if zs else None,
            "z_all": zs,
            "matched": len(zs),
        })
    return rows


def ribbon_depths(obj: dict, tol: float = 1e-3):
    """Per node, the PLAIN RIBBON's front/back z — the pair the generator's law
    governs.

    ``node_depths`` reports the extreme pair sitting on a node's own XY, and on a
    MeshType 1 object that pair is the *rim*'s plane (SurfaceWidth/2 +
    HatWidthOffset1), not the ribbon's (SurfaceWidth/2). Both planes are real and
    both are in the bake; the rim always sits strictly outside the ribbon — its
    outer edge is ``front + HatWidthOffset1`` of the same node — so the ribbon is
    the pair closest to z = 0. Comparing a regenerated mesh's ribbon against the
    rim plane instead reports a constant phantom error of HatWidthOffset1 (5.0 on
    every caves object), which is exactly what the old harness did.
    """
    out = []
    for r in node_depths(obj, tol):
        zs = r["z_all"]
        row = dict(r)
        row["back"] = max([z for z in zs if z <= 1e-6], default=None)
        row["front"] = min([z for z in zs if z >= -1e-6], default=None)
        out.append(row)
    return out


def envelope(obj: dict):
    half = (obj["max_depth"] or 0.0) / 2.0
    zlo = 1e18
    zhi = -1e18
    for sm in obj["submeshes"]:
        for v in sm["verts"]:
            zlo = min(zlo, v[2])
            zhi = max(zhi, v[2])
    if zlo > zhi:
        zlo = zhi = None
    return {
        "half_depth": half,
        "zlo": zlo,
        "zhi": zhi,
        "abs_max": max(abs(zlo), abs(zhi)) if zlo is not None else None,
        "beyond_half": (max(abs(zlo), abs(zhi)) - half) if zlo is not None else None,
    }


def report(obj: dict, full_verts: bool = False):
    p = obj["params"]
    env = envelope(obj)
    print("=" * 78)
    print("object %s   (%s)" % (obj["identifier"], os.path.basename(obj["scene"])))
    print("  polygon      : %d nodes  MinDepth=%s MaxDepth=%s"
          % (len(obj["polygon"]), obj["min_depth"], obj["max_depth"]))
    print("  generator    : " + " ".join(
        "%s=%g" % (k, v) for k, v in p.items() if v is not None))
    for mid, m in sorted(obj["mappings"].items()):
        role = []
        if p.get("SurfaceTextureMappingId") is not None and int(mid) == int(p["SurfaceTextureMappingId"]):
            role.append("surface")
        if p.get("FrontTextureMappingId") is not None and int(mid) == int(p["FrontTextureMappingId"]):
            role.append("front")
        print("  mapping %-3d  : %-24s scale=%s  role=%s"
              % (mid, m["name"], m["scale"], "/".join(role) or "-"))
    print("  envelope     : z=[%s, %s]  |z|max=%s  half-depth=%s  beyond=%s"
          % (fmt(env["zlo"]), fmt(env["zhi"]), fmt(env["abs_max"]),
             fmt(env["half_depth"]), fmt(env["beyond_half"])))
    print("  %-4s %-12s %5s %5s %-10s %-22s %-16s %s"
          % ("#", "kind", "nv", "nf", "mapping", "texture", "z", "normal"))
    for i, sm in enumerate(obj["submeshes"]):
        zs = [v[2] for v in sm["verts"]] or [0.0]
        ns = [sum(abs(v[k]) for v in sm["verts"]) for k in (3, 4, 5)] or [0, 0, 0]
        nax = "xyz"[max(range(3), key=lambda k: ns[k])]
        print("  %-4d %-12s %5d %5d %-10s %-22s [%7s,%7s] %s-axis"
              % (i, sm["kind"], sm["nv"], sm["nf"],
                 classify_mapping(sm, obj["mappings"], p),
                 sm["tex"], fmt(min(zs)), fmt(max(zs)), nax))
        if full_verts:
            for j, v in enumerate(sm["verts"]):
                print("        v%-4d (%9.3f %9.3f %9.3f)  n=(%6.3f %6.3f %6.3f)  uv=(%8.3f %8.3f)"
                      % ((j,) + v))
            if sm["indices"]:
                print("        idx %s" % (sm["indices"][:36],))
    rows = node_depths(obj)
    matched = sum(1 for r in rows if r["matched"])
    print("  node depths  : %d/%d nodes have a baked vertex on their own XY"
          % (matched, len(rows)))
    span = [r for r in rows if r["front"] is not None and r["back"] is not None]
    if span:
        print("  %-5s %-11s %-11s %-10s %-10s %s"
              % ("node", "x", "y", "back", "front", "verts"))
        for r in rows:
            print("  %-5d %-11s %-11s %-10s %-10s %d"
                  % (r["node"], fmt(r["x"], 1), fmt(r["y"], 1), fmt(r["back"]),
                     fmt(r["front"]), r["matched"]))
        fronts = [r["front"] for r in span]
        backs = [r["back"] for r in span]
        print("  relief       : front %.2f..%.2f  back %.2f..%.2f  (front span %.2f)"
              % (min(fronts), max(fronts), min(backs), max(backs),
                 max(fronts) - min(fronts)))
        rows = ribbon_depths(obj)
        span = [r for r in rows if r["front"] is not None and r["back"] is not None]
        if span:
            fronts = [r["front"] for r in span]
            backs = [r["back"] for r in span]
            print("  ribbon plane : front %.2f..%.2f  back %.2f..%.2f  (the pair the "
                  "depth law produces; the rest of `relief` is the rim)"
                  % (min(fronts), max(fronts), min(backs), max(backs)))


def fmt(v, nd: int = 2):
    if v is None:
        return "-"
    return ("%%.%df" % nd) % v


# ── TSV, for the C++ half of the harness ────────────────────────────────────

def write_tsv(obj: dict, path: str):
    p = obj["params"]
    env = envelope(obj)
    lines = []

    def kv(k, v):
        lines.append("%s\t%s" % (k, "" if v is None else v))

    lines.append("#section\tobject")
    kv("scene", os.path.basename(obj["scene"]))
    kv("identifier", obj["identifier"])
    kv("nodes", len(obj["polygon"]))
    kv("min_depth", obj["min_depth"])
    kv("max_depth", obj["max_depth"])
    for k in ("MeshType", "SurfaceWidth", "HatHeight", "HatWidthOffset1",
              "HatWidthOffset2", "HorizNoise", "RandomSeed",
              "SurfaceTextureMappingId", "FrontTextureMappingId"):
        kv(k, p.get(k))
    sid, fid = p.get("SurfaceTextureMappingId"), p.get("FrontTextureMappingId")
    kv("surface_tex", obj["mappings"].get(int(sid), {}).get("name") if sid is not None else None)
    kv("front_tex", obj["mappings"].get(int(fid), {}).get("name") if fid is not None else None)
    kv("surface_scale", obj["mappings"].get(int(sid), {}).get("scale") if sid is not None else None)
    kv("front_scale", obj["mappings"].get(int(fid), {}).get("scale") if fid is not None else None)
    kv("half_depth", env["half_depth"])
    kv("vanilla_zlo", env["zlo"])
    kv("vanilla_zhi", env["zhi"])

    lines.append("#section\tpolygon")
    lines.append("node\tx\ty")
    for i, (x, y) in enumerate(obj["polygon"]):
        lines.append("%d\t%.6f\t%.6f" % (i, x, y))

    # The ribbon plane, not the extreme pair: see ribbon_depths().
    lines.append("#section\tnodez")
    lines.append("node\tback\tfront\tmatched")
    for r in ribbon_depths(obj):
        lines.append("%d\t%s\t%s\t%d"
                     % (r["node"], "" if r["back"] is None else "%.6f" % r["back"],
                        "" if r["front"] is None else "%.6f" % r["front"], r["matched"]))

    lines.append("#section\tsubmesh")
    lines.append("index\tkind\tnv\tnf\tindexed\tmapping\ttexture\tzlo\tzhi\tnormal_axis")
    for i, sm in enumerate(obj["submeshes"]):
        zs = [v[2] for v in sm["verts"]]
        ns = [sum(abs(v[k]) for v in sm["verts"]) for k in (3, 4, 5)]
        nax = "xyz"[max(range(3), key=lambda k: ns[k])] if sm["verts"] else "?"
        lines.append("\t".join(str(x) for x in (
            i, sm["kind"], sm["nv"], sm["nf"],
            0 if sm["kind"] == "FrontMesh" else 1,
            classify_mapping(sm, obj["mappings"], p), sm["tex"],
            "%.6f" % min(zs) if zs else "", "%.6f" % max(zs) if zs else "", nax)))

    # The bake's u at each node's own XY, as a set.
    #
    # u is a per-NODE parameter — the ribbon's is arc length along the outline and
    # the cap's is planar (0.5 + x/scale) — so every vertex standing on a node
    # carries a u that belongs to that node. Reporting the set lets a generator be
    # compared against the bake. The alternative, checking the generated ribbon's u
    # for internal consistency with its own arc, needs a ring-direction guess and
    # reports a residual on meshes whose u matches the bake exactly.
    lines.append("#section\tnodeu")
    lines.append("node\tcount\tu...")
    for i, (nx, ny) in enumerate(obj["polygon"]):
        us = sorted({round(v[6], 4) for sm in obj["submeshes"] for v in sm["verts"]
                     if abs(v[0] - nx) <= 1e-3 and abs(v[1] - ny) <= 1e-3})
        lines.append("%d\t%d\t%s" % (i, len(us), " ".join("%.4f" % u for u in us)))

    # How many cap triangles stand on each node, in the bake.
    #
    # This is the bake's triangulation "concentration", and it is the number that
    # catches the reported crumble: on thecave_part15 obj9#10 the bake's busiest
    # node carries 8 of its 57 cap triangles, while the recovered-walk port put 29
    # of 57 on node 10 — one apex spanning the whole band, which is what a single
    # point collapsing every curvature and depth looks like. The cap is the
    # FrontMesh family, whose vertices stand exactly on the nodes (a FrontMesh is a
    # non-indexed triangle list, so one corner per triangle is one more use).
    lines.append("#section\tcapdeg")
    lines.append("node\tcap_triangles")
    caps = [sm for sm in obj["submeshes"] if sm["kind"] == "FrontMesh"]
    deg = [0] * len(obj["polygon"])
    for sm in caps:
        for v in sm["verts"]:
            best, best_d = 0, None
            for i, (nx, ny) in enumerate(obj["polygon"]):
                d = (v[0] - nx) ** 2 + (v[1] - ny) ** 2
                if best_d is None or d < best_d:
                    best, best_d = i, d
            deg[best] += 1
    for i, c in enumerate(deg):
        lines.append("%d\t%d" % (i, c))

    lines.append("#section\tvertices")
    lines.append("submesh\tindex\tx\ty\tz\tnx\tny\tnz\tu\tv")
    for i, sm in enumerate(obj["submeshes"]):
        for j, v in enumerate(sm["verts"]):
            lines.append("%d\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f"
                         % (i, j, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]))

    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def _g(v):
    """A number as text, without Python's exponent/None surprises.

    The C++ reader uses `atof`, so `None` must become an empty cell and not the
    string "None" — which atof reads as 0.0, silently and wrongly.
    """
    if v is None:
        return ""
    return "%.6f" % float(v)


def dump_full(obj: dict, path: str) -> str:
    """The complete bake, in a form the C++ harness can diff vertex for vertex.

    Not a summary. Every submesh's positions, normals, UVs and indices, the
    outline, and the generator parameters — so a port can be held to EXACT
    agreement (vertex count, index list, triangle set) instead of to statistics
    that two visibly different meshes can both satisfy. The per-vertex `node`
    column is what turns the cap into a *rule*: a cap corner sits on its node's
    own XY, so the bake's cap triangle set can be read as triples of outline
    indices, which is the thing that has to be reproduced.
    """
    p = obj["params"]
    sid = p.get("SurfaceTextureMappingId")
    fid = p.get("FrontTextureMappingId")
    sname = obj["mappings"].get(int(sid), {}).get("name") if sid is not None else None
    fname = obj["mappings"].get(int(fid), {}).get("name") if fid is not None else None
    sscale = obj["mappings"].get(int(sid), {}).get("scale") if sid is not None else None
    fscale = obj["mappings"].get(int(fid), {}).get("scale") if fid is not None else None
    poly = obj["polygon"]

    def nearest_node(x, y):
        best, best_d = -1, 1e30
        for i, (nx, ny) in enumerate(poly):
            d = (x - nx) ** 2 + (y - ny) ** 2
            if d < best_d:
                best_d, best = d, i
        return best, best_d ** 0.5

    lines = []

    def w(*cells):
        lines.append("\t".join("" if c is None else str(c) for c in cells))

    w("object", obj["identifier"], os.path.basename(obj["scene"]),
      int(p.get("MeshType") or 0))
    for key, val in (("SurfaceWidth", p.get("SurfaceWidth")),
                     ("HatHeight", p.get("HatHeight")),
                     ("HatWidthOffset1", p.get("HatWidthOffset1")),
                     ("HatWidthOffset2", p.get("HatWidthOffset2")),
                     ("HorizNoise", p.get("HorizNoise")),
                     ("RandomSeed", int(p.get("RandomSeed") or 0)),
                     ("MinDepth", obj["min_depth"]),
                     ("MaxDepth", obj["max_depth"]),
                     ("surface_scale", sscale), ("front_scale", fscale)):
        w("param", key, _g(val))
    w("param", "surface_tex", sname or "")
    w("param", "front_tex", fname or "")

    w("nodes", len(poly))
    ribbons = ribbon_depths(obj)
    for i, (x, y) in enumerate(poly):
        r = ribbons[i]
        zs = r["z_all"]
        w("node", i, _g(x), _g(y), _g(r["back"]), _g(r["front"]),
          _g(zs[0] if zs else None), _g(zs[-1] if zs else None))

    w("submeshes", len(obj["submeshes"]))
    for i, sm in enumerate(obj["submeshes"]):
        tex = sm["tex"] or ""
        scale = sscale if tex == sname else (fscale if tex == fname else None)
        w("submesh", i, sm["kind"], tex, 1 if sm["indices"] else 0,
          sm["nv"], sm["nf"], _g(scale),
          classify_mapping(sm, obj["mappings"], p))
        for k, v in enumerate(sm["verts"]):
            n, d = nearest_node(v[0], v[1])
            w("vert", i, k, _g(v[0]), _g(v[1]), _g(v[2]), _g(v[3]), _g(v[4]),
              _g(v[5]), _g(v[6]), _g(v[7]), n, _g(round(d, 6)))
        ind = sm["indices"]
        if not ind:
            ind = list(range(len(sm["verts"])))
        for k in range(0, len(ind) - 2, 3):
            tri = []
            for v in (ind[k], ind[k + 1], ind[k + 2]):
                tri.append(nearest_node(sm["verts"][v][0], sm["verts"][v][1])[0])
            w("tri", i, k // 3, ind[k], ind[k + 1], ind[k + 2], *tri)
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def write_obj(obj: dict, path: str):
    """The baked mesh as OBJ, one `g` group per submesh.

    The subset is here because a shape claim ("every side is wrapped",
    "the vertices collapse to a point") cannot be settled from per-strip
    statistics — the strips have to be looked at. Group names carry the submesh
    index, kind and material so the viewer can colour by family.
    """
    lines = ["# %s %s — baked GroundMesh" % (os.path.basename(obj["scene"]),
                                              obj["identifier"])]
    base = 0
    for i, sm in enumerate(obj["submeshes"]):
        lines.append("g %d_%s_%s" % (i, sm["kind"], sm["tex"] or "none"))
        for v in sm["verts"]:
            lines.append("v %.6f %.6f %.6f" % (v[0], v[1], v[2]))
        for v in sm["verts"]:
            lines.append("vn %.6f %.6f %.6f" % (v[3], v[4], v[5]))
        n = len(sm["verts"])
        idx = sm["indices"]
        if idx:
            for f in range(0, len(idx) - 2, 3):
                t = [base + idx[f + k] + 1 for k in range(3)]
                lines.append("f %d//%d %d//%d %d//%d" % (t[0], t[0], t[1], t[1], t[2], t[2]))
        else:
            for f in range(0, n - 2, 3):
                t = [base + f + 1, base + f + 2, base + f + 3]
                lines.append("f %d//%d %d//%d %d//%d" % (t[0], t[0], t[1], t[1], t[2], t[2]))
        base += n
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def write_swdm(obj: dict, path: str):
    """A boulderx v2 sheet for this object, depths taken from the baked mesh."""
    p = obj["params"]
    rows = node_depths(obj)
    lines = []
    lines.append("# boulderx ground sheet (v2) — per-node depth")
    lines.append("identifier %s" % obj["identifier"])
    lines.append("mesh_type %d" % int(p.get("MeshType") or 0))
    lines.append("surface_width %g" % (p.get("SurfaceWidth") or 200.0))
    lines.append("hat_height %g" % (p.get("HatHeight") or 0.0))
    lines.append("hat_width_offset_1 %g" % (p.get("HatWidthOffset1") or 0.0))
    lines.append("hat_width_offset_2 %g" % (p.get("HatWidthOffset2") or 0.0))
    lines.append("horiz_noise %g" % (p.get("HorizNoise") or 0.0))
    lines.append("random_seed %d" % int(p.get("RandomSeed") or 0))
    lines.append("surface_texture %s" % (obj["mappings"].get(
        int(p.get("SurfaceTextureMappingId") or -1), {}).get("name") or "surface"))
    lines.append("front_texture %s" % (obj["mappings"].get(
        int(p.get("FrontTextureMappingId") or -1), {}).get("name") or "front"))
    lines.append("scale %g" % (obj["mappings"].get(
        int(p.get("SurfaceTextureMappingId") or -1), {}).get("scale") or 250.0))
    lines.append("Polygon[")
    for r in rows:
        front = r["front"] if r["front"] is not None else (obj["max_depth"] or 0) / 2.0
        back = r["back"] if r["back"] is not None else -(obj["max_depth"] or 0) / 2.0
        lines.append("  %g %g %g %g" % (r["x"], r["y"], front, back))
    lines.append("]")
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


# ── CLI ─────────────────────────────────────────────────────────────────────

def pick_caves(top: int, min_nodes: int):
    """The biggest ground meshes in the caves series, by vertex count."""
    paths = sorted(glob.glob("Scener/data/decoded_scenes/thecave*.scene"))
    paths += sorted(glob.glob("Scener/data/decoded_scenes/*cave*.scene"))
    seen = set()
    found = []
    for path in paths:
        if path in seen:
            continue
        seen.add(path)
        try:
            objs = load_object(path, min_nodes=min_nodes)
        except Exception as exc:                      # noqa: BLE001 - report and go on
            print("skip %s: %s" % (path, exc), file=sys.stderr)
            continue
        for obj in objs:
            nv = sum(sm["nv"] for sm in obj["submeshes"])
            nf = sum(sm["nf"] for sm in obj["submeshes"])
            found.append((nv, nf, path, obj))
    found.sort(key=lambda t: -t[0])
    return found[:top]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    sub = ap.add_subparsers(dest="cmd")

    def common(p):
        p.add_argument("--scene", required=True)
        p.add_argument("--object", default=None)

    r = sub.add_parser("report", help="decode one object and print it")
    common(r)
    r.add_argument("--full", action="store_true", help="print every vertex and index")
    r.add_argument("--all", action="store_true", help="every ground object in the scene")

    t = sub.add_parser("tsv", help="machine-readable dump for the C++ harness")
    common(t)
    t.add_argument("--out", required=True)

    s = sub.add_parser("swdm", help="write a boulderx sheet for the object")
    common(s)
    s.add_argument("--out", required=True)

    o = sub.add_parser("obj", help="write the baked mesh as OBJ (one `g` per submesh)")
    common(o)
    o.add_argument("--out", required=True)

    d = sub.add_parser("dump",
                       help="the WHOLE bake (every vertex, normal, UV, index) for "
                            "the C++ bit-for-bit diff")
    common(d)
    d.add_argument("--out", required=True)

    c = sub.add_parser("caves", help="the biggest ground meshes in the caves series")
    c.add_argument("--top", type=int, default=5)
    c.add_argument("--min-nodes", type=int, default=8)
    c.add_argument("--tsv-dir", default=None,
                   help="also write <dir>/<scene>_<object>.tsv for each")

    args = ap.parse_args(argv)

    if args.cmd == "caves":
        picked = pick_caves(args.top, args.min_nodes)
        for nv, nf, path, obj in picked:
            print("%s  %s  nv=%d nf=%d nodes=%d"
                  % (os.path.basename(path), obj["identifier"], nv, nf,
                     len(obj["polygon"])))
            report(obj)
            if args.tsv_dir:
                out = os.path.join(args.tsv_dir, "%s_%s.tsv"
                                   % (os.path.splitext(os.path.basename(path))[0],
                                      obj["identifier"]))
                write_tsv(obj, out)
                print("  wrote %s" % out)
        return 0

    objs = load_object(args.scene, want_ident=args.object)
    if not objs:
        print("no ground object found (%s%s)"
              % (args.scene, "" if args.object is None else ", object %s" % args.object),
              file=sys.stderr)
        return 1
    if args.cmd == "report":
        for obj in (objs if getattr(args, "all", False) else objs[:1]):
            report(obj, full_verts=args.full)
        return 0
    if args.cmd == "tsv":
        print(write_tsv(objs[0], args.out))
        return 0
    if args.cmd == "swdm":
        print(write_swdm(objs[0], args.out))
        return 0
    if args.cmd == "obj":
        print(write_obj(objs[0], args.out))
        return 0
    if args.cmd == "dump":
        print(dump_full(objs[0], args.out))
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
