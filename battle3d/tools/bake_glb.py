#!/usr/bin/env python3
"""Bake Draco+WebP glTF Pokémon models into the compact PK3M runtime format."""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from io import BytesIO
from typing import Any

import DracoPy
import numpy as np
from PIL import Image

PK3M_MAGIC = b"PK3M"
PK3M_VERSION = 1
PATH_TRANSLATION = 0
PATH_ROTATION = 1
PATH_SCALE = 2

COMPONENT_NP = {
    5120: np.int8,
    5121: np.uint8,
    5122: np.int16,
    5123: np.uint16,
    5125: np.uint32,
    5126: np.float32,
}
NUM_COMPONENTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT4": 16,
}


def load_glb(path: str) -> tuple[dict[str, Any], bytes]:
    with open(path, "rb") as f:
        magic, version, length = struct.unpack("<4sII", f.read(12))
        if magic != b"glTF":
            raise ValueError(f"not a GLB: {path}")
        json_blob = None
        bin_blob = b""
        while f.tell() < length:
            chunk_len, chunk_type = struct.unpack("<I4s", f.read(8))
            data = f.read(chunk_len)
            if chunk_type.startswith(b"JSON"):
                json_blob = json.loads(data)
            elif chunk_type.startswith(b"BIN"):
                bin_blob = data
    if json_blob is None:
        raise ValueError("GLB missing JSON chunk")
    return json_blob, bin_blob


def accessor_numpy(gltf: dict[str, Any], blob: bytes, accessor_index: int) -> np.ndarray:
    acc = gltf["accessors"][accessor_index]
    count = acc["count"]
    ncomp = NUM_COMPONENTS[acc["type"]]
    dtype = COMPONENT_NP[acc["componentType"]]
    if "bufferView" not in acc:
        return np.zeros((count, ncomp), dtype=np.float32)
    view = gltf["bufferViews"][acc["bufferView"]]
    offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or (np.dtype(dtype).itemsize * ncomp)
    raw = blob[offset : offset + count * stride]
    if stride == np.dtype(dtype).itemsize * ncomp:
        arr = np.frombuffer(raw, dtype=dtype, count=count * ncomp).reshape(count, ncomp)
    else:
        item = np.dtype(dtype).itemsize * ncomp
        rows = []
        for i in range(count):
            sl = raw[i * stride : i * stride + item]
            rows.append(np.frombuffer(sl, dtype=dtype, count=ncomp))
        arr = np.stack(rows, axis=0)
    out = arr.astype(np.float32, copy=False)
    if acc.get("normalized"):
        if dtype == np.uint8:
            out = arr.astype(np.float32) / 255.0
        elif dtype == np.uint16:
            out = arr.astype(np.float32) / 65535.0
        elif dtype == np.int8:
            out = np.maximum(arr.astype(np.float32) / 127.0, -1.0)
        elif dtype == np.int16:
            out = np.maximum(arr.astype(np.float32) / 32767.0, -1.0)
    if ncomp == 1:
        return out.reshape(count)
    return out


def decode_image(gltf: dict[str, Any], blob: bytes, image_index: int) -> Image.Image:
    image = gltf["images"][image_index]
    if "bufferView" in image:
        view = gltf["bufferViews"][image["bufferView"]]
        offset = view.get("byteOffset", 0)
        data = blob[offset : offset + view["byteLength"]]
        return Image.open(BytesIO(data)).convert("RGBA")
    raise ValueError("image without bufferView")


def decode_draco_primitive(gltf: dict[str, Any], blob: bytes, prim: dict[str, Any]):
    ext = prim["extensions"]["KHR_draco_mesh_compression"]
    view = gltf["bufferViews"][ext["bufferView"]]
    offset = view.get("byteOffset", 0)
    payload = blob[offset : offset + view["byteLength"]]
    mesh = DracoPy.decode(payload)
    by_id = {attr["unique_id"]: attr for attr in mesh.attributes}
    mapping = ext.get("attributes", {})

    def attr_data(semantic: str, fallback_id: int | None = None):
        if semantic in mapping:
            uid = mapping[semantic]
            if uid in by_id:
                return np.asarray(by_id[uid]["data"])
        if fallback_id is not None and fallback_id in by_id:
            return np.asarray(by_id[fallback_id]["data"])
        return None

    positions = np.asarray(mesh.points, dtype=np.float32)
    normals = attr_data("NORMAL")
    if normals is None:
        normals = np.zeros_like(positions)
        faces = np.asarray(mesh.faces, dtype=np.int64)
        v0 = positions[faces[:, 0]]
        v1 = positions[faces[:, 1]]
        v2 = positions[faces[:, 2]]
        fn = np.cross(v1 - v0, v2 - v0)
        for i in range(3):
            np.add.at(normals, faces[:, i], fn)
        lens = np.linalg.norm(normals, axis=1, keepdims=True)
        lens[lens < 1e-8] = 1.0
        normals = (normals / lens).astype(np.float32)
    else:
        normals = normals.astype(np.float32)
        lens = np.linalg.norm(normals, axis=1, keepdims=True)
        lens[lens < 1e-8] = 1.0
        normals = (normals / lens).astype(np.float32)

    uvs = attr_data("TEXCOORD_0")
    if uvs is None:
        uvs = np.zeros((len(positions), 2), dtype=np.float32)
    else:
        uvs = uvs.astype(np.float32)

    joints = attr_data("JOINTS_0")
    if joints is None:
        joints = np.zeros((len(positions), 4), dtype=np.uint16)
    else:
        joints = joints.astype(np.uint16)

    weights = attr_data("WEIGHTS_0")
    if weights is None:
        weights = np.zeros((len(positions), 4), dtype=np.float32)
        weights[:, 0] = 1.0
    else:
        weights = weights.astype(np.float32)
        sums = weights.sum(axis=1, keepdims=True)
        sums[sums < 1e-8] = 1.0
        weights = weights / sums

    indices = np.asarray(mesh.faces, dtype=np.uint32).reshape(-1)
    return positions, normals, uvs, joints, weights, indices


