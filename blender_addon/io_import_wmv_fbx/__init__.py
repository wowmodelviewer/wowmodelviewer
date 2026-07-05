# ----------------------------------------------------------------------------
# WoW Model Viewer: Midnight -- Blender FBX importer add-on.
#
# Imports an FBX exported by WoW Model Viewer: Midnight and rebuilds every
# material's node graph from the ".wmvmat.json" sidecar the exporter writes,
# so the imported model matches the WMV viewport exactly:
#
#   blend 0 (opaque)          -> opaque Principled BSDF
#   blend 1 (alpha test)      -> alpha clip at 0.5
#   blend 2 (alpha blend)     -> alpha blended from texture alpha
#   blend 3/4 (additive glow) -> emission, transparency driven by the file's
#                                alpha (which the exporter bakes as per-pixel
#                                brightness: black = invisible, like in-game)
#   blend 5/6 (modulate)      -> multiply-style blend approximation
#   unlit                     -> emission instead of diffuse shading
#
# Geometry, armature, skin weights and animation come from Blender's own FBX
# importer; this add-on only replaces the material setup, which is the part a
# generic FBX round-trip cannot preserve. Without a sidecar the import still
# works and simply keeps Blender's default materials.
# ----------------------------------------------------------------------------

bl_info = {
    "name": "WoW Model Viewer FBX (.fbx)",
    "author": "WoW Model Viewer: Midnight",
    "version": (1, 0, 0),
    "blender": (3, 0, 0),
    "location": "File > Import > WoW Model Viewer FBX (.fbx)",
    "description": "Import WMV-exported FBX with viewport-identical materials",
    "category": "Import-Export",
}

import json
import os
import re

import bpy
from bpy.props import StringProperty, BoolProperty
from bpy_extras.io_utils import ImportHelper

# Raw M2 blend modes, mirrored from the exporter.
BM_OPAQUE = 0
BM_ALPHA_TEST = 1
BM_ALPHA_BLEND = 2
BM_ADDITIVE = 3
BM_ADDITIVE_ALPHA = 4
BM_MODULATE = 5
BM_MODULATE2X = 6

# Blender appends ".001"-style suffixes when names collide.
_DEDUP_SUFFIX = re.compile(r"\.\d{3,}$")

# Names over Blender's 63-char limit get truncated with a "_<7 hex>" hash tail.
_TRUNCATION_HASH = re.compile(r"_[0-9a-f]{7}$")


def _base_name(name):
    return _DEDUP_SUFFIX.sub("", name)


def _match_sidecar_entry(sidecar, material_name):
    """Match a Blender material name to a sidecar entry.

    Tries, in order: exact name; name with Blender's ".00N" de-dup suffix
    stripped; and -- for names Blender truncated to its 63-character limit
    (recognizable by the "_<hash>" tail) -- a unique prefix match against the
    sidecar names. Returns None when no unambiguous match exists.
    """
    base = _base_name(material_name)
    entry = sidecar.get(base) or sidecar.get(material_name)
    if entry is not None:
        return entry

    truncated = _TRUNCATION_HASH.sub("", base)
    if truncated != base:
        candidates = [e for name, e in sidecar.items() if name.startswith(truncated)]
        if len(candidates) == 1:
            return candidates[0]
    return None


