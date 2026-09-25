#!/usr/bin/env python3
"""convert_model_to_mesh.py — 3D Model to Swordigo GroundMesh Converter

Imports 3D models (.POD) and converts them into native Swordigo GroundMesh
SceneObjects with:
  1. Multi-material submeshes with texture mapping (SurfaceMesh)
  2. 32-byte vertex streams (position xyz, normal xyz, uv)
  3. Triangle indexing with optional two-sided winding
  4. Automatic 2D collision contour calculation (Monotone Chain Convex Hull)
     strictly counter-clockwise (CCW, signed_area > 0)
  5. Full Chipmunk 2D physics body integration (GroundPolygon + CollisionShape)
  6. Direct export to standalone .scene or injection into existing .scene files

Usage:
  python3 tools/convert_model_to_mesh.py <model.POD> [options]
"""

import os
import sys
import math
import struct
import argparse

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src', 'tools'))
from rubyforge.core.pod_reader import pod_load


# ── Protobuf Helpers ─────────────────────────────────────────────────────────

def encode_varint(val: int) -> bytes:
    out = bytearray()
    while True:
        b = val & 0x7f
        val >>= 7
        if val:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)

def encode_tag(field_num: int, wire_type: int) -> bytes:
    return encode_varint((field_num << 3) | wire_type)

def encode_varint_field(field_num: int, val: int) -> bytes:
    return encode_tag(field_num, 0) + encode_varint(val)

def encode_float_field(field_num: int, val: float) -> bytes:
    return encode_tag(field_num, 5) + struct.pack('<f', val)

def encode_bytes_field(field_num: int, data: bytes) -> bytes:
    return encode_tag(field_num, 2) + encode_varint(len(data)) + data

def encode_string_field(field_num: int, s: str) -> bytes:
    return encode_bytes_field(field_num, s.encode('utf-8'))

def pw_vec2(x: float, y: float) -> bytes:
    return encode_float_field(1, x) + encode_float_field(2, y)

def pw_rect(x: float, y: float, w: float, h: float) -> bytes:
    return (encode_float_field(1, x) + encode_float_field(2, y) +
            encode_float_field(3, w) + encode_float_field(4, h))

def pw_box(x: float, y: float, z: float, w: float, h: float, d: float) -> bytes:
    return (encode_float_field(1, x) + encode_float_field(2, y) + encode_float_field(3, z) +
            encode_float_field(4, w) + encode_float_field(5, h) + encode_float_field(6, d))

def pw_color(r: float, g: float, b: float, a: float = 1.0) -> bytes:
    return (encode_float_field(1, r) + encode_float_field(2, g) +
            encode_float_field(3, b) + encode_float_field(4, a))

def pw_mesh_attr(vtype: int, vper: int, stride: int, offset: int) -> bytes:
    return (encode_varint_field(1, vtype) + encode_varint_field(2, vper) +
            encode_varint_field(3, stride) + encode_varint_field(4, offset))

def pw_material(tex: str) -> bytes:
    t = encode_string_field(1, tex) + encode_varint_field(2, 1) + encode_varint_field(4, 1)
    m = (encode_bytes_field(1, pw_color(1, 1, 1, 1)) +
         encode_bytes_field(2, pw_color(1, 1, 1, 1)) +
         encode_bytes_field(3, pw_color(1, 1, 1, 1)) +
         encode_float_field(4, 0.0) +
         encode_bytes_field(5, t))
    return m

