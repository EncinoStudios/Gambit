#!/usr/bin/env python3
"""Generates the glTF 2.0 test models of GambitTests (T08): small files that exercise the glTF importer.

    python3 tools/gen_test_gltf.py [--output GambitTests/Assets/Models]

Everything is computed here (geometry, textures), so the files are reproducible and carry no third-party content
(D-080). The output is deterministic: rerunning the script on any machine rewrites identical bytes. Files:

- TexturedCube.gltf + TexturedCube.bin + Checker.png + Bumps.png: a .gltf with external buffer and images. A node
  hierarchy (Root with Cube, Lamp and Camera), a cube without tangents (MikkTSpace generates them), a material that
  uses one image as sRGB base color and as linear occlusion, a nearest/clamp sampler, KHR_texture_transform,
  KHR_lights_punctual and a perspective camera.
- Shapes.glb: a binary glTF with an embedded PNG. A triangle strip, a triangle fan with byte indices and skinning
  attributes, a mesh without normals, a quantized mesh (KHR_mesh_quantization, required), a mesh with two primitives
  and two materials (alpha mask, emissive with KHR_materials_emissive_strength and alpha blend), a points-only mesh,
  node matrices, an unnamed node, spot and directional lights and an orthographic camera.
- Embedded.gltf: buffers and image as base64 data URIs, two meshes with the same name.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import struct
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# glTF enums.
BYTE, UNSIGNED_BYTE, SHORT, UNSIGNED_SHORT, UNSIGNED_INT, FLOAT = 5120, 5121, 5122, 5123, 5125, 5126
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
POINTS, TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN = 0, 4, 5, 6
NEAREST, LINEAR, LINEAR_MIPMAP_LINEAR = 9728, 9729, 9987
REPEAT, CLAMP_TO_EDGE = 10497, 33071

COMPONENT_FORMATS = {BYTE: "b", UNSIGNED_BYTE: "B", SHORT: "h", UNSIGNED_SHORT: "H", UNSIGNED_INT: "I", FLOAT: "f"}
TYPE_SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def encode_png(width: int, height: int, rows: list[bytes], channels: int) -> bytes:
    """A PNG of 8-bit RGB (channels 3) or RGBA (channels 4) rows, zlib level 9."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    color_type = 2 if channels == 3 else 6
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    raw = b"".join(b"\x00" + row for row in rows)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def checker_png() -> bytes:
    """8x8 RGB: orange and blue squares of 2x2 texels."""
    rows = []
    for y in range(8):
        row = bytearray()
        for x in range(8):
            row += bytes((230, 120, 30)) if ((x // 2) + (y // 2)) % 2 == 0 else bytes((40, 80, 200))
        rows.append(bytes(row))
    return encode_png(8, 8, rows, 3)


def bumps_png() -> bytes:
    """16x16 RGB tangent-space normal map: a ring of bumps (encoded as n * 0.5 + 0.5)."""
    rows = []
    for y in range(16):
        row = bytearray()
        for x in range(16):
            u = (x + 0.5) / 16.0 * 2.0 * math.pi
            v = (y + 0.5) / 16.0 * 2.0 * math.pi
            nx, ny = 0.4 * math.sin(u), 0.4 * math.sin(v)
            nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))
            row += bytes(round((c * 0.5 + 0.5) * 255.0) for c in (nx, ny, nz))
        rows.append(bytes(row))
    return encode_png(16, 16, rows, 3)


def leaf_png() -> bytes:
    """16x16 RGBA: a green diamond, opaque inside and fully transparent outside (alpha mask tests)."""
    rows = []
    for y in range(16):
        row = bytearray()
        for x in range(16):
            inside = abs(x - 7.5) + abs(y - 7.5) <= 7.0
            row += bytes((60, 170, 60, 255 if inside else 0))
        rows.append(bytes(row))
    return encode_png(16, 16, rows, 4)