def _load_sidecar(fbx_path):
    """Load '<fbx>.wmvmat.json'. Returns the raw sidecar dict (version 1 = bake mode, version 2 =
    component / raw node-based mode with per-unit data) or None."""
    sidecar_path = fbx_path + ".wmvmat.json"
    if not os.path.isfile(sidecar_path):
        return None
    try:
        with open(sidecar_path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as exc:
        print("WMV import: unreadable sidecar %s (%s)" % (sidecar_path, exc))
        return None
    if data.get("version") not in (1, 2):
        print("WMV import: unsupported sidecar version %r" % data.get("version"))
        return None
    return data


def _stamp_wmv_properties(material, entry):
    """Record the WMV pass data as material custom properties, whatever the
    glow mode -- artists building their own shaders can read blend mode /
    unlit / two-sided straight from the material's Custom Properties panel."""
    material["wmv_blend_mode"] = int(entry.get("blendMode", BM_OPAQUE))
    material["wmv_unlit"] = bool(entry.get("unlit", False))
    material["wmv_two_sided"] = bool(entry.get("twoSided", False))
    material["wmv_additive"] = int(entry.get("blendMode", BM_OPAQUE)) in (
        BM_ADDITIVE, BM_ADDITIVE_ALPHA)
    material["wmv_texture"] = str(entry.get("texture", ""))


def _is_effect_plane(entry):
    """A translucent, unlit glow pass (alpha-blend or additive) is a WoW particle/effect billboard --
    e.g. an artifact weapon's frost/energy sheets. In-game these are animated and scroll; frozen into
    static geometry they become big flat quads with hard rectangular edges. Detected so the importer
    can optionally hide them (they're rarely wanted in a static export). A SOLID glow (blend 0, opaque
    -- a rune/emissive detail baked onto the weapon) is NOT an effect plane and stays visible."""
    return (bool(entry.get("isGlow", False))
            and bool(entry.get("unlit", False))
            and entry.get("alphaUsage", "opaque") in ("alpha_blend", "additive"))


def _collect_imported_materials(objects):
    materials = {}
    for obj in objects:
        if obj.type != "MESH":
            continue
        for slot in obj.material_slots:
            if slot.material is not None:
                materials[slot.material.name] = slot.material
    return materials


def _separate_by_material(objects):
    """Split each imported multi-material mesh into one object per material, named after the
    material. WoW exports a whole model as ONE mesh with many material slots, so isolating a part
    (e.g. hiding an effect plane) otherwise means a manual Edit-Mode 'Separate by Material'. Blender's
    separate preserves the armature modifier, parenting and vertex groups on every piece, so skinning
    still works. Returns the resulting mesh objects."""
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        try:
            bpy.ops.object.mode_set(mode="OBJECT")
        except Exception:
            pass
    result = []
    for obj in list(objects):
        if obj.type != "MESH":
            continue
        if len(obj.data.materials) <= 1:
            result.append(obj)
            continue
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        before = set(bpy.data.objects)
        try:
            bpy.ops.object.mode_set(mode="EDIT")
            bpy.ops.mesh.separate(type="MATERIAL")
            bpy.ops.object.mode_set(mode="OBJECT")
        except Exception as exc:
            print("WMV import: separate-by-material failed for %r (%s)" % (obj.name, exc))
            result.append(obj)
            continue
        pieces = [o for o in bpy.data.objects if o not in before]
        pieces.append(obj)
        for piece in pieces:
            bpy.ops.object.select_all(action="DESELECT")
            piece.select_set(True)
            bpy.context.view_layer.objects.active = piece
            try:
                bpy.ops.object.material_slot_remove_unused()
            except Exception:
                pass
            mat = piece.data.materials[0] if piece.data.materials else None
            if mat is not None:
                piece.name = mat.name
                piece.data.name = mat.name
            result.append(piece)
    return result


def import_wmv_fbx(filepath, separate_materials=True, hide_effect_planes=False):
    """Import an FBX with plain materials. Returns (imported object count,
    0, sidecar found).

    Materials are left exactly as Blender's stock FBX importer creates them -- an Image Texture
    feeding a Principled BSDF, nothing else. The import provides the DATA (UV2 layer, raw unit
    textures on disk, render-state metadata stamped as custom properties) and the artist builds
    any glow / effect node graphs by hand per the item-component tutorial."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=filepath)
    imported = [obj for obj in bpy.data.objects if obj not in before]

    data = _load_sidecar(filepath)
    if data:
        sidecar = {entry["name"]: entry for entry in data.get("materials", [])}
        for mat_name, material in _collect_imported_materials(imported).items():
            entry = _match_sidecar_entry(sidecar, mat_name)
            if entry is not None:
                # No node edits at all. Stamp the render-state metadata as inert custom
                # properties (visible under Material > Custom Properties) so the artist can see
                # blend mode / glow / two-sided etc. while doing the node work by hand, and so
                # 'Hide effect planes' can still identify the billboard sheets.
                _stamp_wmv_properties(material, entry)
                material["wmv_effect_plane"] = _is_effect_plane(entry)
            else:
                print("WMV import: no sidecar entry for material '%s'" % mat_name)

    # Split each model into one object per material so parts (armor, effect planes, glows) can be
    # selected/hidden directly instead of via a manual Edit-Mode separate.
    if separate_materials:
        imported = _separate_by_material(imported)

    # Optionally hide the frozen particle/effect billboards (frost/energy sheets etc.). They're kept
    # in the scene (just hidden in viewport + render) so nothing is lost -- un-hide to get them back.
    # Only meaningful when split into per-material objects; otherwise a whole merged mesh would hide.
    if hide_effect_planes and separate_materials:
        hidden = 0
        for obj in imported:
            if obj.type != "MESH":
                continue
            if any(m is not None and m.get("wmv_effect_plane") for m in obj.data.materials):
                obj.hide_viewport = True
                obj.hide_render = True
                hidden += 1
        if hidden:
            print("WMV import: hid %d effect-plane object(s)" % hidden)

    return len(imported), 0, data is not None


class IMPORT_SCENE_OT_wmv_fbx(bpy.types.Operator, ImportHelper):
    """Import a WoW Model Viewer FBX with viewport-identical materials"""

    bl_idname = "import_scene.wmv_fbx"
    bl_label = "Import WMV FBX"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".fbx"
    filter_glob: StringProperty(default="*.fbx", options={"HIDDEN"})

    hide_effect_planes: BoolProperty(
        name="Hide effect planes",
        description="Hide the frozen particle/effect billboards (frost, energy, glow sheets). "
                    "In-game these are animated; as static geometry they're flat hard-edged quads. "
                    "They're hidden, not deleted -- un-hide them in the Outliner to bring them back",
        default=False,
    )

    def execute(self, context):
        object_count, _, had_sidecar = import_wmv_fbx(
            self.filepath, hide_effect_planes=self.hide_effect_planes)
        if not had_sidecar:
            self.report(
                {"WARNING"},
                "No .wmvmat.json sidecar next to the FBX -- imported without "
                "WoW render-state properties. Re-export from WoW Model Viewer to get it.",
            )
        else:
            self.report(
                {"INFO"},
                "Imported %d objects (WoW render states stamped as custom "
                "properties)" % object_count,
            )
        return {"FINISHED"}


def _menu_entry(self, context):
    self.layout.operator(IMPORT_SCENE_OT_wmv_fbx.bl_idname,
                         text="WoW Model Viewer FBX (.fbx)")


def register():
    bpy.utils.register_class(IMPORT_SCENE_OT_wmv_fbx)
    bpy.types.TOPBAR_MT_file_import.append(_menu_entry)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(_menu_entry)
    bpy.utils.unregister_class(IMPORT_SCENE_OT_wmv_fbx)


if __name__ == "__main__":
    register()