def encode_mesh(verts: list, indices: list, tex_name: str, two_sided: bool = True) -> bytes:
    vbuf = bytearray()
    mn = [1e9, 1e9, 1e9]
    mx = [-1e9, -1e9, -1e9]

    for v in verts:
        for f in v:
            vbuf += struct.pack('<f', f)
        for c in range(3):
            if v[c] < mn[c]: mn[c] = v[c]
            if v[c] > mx[c]: mx[c] = v[c]

    if not verts:
        mn = [0.0, 0.0, 0.0]
        mx = [0.0, 0.0, 0.0]

    all_indices = list(indices)
    if two_sided:
        # Append reverse-wound triangles for 100% solid visibility
        for i in range(0, len(indices), 3):
            if i + 2 < len(indices):
                all_indices.extend([indices[i], indices[i+2], indices[i+1]])

    ibuf = bytearray()
    for idx in all_indices:
        ibuf += struct.pack('<H', idx)

    nv = len(verts)
    nf = len(all_indices) // 3

    m = bytearray()
    m += encode_varint_field(1, nv)
    m += encode_varint_field(2, nf)
    if all_indices:
        m += encode_bytes_field(3, pw_mesh_attr(4, 1, 2, 0))
    m += encode_bytes_field(4, pw_mesh_attr(7, 3, 32, 0))   # Position
    m += encode_bytes_field(5, pw_mesh_attr(7, 3, 32, 12))  # Normal
    m += encode_bytes_field(6, pw_mesh_attr(7, 2, 32, 24))  # TexCoord0
    m += encode_string_field(7, tex_name)                   # Direct Texture string
    m += encode_bytes_field(10, pw_material(tex_name))      # Material
    m += encode_bytes_field(11, pw_box(mn[0], mn[1], mn[2],
                                       mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]))
    m += encode_bytes_field(50, bytes(vbuf))
    if all_indices:
        m += encode_bytes_field(51, bytes(ibuf))
    return bytes(m)


# ── 2D Convex Hull (Monotone Chain Algorithm) ────────────────────────────────

def compute_ccw_convex_hull(points: list) -> list:
    pts = sorted(set(points))
    if len(pts) <= 2:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)

    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)

    hull = lower[:-1] + upper[:-1]

    # Verify and enforce CCW signed area
    signed_area = 0.5 * sum(hull[i][0] * hull[(i+1)%len(hull)][1] -
                            hull[(i+1)%len(hull)][0] * hull[i][1] for i in range(len(hull)))
    if signed_area < 0:
        hull.reverse()

    return hull


# ── Model to SceneObject Converter ───────────────────────────────────────────