class Document:
    """A glTF document under construction with one binary buffer."""

    def __init__(self) -> None:
        self.json: dict = {"asset": {"version": "2.0", "generator": "Gambit tools/gen_test_gltf.py"}}
        self.binary = bytearray()

    def _list(self, name: str) -> list:
        return self.json.setdefault(name, [])

    def add(self, name: str, item: dict) -> int:
        items = self._list(name)
        items.append(item)
        return len(items) - 1

    def view(self, data: bytes, target: int | None = None, stride: int | None = None) -> int:
        while len(self.binary) % 4 != 0:
            self.binary.append(0)
        view = {"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(data)}
        if stride is not None:
            view["byteStride"] = stride
        if target is not None:
            view["target"] = target
        self.binary += data
        return self.add("bufferViews", view)

    def accessor(self, values: list, component: int, kind: str, normalized: bool = False, bounds: bool = False,
                 target: int | None = ARRAY_BUFFER) -> int:
        """An accessor over tightly packed `values` (a flat list); `bounds` writes min and max (POSITION needs them)."""
        width = TYPE_SIZES[kind]
        count = len(values) // width
        element = struct.calcsize("<" + COMPONENT_FORMATS[component])
        packed = bytearray()
        for index in range(count):
            element_values = values[index * width:(index + 1) * width]
            packed += struct.pack("<" + COMPONENT_FORMATS[component] * width, *element_values)
            # Vertex attributes start on 4-byte boundaries (glTF 3.6.2.4).
            if target == ARRAY_BUFFER:
                while (len(packed) % 4) != 0:
                    packed.append(0)
        stride = None
        if target == ARRAY_BUFFER and (element * width) % 4 != 0:
            stride = len(packed) // count
        accessor = {"bufferView": self.view(bytes(packed), target, stride), "componentType": component,
                    "count": count, "type": kind}
        if normalized:
            accessor["normalized"] = True
        if bounds:
            columns = [values[i::width] for i in range(width)]
            accessor["min"] = [min(column) for column in columns]
            accessor["max"] = [max(column) for column in columns]
        return self.add("accessors", accessor)

    def finish(self, uri: str | None) -> None:
        while len(self.binary) % 4 != 0:
            self.binary.append(0)
        buffer = {"byteLength": len(self.binary)}
        if uri is not None:
            buffer["uri"] = uri
        self.json["buffers"] = [buffer]

    def text(self) -> str:
        return json.dumps(self.json, indent=2) + "\n"


def flatten(items: list) -> list:
    return [value for item in items for value in item]


def cube_geometry() -> tuple[list, list, list, list]:
    """A unit cube: 4 vertices per face, outward normals, glTF UVs (u right, v down as seen from outside)."""
    faces = [((0, 0, 1), (1, 0, 0), (0, -1, 0)), ((0, 0, -1), (-1, 0, 0), (0, -1, 0)),
             ((1, 0, 0), (0, 0, -1), (0, -1, 0)), ((-1, 0, 0), (0, 0, 1), (0, -1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, 1)), ((0, -1, 0), (1, 0, 0), (0, 0, -1))]
    positions, normals, uvs, indices = [], [], [], []
    for normal, right, down in faces:
        base = len(positions)
        for v in (0, 1):
            for u in (0, 1):
                positions.append([0.5 * (normal[i] + right[i] * (2 * u - 1) + down[i] * (2 * v - 1)) for i in range(3)])
                normals.append(list(normal))
                uvs.append([float(u), float(v)])
        # Counter-clockwise seen from outside: (top-left, bottom-left, bottom-right), (top-left, bottom-right,
        # top-right).
        indices += [base, base + 2, base + 3, base, base + 3, base + 1]
    return positions, normals, uvs, indices


def quaternion_y(degrees: float) -> list[float]:
    half = math.radians(degrees) / 2.0
    return [0.0, math.sin(half), 0.0, math.cos(half)]


def textured_cube(output: Path) -> None:
    document = Document()
    gltf = document.json
    gltf["extensionsUsed"] = ["KHR_lights_punctual", "KHR_texture_transform"]

    positions, normals, uvs, indices = cube_geometry()
    attributes = {
        "POSITION": document.accessor(flatten(positions), FLOAT, "VEC3", bounds=True),
        "NORMAL": document.accessor(flatten(normals), FLOAT, "VEC3"),
        "TEXCOORD_0": document.accessor(flatten(uvs), FLOAT, "VEC2"),
    }
    index_accessor = document.accessor(indices, UNSIGNED_SHORT, "SCALAR", target=ELEMENT_ARRAY_BUFFER)
    document.add("meshes", {"name": "Cube", "primitives": [
        {"attributes": attributes, "indices": index_accessor, "material": 0, "mode": TRIANGLES}]})

    document.add("images", {"uri": "Checker.png", "name": "CheckerImage"})
    document.add("images", {"uri": "Bumps.png"})
    document.add("samplers", {"magFilter": LINEAR, "minFilter": LINEAR_MIPMAP_LINEAR, "wrapS": REPEAT, "wrapT": REPEAT})
    document.add("samplers", {"magFilter": NEAREST, "minFilter": NEAREST, "wrapS": CLAMP_TO_EDGE,
                              "wrapT": CLAMP_TO_EDGE})
    document.add("textures", {"name": "Checker", "source": 0, "sampler": 0})
    document.add("textures", {"source": 1, "sampler": 1})  # named after its image file: "Bumps.png"

    transform = {"KHR_texture_transform": {"offset": [0.25, 0.5], "scale": [2.0, 2.0]}}
    document.add("materials", {
        "name": "Painted",
        "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.9, 0.8, 1.0],
                                 "baseColorTexture": {"index": 0, "extensions": transform},
                                 "metallicFactor": 0.0, "roughnessFactor": 0.6},
        "normalTexture": {"index": 1, "scale": 0.8, "extensions": transform},
        "occlusionTexture": {"index": 0, "strength": 0.5, "extensions": transform},
    })

    gltf["extensions"] = {"KHR_lights_punctual": {"lights": [
        {"name": "Bulb", "type": "point", "color": [1.0, 0.9, 0.8], "intensity": 50.0, "range": 10.0}]}}
    document.add("cameras", {"name": "View", "type": "perspective",
                             "perspective": {"yfov": 0.8, "znear": 0.1, "zfar": 100.0, "aspectRatio": 1.5}})
    document.add("nodes", {"name": "Root", "translation": [0.0, 0.5, 0.0], "children": [1, 2, 3]})
    document.add("nodes", {"name": "Cube", "mesh": 0, "rotation": quaternion_y(30.0), "scale": [1.0, 1.0, 1.0]})
    document.add("nodes", {"name": "Lamp", "translation": [1.0, 2.0, 1.0],
                           "extensions": {"KHR_lights_punctual": {"light": 0}}})
    document.add("nodes", {"name": "Camera", "translation": [0.0, 1.0, 4.0], "camera": 0})
    gltf["scenes"] = [{"name": "Scene", "nodes": [0]}]
    gltf["scene"] = 0

    document.finish("TexturedCube.bin")
    (output / "TexturedCube.gltf").write_text(document.text(), newline="\n")
    (output / "TexturedCube.bin").write_bytes(bytes(document.binary))
    (output / "Checker.png").write_bytes(checker_png())
    (output / "Bumps.png").write_bytes(bumps_png())