def node_trs(node: dict[str, Any]):
    t = np.array(node.get("translation", [0, 0, 0]), dtype=np.float32)
    r = np.array(node.get("rotation", [0, 0, 0, 1]), dtype=np.float32)
    s = np.array(node.get("scale", [1, 1, 1]), dtype=np.float32)
    if "matrix" in node:
        m = np.array(node["matrix"], dtype=np.float32).reshape(4, 4)
        t = m[3, :3].copy() if m.shape == (4, 4) else t
        # column-major glTF matrix stored as 16 floats; reshape(4,4) is C-order rows
        m = np.array(node["matrix"], dtype=np.float32).reshape(4, 4, order="F")
        t = m[:3, 3].copy()
        s = np.array(
            [np.linalg.norm(m[:3, 0]), np.linalg.norm(m[:3, 1]), np.linalg.norm(m[:3, 2])],
            dtype=np.float32,
        )
        rotm = np.column_stack([m[:3, i] / max(s[i], 1e-8) for i in range(3)])
        r = mat3_to_quat(rotm)
    return t, r, s


def mat3_to_quat(m: np.ndarray) -> np.ndarray:
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = np.sqrt(tr + 1.0) * 2
        return np.array(
            [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s],
            dtype=np.float32,
        )
    if m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        return np.array(
            [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s],
            dtype=np.float32,
        )
    if m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        return np.array(
            [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s],
            dtype=np.float32,
        )
    s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
    return np.array(
        [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s],
        dtype=np.float32,
    )