def convert_pod_to_scene_object(pod_path: str,
                                identifier: str,
                                scale: float = 1.0,
                                pos: tuple = (0.0, 0.0),
                                depth: float = 0.0,
                                base_cid: int = 9000,
                                two_sided: bool = True) -> bytes:
    model = pod_load(pod_path)
    if not model.meshes:
        raise ValueError(f"No meshes found in POD model: {pod_path}")

    submeshes = []
    all_xy = []
    all_z = []
    unique_textures = []

    for node in model.nodes[:model.num_mesh_nodes]:
        if node.object_index < 0 or node.object_index >= len(model.meshes):
            continue
        mesh = model.meshes[node.object_index]
        tex_base = "graveyard_ground"
        if 0 <= node.material_index < len(model.materials):
            mat = model.materials[node.material_index]
            if 0 <= mat.diffuse_texture_index < len(model.texture_filenames):
                tex_file = model.texture_filenames[mat.diffuse_texture_index]
                tex_base = os.path.splitext(os.path.basename(tex_file))[0]

        if tex_base not in unique_textures:
            unique_textures.append(tex_base)

        verts = []
        for i in range(mesh.num_vertices):
            x = mesh.positions[i*3+0] * scale
            y = mesh.positions[i*3+1] * scale
            z = mesh.positions[i*3+2] * scale
            nx = mesh.normals[i*3+0] if len(mesh.normals) > i*3+2 else 0.0
            ny = mesh.normals[i*3+1] if len(mesh.normals) > i*3+2 else 1.0
            nz = mesh.normals[i*3+2] if len(mesh.normals) > i*3+2 else 0.0
            u = mesh.uvs[i*2+0] if len(mesh.uvs) > i*2+1 else 0.0
            v = mesh.uvs[i*2+1] if len(mesh.uvs) > i*2+1 else 0.0

            verts.append((x, y, z, nx, ny, nz, u, v))
            all_xy.append((x, y))
            all_z.append(z)

        submeshes.append((verts, mesh.indices, tex_base))

    min_x = min(p[0] for p in all_xy)
    max_x = max(p[0] for p in all_xy)
    min_y = min(p[1] for p in all_xy)
    max_y = max(p[1] for p in all_xy)
    min_z = min(all_z)
    max_z = max(all_z)

    hull = compute_ccw_convex_hull(all_xy)
    area = 0.5 * sum(hull[i][0] * hull[(i+1)%len(hull)][1] -
                     hull[(i+1)%len(hull)][0] * hull[i][1] for i in range(len(hull)))
    print(f"[ModelToMesh] Generated {len(hull)}-point CCW collision hull (signed area: {area:.1f})")

    depth_extent = max(abs(min_z), abs(max_z), 30.0) + 15.0

    # 1. GroundPolygon
    poly = bytearray()
    for pt in hull:
        poly += encode_bytes_field(1, pw_vec2(pt[0], pt[1]))
    poly += encode_varint_field(2, 0) # Convex: 0
    poly += encode_varint_field(3, 1) # Closed: 1

    gpc = bytearray()
    gpc += encode_bytes_field(2, bytes(poly))
    gpc += encode_varint_field(3, 1) # Collides: 1
    gpc += encode_float_field(4, -depth_extent)
    gpc += encode_float_field(5,  depth_extent)

    comp_poly = bytearray()
    comp_poly += encode_string_field(1, "GroundPolygon")
    comp_poly += encode_varint_field(2, base_cid)
    comp_poly += encode_bytes_field(110, bytes(gpc))

    # 2. GroundMeshComponent
    bnd_w = (max_x - min_x) + 20.0
    bnd_h = (max_y - min_y) + 20.0
    center_x = (min_x + max_x) * 0.5
    center_y = (min_y + max_y) * 0.5

    gmc = bytearray()
    gmc += encode_bytes_field(7, pw_rect(center_x - bnd_w * 0.5, center_y - bnd_h * 0.5, bnd_w, bnd_h))
    for verts, indices, tex in submeshes:
        gmc += encode_bytes_field(8, encode_mesh(verts, indices, tex, two_sided=two_sided))
    gmc += encode_bytes_field(10, pw_color(1, 1, 1, 1))

    comp_mesh = bytearray()
    comp_mesh += encode_string_field(1, "GroundMesh")
    comp_mesh += encode_varint_field(2, base_cid + 1)
    comp_mesh += encode_bytes_field(111, bytes(gmc))

    # 3. GroundMeshGenerator
    ggc = bytearray()
    ggc += encode_varint_field(1, base_cid)
    ggc += encode_varint_field(2, base_cid + 1)
    ggc += encode_varint_field(3, base_cid + 2)
    ggc += encode_varint_field(4, base_cid + 3)
    ggc += encode_varint_field(5, 1291618994)
    ggc += encode_float_field(6,  0.0)
    ggc += encode_varint_field(7,  1)
    ggc += encode_float_field(8,  bnd_w)
    ggc += encode_float_field(9,  bnd_h)
    ggc += encode_float_field(10, bnd_w * 0.5)
    ggc += encode_float_field(11, bnd_h * 0.5)

    comp_gen = bytearray()
    comp_gen += encode_string_field(1, "GroundMeshGenerator")
    comp_gen += encode_varint_field(2, base_cid + 4)
    comp_gen += encode_bytes_field(112, bytes(ggc))

    # 4. CollisionShape (Chipmunk 2D)
    shape = bytearray()
    shape += encode_bytes_field(3, bytes(poly))

    csc = bytearray()
    csc += encode_varint_field(2, 1) # IsGround: 1
    csc += encode_float_field(6, -depth_extent)
    csc += encode_float_field(7,  depth_extent)
    csc += encode_varint_field(11, 1)

    comp_col = bytearray()
    comp_col += encode_string_field(1, "CollisionShape")
    comp_col += encode_varint_field(2, base_cid + 5)
    comp_col += encode_varint_field(4, base_cid)
    comp_col += encode_bytes_field(120, bytes(shape))
    comp_col += encode_bytes_field(121, bytes(csc))

    # 5. TextureMapping
    def make_tm(cid: int, tex: str) -> bytes:
        tm = bytearray()
        tm += encode_string_field(1, tex)
        tm += encode_float_field(2, 200.0)
        tm += encode_bytes_field(3, pw_vec2(0.0, 0.0))
        c = bytearray()
        c += encode_string_field(1, "TextureMapping")
        c += encode_varint_field(2, cid)
        c += encode_bytes_field(113, bytes(tm))
        return bytes(c)

    # 6. SceneObject
    obj = bytearray()
    obj += encode_string_field(1, "SceneObject")
    obj += encode_string_field(2, identifier)
    obj += encode_bytes_field(3, bytes(comp_poly))
    obj += encode_bytes_field(3, bytes(comp_mesh))
    obj += encode_bytes_field(3, bytes(comp_gen))
    obj += encode_bytes_field(3, bytes(comp_col))
    for idx, tex in enumerate(unique_textures):
        obj += encode_bytes_field(3, make_tm(base_cid + 2 + idx, tex))

    obj += encode_bytes_field(4, pw_vec2(pos[0], pos[1]))
    obj += encode_float_field(5, depth)
    obj += encode_float_field(6, 0.0) # Rotation
    obj += encode_float_field(7, 1.0) # Scaling
    obj += encode_bytes_field(8, pw_rect(center_x - bnd_w * 0.5, center_y - bnd_h * 0.5, bnd_w, bnd_h))
    obj += encode_varint_field(9, 0)  # Active: 0

    return bytes(obj)


