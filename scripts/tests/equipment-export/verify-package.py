"""Validate the portable catalogs and independently reimport every equipment FBX."""
import argparse
import json
import os
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("package", type=pathlib.Path)
parser.add_argument("--inspector", required=True, type=pathlib.Path)
args = parser.parse_args()
root = args.package.resolve()

def read(path):
    return json.loads(path.read_text(encoding="utf-8"))

def reference(catalog, value):
    relative = pathlib.Path(value.replace("\\", "/"))
    assert not relative.is_absolute(), value
    path = (catalog.parent / relative).resolve()
    assert path.is_relative_to(root) and path.is_file() and path.stat().st_size > 0, value
    return path

top = read(root / "catalog.json")
assert top["version"] == 1
if top["type"] == "armor-set":
    top = dict(items=top["pieces"], set="catalog.json")
elif top["type"] in ("armor", "weapon"):
    top = dict(items=[dict(slot=top["slot"], catalog="catalog.json")], set="")
else:
    assert top["type"] == "wmv-equipment"
assert top["items"]
variants = 0
geometry = 0
for item in top["items"]:
    index = reference(root / "catalog.json", item["catalog"])
    piece = read(index)
    assert piece["type"] in ("armor", "weapon")
    seen = set()
    for variant in piece["variants"]:
        key = (variant["race"], variant["sex"])
        assert key not in seen
        seen.add(key)
        path = reference(index, variant["catalog"])
        catalog = read(path)
        assert key == (catalog["race"], catalog["sex"])
        assert len(catalog["items"]) == 1 and catalog["items"][0]["slot"] == item["slot"]
        textures = {t["fileId"]: reference(path, t["file"]) for t in catalog["textures"]}
        assert all(t["fileId"] in textures for t in catalog["bodyLayers"])
        fbx = reference(path, catalog["fbx"])
        scene = json.loads(subprocess.check_output([str(args.inspector), str(fbx)], env={**os.environ, "SystemDrive": "C:"}))
        assert scene["clips"] == 0
        nodes = {n["name"]: n for n in scene["nodes"]}
        assert len(nodes) == len(scene["nodes"])
        for node in nodes.values():
            assert node["unusedVertices"] == node["invalidIndices"] == node["invalidSkinIndices"] == node["invalidWeights"] == 0, node["name"]
            assert all(nodes[b]["skeleton"] for b in node["clusters"])
        for g in catalog["geometry"]:
            assert not g["owner"].startswith("customization:")
            if g["owner"] == "base":
                assert g["geoset"] >= 400 and (g["geoset"] % 100 > 1 or g["geoset"] in (1201, 1501))
            else:
                assert g["owner"] == "equipment:" + str(item["slot"])
            node = nodes[g["node"]]
            assert node["polygons"] > 0 and node["materials"][g["materialIndex"]] == g["material"]
            if g["node"].startswith("WMVEquipment_"):
                assert nodes[node["parent"]]["skeleton"]
        for texture in scene["textures"]:
            reference(path, texture["relative"])
        materials = read(reference(path, catalog["materialCatalog"]))
        for material in materials["materials"]:
            for unit in material["units"]:
                if unit["file"]:
                    reference(path, unit["file"])
                else:
                    assert unit["role"] == "env"
        variants += 1
        geometry += len(catalog["geometry"])

if top["set"]:
    index = reference(root / "catalog.json", top["set"])
    outfit = read(index)
    assert outfit["type"] == "armor-set"
    armor_slots = {p["slot"] for p in outfit["pieces"]}
    assert not armor_slots.intersection({9, 10})
    for piece in outfit["pieces"]:
        assert read(reference(index, piece["catalog"]))["type"] == "armor"
    for variant in outfit["variants"]:
        path = reference(index, variant["catalog"])
        rules = read(path)
        assert {p["slot"] for p in rules["items"]} == armor_slots
        for texture in rules["textures"]:
            reference(path, texture["file"])

print(json.dumps(dict(items=len(top["items"]), variants=variants, geometry=geometry, shared_textures=len(list((root / "textures").glob("*.png")))), indent=2))