def quad(z: float, size: float = 1.0) -> tuple[list, list, list]:
    """A quad in XY facing +Z as a strip order: top-left, bottom-left, top-right, bottom-right."""
    h = size / 2.0
    positions = [[-h, h, z], [-h, -h, z], [h, h, z], [h, -h, z]]
    normals = [[0.0, 0.0, 1.0]] * 4
    uvs = [[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]]
    return positions, normals, uvs


def shapes(output: Path) -> None:
    document = Document()
    gltf = document.json
    gltf["extensionsUsed"] = ["KHR_lights_punctual", "KHR_materials_emissive_strength", "KHR_mesh_quantization"]
    gltf["extensionsRequired"] = ["KHR_mesh_quantization"]

    # Strip: a quad as a triangle strip without indices, with vertex colors (RGB).
    positions, normals, uvs = quad(0.0)
    strip = {"POSITION": document.accessor(flatten(positions), FLOAT, "VEC3", bounds=True),
             "NORMAL": document.accessor(flatten(normals), FLOAT, "VEC3"),
             "TEXCOORD_0": document.accessor(flatten(uvs), FLOAT, "VEC2"),
             "COLOR_0": document.accessor([1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0], FLOAT, "VEC3")}
    document.add("meshes", {"name": "Strip", "primitives": [{"attributes": strip, "mode": TRIANGLE_STRIP}]})

    # Fan: a hexagon around its center with byte indices, and skinning attributes (no skin: they are kept as data).
    fan_positions = [[0.0, 0.0, 0.0]] + [[math.cos(math.radians(60 * i)), math.sin(math.radians(60 * i)), 0.0]
                                         for i in range(6)]
    fan = {"POSITION": document.accessor(flatten(fan_positions), FLOAT, "VEC3", bounds=True),
           "NORMAL": document.accessor(flatten([[0.0, 0.0, 1.0]] * 7), FLOAT, "VEC3"),
           "JOINTS_0": document.accessor(flatten([[0, 1, 0, 0]] * 7), UNSIGNED_BYTE, "VEC4"),
           "WEIGHTS_0": document.accessor(flatten([[1.0, 1.0, 0.0, 0.0]] * 7), FLOAT, "VEC4")}
    fan_indices = document.accessor([0, 1, 2, 3, 4, 5, 6, 1], UNSIGNED_BYTE, "SCALAR", target=ELEMENT_ARRAY_BUFFER)
    document.add("meshes", {"name": "Fan", "primitives": [
        {"attributes": fan, "indices": fan_indices, "mode": TRIANGLE_FAN}]})

    # Faceted: a tetrahedron without normals (flat normals are generated).
    tetra = [[0.0, 1.0, 0.0], [-1.0, -0.5, 0.8], [1.0, -0.5, 0.8], [0.0, -0.5, -1.0]]
    faceted = {"POSITION": document.accessor(flatten(tetra), FLOAT, "VEC3", bounds=True)}
    faceted_indices = document.accessor([0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2], UNSIGNED_SHORT, "SCALAR",
                                        target=ELEMENT_ARRAY_BUFFER)
    document.add("meshes", {"name": "Faceted", "primitives": [{"attributes": faceted, "indices": faceted_indices}]})

    # Quantized: a quad with SHORT positions (the node scales them by 0.01), BYTE normals and USHORT UVs.
    quantized = {"POSITION": document.accessor([-100, 100, 0, -100, -100, 0, 100, 100, 0, 100, -100, 0], SHORT, "VEC3",
                                               bounds=True),
                 "NORMAL": document.accessor([0, 0, 127] * 4, BYTE, "VEC3", normalized=True),
                 "TEXCOORD_0": document.accessor([0, 0, 0, 65535, 65535, 0, 65535, 65535], UNSIGNED_SHORT, "VEC2",
                                                 normalized=True)}
    quantized_indices = document.accessor([0, 1, 3, 0, 3, 2], UNSIGNED_SHORT, "SCALAR", target=ELEMENT_ARRAY_BUFFER)
    document.add("meshes", {"name": "Quantized", "primitives": [
        {"attributes": quantized, "indices": quantized_indices}]})

    # TwoMaterials: two quads, one per material.
    primitives = []
    for material, z in ((0, 0.0), (1, 0.5)):
        positions, normals, uvs = quad(z)
        attributes = {"POSITION": document.accessor(flatten(positions), FLOAT, "VEC3", bounds=True),
                      "NORMAL": document.accessor(flatten(normals), FLOAT, "VEC3"),
                      "TEXCOORD_0": document.accessor(flatten(uvs), FLOAT, "VEC2")}
        primitive_indices = document.accessor([0, 1, 3, 0, 3, 2], UNSIGNED_SHORT, "SCALAR",
                                              target=ELEMENT_ARRAY_BUFFER)
        primitives.append({"attributes": attributes, "indices": primitive_indices, "material": material})
    document.add("meshes", {"name": "TwoMaterials", "primitives": primitives})

    # Points: nothing to import (a warning says so).
    points = {"POSITION": document.accessor([0.0, 0.0, 0.0, 1.0, 1.0, 1.0], FLOAT, "VEC3", bounds=True)}
    document.add("meshes", {"name": "Points", "primitives": [{"attributes": points, "mode": POINTS}]})

    document.add("images", {"name": "Leaf", "mimeType": "image/png", "bufferView": document.view(leaf_png())})
    document.add("textures", {"source": 0})
    document.add("materials", {"name": "Leaf", "alphaMode": "MASK", "alphaCutoff": 0.4, "doubleSided": True,
                               "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0}})
    document.add("materials", {"name": "Glow", "alphaMode": "BLEND", "emissiveFactor": [1.0, 0.5, 0.0],
                               "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 4.0}},
                               "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.2, 0.2, 0.5],
                                                        "metallicFactor": 0.25, "roughnessFactor": 0.75}})

    gltf["extensions"] = {"KHR_lights_punctual": {"lights": [
        {"name": "Sun", "type": "directional", "color": [1.0, 1.0, 0.9], "intensity": 1000.0},
        {"name": "Torch", "type": "spot", "intensity": 10.0,
         "spot": {"innerConeAngle": 0.25, "outerConeAngle": 0.5}}]}}
    document.add("cameras", {"type": "orthographic", "orthographic": {"xmag": 3.0, "ymag": 2.0, "znear": 0.5,
                                                                      "zfar": 50.0}})

    # Strip uses a column-major matrix: rotation of 90 degrees about Y, then a translation of (2, 0, 0).
    document.add("nodes", {"name": "Strip", "mesh": 0, "children": [1],
                           "matrix": [0.0, 0.0, -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 1.0]})
    document.add("nodes", {"name": "Fan", "mesh": 1, "translation": [0.0, 1.0, 0.0]})
    document.add("nodes", {"name": "Faceted", "mesh": 2})
    document.add("nodes", {"name": "Quantized", "mesh": 3, "scale": [0.01, 0.01, 0.01]})
    document.add("nodes", {"name": "TwoMaterials", "mesh": 4})
    document.add("nodes", {"name": "Points", "mesh": 5})
    document.add("nodes", {"children": [7, 8, 9]})
    document.add("nodes", {"name": "Sun", "rotation": [-0.7071068, 0.0, 0.0, 0.7071068],
                           "extensions": {"KHR_lights_punctual": {"light": 0}}})
    document.add("nodes", {"name": "Torch", "translation": [0.0, 3.0, 0.0],
                           "extensions": {"KHR_lights_punctual": {"light": 1}}})
    document.add("nodes", {"name": "TopView", "camera": 0, "translation": [0.0, 10.0, 0.0],
                           "rotation": [-0.7071068, 0.0, 0.0, 0.7071068]})
    gltf["scenes"] = [{"nodes": [0, 2, 3, 4, 5, 6]}]
    gltf["scene"] = 0

    document.finish(None)
    content = document.text().encode("utf-8")
    while len(content) % 4 != 0:
        content += b" "
    binary = bytes(document.binary)
    glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(content) + 8 + len(binary))
    glb += struct.pack("<II", len(content), 0x4E4F534A) + content
    glb += struct.pack("<II", len(binary), 0x004E4942) + binary
    (output / "Shapes.glb").write_bytes(glb)