def inject_into_scene(scene_path: str, obj_bytes: bytes) -> None:
    with open(scene_path, 'rb') as f:
        data = f.read()

    pos = 0
    fields = []
    while pos < len(data):
        val = 0
        shift = 0
        while True:
            b = data[pos]
            pos += 1
            val |= (b & 0x7f) << shift
            if not (b & 0x80):
                break
            shift += 7
        fnum = val >> 3
        wtype = val & 7
        if wtype != 2:
            raise ValueError(f"Unexpected wire type {wtype} at {pos}")

        length = 0
        shift = 0
        while True:
            b = data[pos]
            pos += 1
            length |= (b & 0x7f) << shift
            if not (b & 0x80):
                break
            shift += 7

        chunk = data[pos:pos+length]
        pos += length
        fields.append((fnum, chunk))

    # Insert object right after existing SceneObjects (which are all field 1)
    insert_idx = 0
    while insert_idx < len(fields) and fields[insert_idx][0] == 1:
        insert_idx += 1

    fields.insert(insert_idx, (1, obj_bytes))

    out_data = bytearray()
    for fn, chunk in fields:
        out_data += encode_bytes_field(fn, chunk)

    with open(scene_path, 'wb') as f:
        f.write(out_data)

    print(f"[ModelToMesh] Successfully injected object at index {insert_idx} in {scene_path} (new size: {len(out_data)} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Convert 3D model (POD) to Swordigo GroundMesh SceneObject")
    parser.add_argument("model", help="Path to input .POD model")
    parser.add_argument("--output", "-o", help="Save standalone .scene file")
    parser.add_argument("--inject", help="Inject into target .scene file")
    parser.add_argument("--name", help="SceneObject identifier (default: model base name)")
    parser.add_argument("--pos", nargs=2, type=float, default=[0.0, 0.0], help="World position X Y")
    parser.add_argument("--depth", type=float, default=0.0, help="World depth Z")
    parser.add_argument("--scale", type=float, default=1.0, help="Scale factor (default: 1.0)")
    parser.add_argument("--base-cid", type=int, default=9000, help="Base component ID (default: 9000)")
    parser.add_argument("--two-sided", action="store_true", default=True, help="Generate two-sided faces")
    parser.add_argument("--one-sided", action="store_false", dest="two_sided", help="Single-sided faces only")

    args = parser.parse_args()

    name = args.name or os.path.splitext(os.path.basename(args.model))[0]

    obj_bytes = convert_pod_to_scene_object(
        args.model,
        identifier=name,
        scale=args.scale,
        pos=tuple(args.pos),
        depth=args.depth,
        base_cid=args.base_cid,
        two_sided=args.two_sided
    )

    if args.output:
        scene_blob = encode_bytes_field(1, obj_bytes)
        with open(args.output, 'wb') as f:
            f.write(scene_blob)
        print(f"[ModelToMesh] Saved standalone scene to {args.output} ({len(scene_blob)} bytes)")

    if args.inject:
        inject_into_scene(args.inject, obj_bytes)

    if not args.output and not args.inject:
        print("[ModelToMesh] Specify --output or --inject. Nothing written.")

if __name__ == "__main__":
    main()
