"""Verify Dwarf jewelry against original M2/TXID and BLP data (Pillow required).

Usage: python verify-package.py PACKAGE RAW_FOLDER
RAW_FOLDER contains <FileDataID>.bin, extracted separately; never commit game data.
Run the independent FBX reimport checker as well to check the FBX/material links.
"""
import json
from pathlib import Path
import struct
import sys

from PIL import Image, ImageChops

package, raw = map(Path, sys.argv[1:])
catalog = json.loads((package / "catalog.json").read_text())
data = (raw / f"{catalog['modelFileId']}.bin").read_bytes()
chunks = {}
offset = 0
while offset < len(data):
    name, size = struct.unpack_from("<4sI", data, offset)
    assert offset + 8 + size <= len(data)
    chunks[name] = data[offset + 8:offset + 8 + size]
    offset += 8 + size
model = chunks[b"MD21"]
count, texture_offset = struct.unpack_from("<II", model, 80)
file_ids = [x[0] for x in struct.iter_unpack("<I", chunks[b"TXID"])]
assert len(file_ids) == count
materials = json.loads((package / catalog["materialCatalog"]).read_text())
materials = {m["name"]: m for m in materials["materials"]}
checked = 0
for option in catalog["options"]:
    for choice in option["choices"]:
        for effect in choice["effects"]:
            if "intrinsicTexture" not in effect:
                continue
            assert "texture" not in effect  # Never synthesize a body-skin layer.
            binding = effect["intrinsicTexture"]
            slot, file_id = binding["slot"], binding["fileId"]
            assert 0 <= slot < count and file_ids[slot] == file_id
            assert struct.unpack_from("<I", model, texture_offset + 16 * slot)[0] == 0
            source = Image.open(raw / f"{file_id}.bin").convert("RGBA")
            geosets = [g for e in choice["effects"] for g in e.get("geosets", [])]
            assert geosets
            for geoset in geosets:
                surfaces = [g for g in catalog["geometry"]
                            if g["owner"] == "base" and g["geoset"] == geoset]
                assert surfaces
                for surface in surfaces:
                    assert surface["textureUnits"][0]["textureType"] == 0
                    path = materials[surface["material"]]["units"][0]["file"]
                    exported = Image.open(package / path).convert("RGBA")
                    assert exported.size == source.size
                    extrema = ImageChops.difference(source, exported).getextrema()
                    # WMV/Pillow expand RGB565 endpoints differently. Bound that
                    # quantization difference, while requiring identical alpha.
                    assert all(high <= limit for (_, high), limit in
                               zip(extrema, (7, 4, 7, 0))), (choice["id"], extrema)
                    checked += 1
assert checked == 12, checked  # Seven earrings and five nose/piercing alternatives.
assert len(catalog["animationTakes"]) > 0
# Native-event export is an independent contribution, not required by this fix.
if "animationEvents" in catalog:
    events = json.loads((package / catalog["animationEvents"]["file"]).read_text())
    assert events["status"] == "complete"
    assert len(events["clips"]) == len(catalog["animationTakes"])
print(f"PASS: {checked} jewelry alternatives retain their native texture; animated package")