def embedded(output: Path) -> None:
    document = Document()
    gltf = document.json
    for z in (0.0, 1.0):
        positions, normals, uvs = quad(z)
        attributes = {"POSITION": document.accessor(flatten(positions), FLOAT, "VEC3", bounds=True),
                      "NORMAL": document.accessor(flatten(normals), FLOAT, "VEC3"),
                      "TEXCOORD_0": document.accessor(flatten(uvs), FLOAT, "VEC2")}
        indices = document.accessor([0, 1, 3, 0, 3, 2], UNSIGNED_SHORT, "SCALAR", target=ELEMENT_ARRAY_BUFFER)
        document.add("meshes", {"name": "Dup", "primitives": [
            {"attributes": attributes, "indices": indices, "material": 0}]})
    png = base64.b64encode(checker_png()).decode("ascii")
    document.add("images", {"uri": "data:image/png;base64," + png})
    document.add("textures", {"source": 0})
    document.add("materials", {"name": "Sticker", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}})
    document.add("nodes", {"name": "First", "mesh": 0})
    document.add("nodes", {"name": "Second", "mesh": 1})
    gltf["scenes"] = [{"nodes": [0, 1]}]
    document.finish(None)
    gltf["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(
        bytes(document.binary)).decode("ascii")
    (output / "Embedded.gltf").write_text(document.text(), newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=REPO_ROOT / "GambitTests" / "Assets" / "Models",
                        help="directory for the generated files")
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    textured_cube(arguments.output)
    shapes(arguments.output)
    embedded(arguments.output)
    print(f"gen_test_gltf: wrote the test models to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