def bake(path: str, out_path: str) -> None:
    gltf, blob = load_glb(path)
    nodes = gltf.get("nodes", [])
    skins = gltf.get("skins", [])
    meshes = gltf.get("meshes", [])
    if not meshes:
        raise ValueError("no meshes")

    positions_all = []
    normals_all = []
    uvs_all = []
    joints_all = []
    weights_all = []
    indices_all = []
    base = 0
    texture = Image.new("RGBA", (8, 8), (180, 180, 180, 255))

    for mesh in meshes:
        for prim in mesh.get("primitives", []):
            if "extensions" in prim and "KHR_draco_mesh_compression" in prim["extensions"]:
                p, n, u, j, w, idx = decode_draco_primitive(gltf, blob, prim)
            else:
                raise ValueError("primitive is not Draco-compressed; unsupported in this baker")
            mat_index = prim.get("material")
            if mat_index is not None:
                mat = gltf["materials"][mat_index]
                pbr = mat.get("pbrMetallicRoughness", {})
                texinfo = pbr.get("baseColorTexture") or mat.get("emissiveTexture")
                if texinfo is not None:
                    tex = gltf["textures"][texinfo["index"]]
                    src = tex.get("source")
                    if src is not None:
                        texture = decode_image(gltf, blob, src)
            positions_all.append(p)
            normals_all.append(n)
            uvs_all.append(u)
            joints_all.append(j)
            weights_all.append(w)
            indices_all.append(idx.astype(np.uint32) + base)
            base += len(p)

    positions = np.concatenate(positions_all, axis=0)
    normals = np.concatenate(normals_all, axis=0)
    uvs = np.concatenate(uvs_all, axis=0)
    joints = np.concatenate(joints_all, axis=0)
    weights = np.concatenate(weights_all, axis=0)
    indices = np.concatenate(indices_all, axis=0)

    # Normalize so the model stands on y=0 with a battle-friendly height.
    mn = positions.min(axis=0)
    mx = positions.max(axis=0)
    size = mx - mn
    height = max(float(size[1]), 1e-4)
    scale = 1.35 / height
    positions = (positions - np.array([0.5 * (mn[0] + mx[0]), mn[1], 0.5 * (mn[2] + mx[2])], dtype=np.float32)) * scale

    texture = texture.resize((512, 512), Image.Resampling.BILINEAR)
    tex_rgba = np.array(texture, dtype=np.uint8)

    parent = np.full((len(nodes),), -1, dtype=np.int32)
    translations = np.zeros((len(nodes), 3), dtype=np.float32)
    rotations = np.zeros((len(nodes), 4), dtype=np.float32)
    rotations[:, 3] = 1.0
    scales = np.ones((len(nodes), 3), dtype=np.float32)
    for i, node in enumerate(nodes):
        translations[i], rotations[i], scales[i] = node_trs(node)
        for child in node.get("children", []):
            parent[child] = i

    joint_nodes = []
    ibms = np.resize(np.eye(4, dtype=np.float32), (0, 16))
    if skins:
        skin = skins[0]
        joint_nodes = list(skin.get("joints", []))
        if "inverseBindMatrices" in skin:
            ibm = accessor_numpy(gltf, blob, skin["inverseBindMatrices"])
            ibms = ibm.reshape(-1, 16).astype(np.float32)
        else:
            ibms = np.tile(np.eye(4, dtype=np.float32).reshape(16), (len(joint_nodes), 1))
    joint_map = {node_index: i for i, node_index in enumerate(joint_nodes)}
    if joint_nodes:
        mapped = np.zeros_like(joints)
        for c in range(4):
            mapped[:, c] = np.vectorize(lambda x: joint_map.get(int(x), 0))(joints[:, c])
        # joints in glTF already index into skin.joints, not node indices
        joints = joints.astype(np.uint16)
        njoints = len(joint_nodes)
        joints = np.clip(joints, 0, max(njoints - 1, 0)).astype(np.uint16)

    animations_out = []
    for anim in gltf.get("animations", []):
        name = (anim.get("name") or "anim")[-63:]
        channels_out = []
        duration = 0.0
        samplers = anim.get("samplers", [])
        for ch in anim.get("channels", []):
            sampler = samplers[ch["sampler"]]
            target = ch.get("target", {})
            node_index = target.get("node")
            path = target.get("path")
            if node_index is None or path not in ("translation", "rotation", "scale"):
                continue
            times = accessor_numpy(gltf, blob, sampler["input"]).reshape(-1).astype(np.float32)
            values = accessor_numpy(gltf, blob, sampler["output"]).astype(np.float32)
            duration = max(duration, float(times[-1]) if len(times) else 0.0)
            path_id = {"translation": PATH_TRANSLATION, "rotation": PATH_ROTATION, "scale": PATH_SCALE}[path]
            channels_out.append((node_index, path_id, times, values))
        animations_out.append((name, duration, channels_out))

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(PK3M_MAGIC)
        f.write(struct.pack("<I", PK3M_VERSION))
        f.write(struct.pack("<IIIIII", len(positions), len(indices), len(joint_nodes), len(nodes), len(animations_out), 0))
        f.write(struct.pack("<II", tex_rgba.shape[1], tex_rgba.shape[0]))
        for i in range(len(positions)):
            f.write(struct.pack("<fff", *positions[i]))
            f.write(struct.pack("<fff", *normals[i]))
            f.write(struct.pack("<ff", *uvs[i]))
            f.write(struct.pack("<HHHH", *joints[i]))
            f.write(struct.pack("<ffff", *weights[i]))
        f.write(indices.astype(np.uint32).tobytes())
        if len(joint_nodes):
            f.write(np.array(joint_nodes, dtype=np.int32).tobytes())
            f.write(ibms.astype(np.float32).reshape(-1).tobytes())
        f.write(parent.tobytes())
        f.write(translations.tobytes())
        f.write(rotations.tobytes())
        f.write(scales.tobytes())
        for name, duration, channels in animations_out:
            raw_name = name.encode("utf-8")[:63]
            f.write(raw_name + b"\0" * (64 - len(raw_name)))
            f.write(struct.pack("<fI", duration, len(channels)))
            for node_index, path_id, times, values in channels:
                f.write(struct.pack("<iii", node_index, path_id, len(times)))
                f.write(np.asarray(times, dtype=np.float32).tobytes())
                f.write(np.asarray(values, dtype=np.float32).ravel().tobytes())
        f.write(tex_rgba.tobytes())
    print(f"baked {path} -> {out_path} ({len(positions)} verts, {len(animations_out)} clips, {len(joint_nodes)} joints)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("-o", "--outdir", required=True)
    args = parser.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    failed = 0
    for src in args.inputs:
        species = os.path.splitext(os.path.basename(src))[0]
        dest = os.path.join(args.outdir, f"{species}.pk3m")
        try:
            bake(src, dest)
        except Exception as exc:
            failed += 1
            print(f"FAIL {src}: {exc}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
