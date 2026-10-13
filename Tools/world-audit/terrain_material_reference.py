"""Independent CPU reference of the stage 04 terrain materials: per-chunk material states and layers, the composite of
one chunk at given positions from given texels (V1, validation sampling), and the map-texture mode that reproduces the
game's own baked map texture (MAID word 5) for the golden tests and V2.

PARSER-INDEPENDENT, DESIGN-DEPENDENT (like terrain_reference.py). Files are read with the audit's `wowfmt` chunk
readers and this file's own alpha and BLP decoders, never with the C# code, so a reading fault in C# shows up as a
difference. The material rules are the stage 04 design's (v3: sections 1 and 2 of the delta), so a design error is
caught only by the golden tests against the game's map texture, which this file also provides.

Rules applied:
  chunk states, the first that applies wins (chunk_state names every condition):
    Malformed, AlphaUnsupported, LayerCountUnsupported, LayerEffectUnresolved, HeightTextureMissing, NoLayers,
    LayerEffectStatic, MissingTexture, Textured; notes (counted, drawing unchanged): MCSH present, MCCV present
    without MCNK flag 0x40 or the reverse
  tile tables: a repeated MDID, MHID or MTXP chunk, an MDID or MHID that is not whole u32 ids, an MHID whose entry
    count differs from MDID's (when MHID is present) and an MTXP that is not one 16-byte entry per MDID entry make
    every chunk with a layer Malformed, as do a layer's MTXP height values that are not finite; an unreadable MHID
    is never read as "no height textures", an absent one is (observed: Beta Azeroth); with MPHD 0x80, a layer whose
    height texture failed makes the chunk HeightTextureMissing, and so does a height texture whose BLP has no alpha
    channel (alpha depth 0: the height is the alpha; failed for the height role only, the same id stays usable as a
    diffuse texture)
  layers: MCLY order; texture = MDID[index]; repeats per chunk = 8 / 2^n for the MTXP texture scale nibble
    n = (MTXP[index].flags >> 4) & 0xF, 8 without MTXP: one texture repeat per 2^n cells, a float (0.5 at n = 4, never
    the integer 8 >> 4 = 0); at most 8 layers are drawn (6..8 composite by the same rule; 8 is the most observed);
    MCLY bits 0x100 (alpha) and 0x200 (RLE) are the storage, 0x40 (animation, with
    0x7 rotation and 0x38 speed) and 0x80 (overbright) are drawn static (LayerEffectStatic: what the game's baked
    map texture shows), 0x400 (cube map) and every other bit, MTXP flag bits outside 0xF0 (0x1 = cube-map texture),
    a non-zero MTXP word 3 and an MTXP scale nibble above 4 (0..4 verified; 5..15 never observed) are
    LayerEffectUnresolved; so is, with MPHD 0x80 and an MTXP entry, height values
    (s, o) as stored (float32) that let H = h * s + o fall below 0 or overflow float32: o < 0, or with a height
    texture (MHID entry not 0) s + o < 0 or s + o not finite in float32 (the height mix is not defined there); without
    0x80 the values are ignored (H = 1) and give no state
  alpha: texel i = row * 64 + col, row south, col east; layers after the first carry maps back to back in MCAL from
    offset 0; MCLY 0x200 = RLE (4096 values), else the spacing decides: 4096 bytes 8-bit, 2048 bytes 4-bit (low
    nibble first, kept as 0..15 and never drawn: its draw-time expansion is not established); MPHD & 0x84 must agree
    with the spacing; texel i is centred at alpha_texel_centre(i) = (i + 0.5) / 64 of the chunk (PROVEN for the map
    generator, INFERRED for the client)
  weights: a_k = alpha_k / 255 for k >= 1, w_0 = 1 - sum(a_k), w_k = a_k
  height mix (the composite rule; measured on the game's map textures, plain weights fit worse on every golden tile):
    H_k = 1 when MPHD & 0x80 is clear (MHID and MTXP height values ignored); with 0x80 set H_k = h_k * s_k + o_k,
    h_k = the alpha of layer k's height texture (MHID[index]) at the layer's own UV, sampled like its diffuse
    texture, / 255 (h_k = 0 when MHID[index] is 0), (s_k, o_k) = MTXP words 1 and 2, (0, 1) without MTXP;
    p_k = max(w_k, 0) * H_k; m = max_k p_k; p_k *= 1 - clamp(m - p_k, 0, 1); S = sum_k p_k;
    colour = sum_k p_k * texture_k / S, and sum_k max(w_k, 0) * texture_k when S <= 0; in stored (authored) values,
    never linearised. S < 0 needs some H_k < 0 (not only every H_k <= 0: H = (0.3, -1) at a = 0.5 gives it), and such
    height values are LayerEffectUnresolved (above), so of the fallback only S == 0 (every p_k = 0, e.g. H = 0
    exactly) reaches a drawn chunk; that fallback is the design's, not verified (no audited data has H = 0)
  texture UV: texel (0, 0) at the chunk's north-west corner, u east, v south, no flip; continuous across chunks:
    a layer's coordinate is s = uv_cells * repeats / 8 plus the chunk phase (frac(ix * repeats), frac(iy * repeats))
    (chunk_phase; 0 for whole repeats, so it matters only at nibble 4), for its diffuse and its height texture alike
    (nibble 4 and 6..8 layers PROVEN for the map generator on Retail map 2923, 5 tiles; INFERRED for draw time)
  vertex colour: MCCV entries are B, G, R, A bytes; colour * byte / 127 per channel, interpolated over the cell's
    four-triangle fan (outer corners and the inner vertex); applied only when MCNK flag 0x40 and MCCV both hold, and
    only to chunks with layers to draw: the drawable ones and those kept for the plain-weights diagnostic view
    (LayerEffectUnresolved, HeightTextureMissing); NoLayers and the other neutral chunks get none
  map-texture mode: 32 px per chunk (512 per tile), north row first; each layer (and each height texture) from the
    BLP's own stored mip at LOD log2(texels per map pixel) (one texel per pixel, sampled at its centre; a texture
    with no stored level of one texel per pixel on both axes -- non-square, magnified, or the level not stored -- is
    sampled bilinearly from the nearest level WITH A NOTE, never silently); alpha at pixel centres (the 2 x 2 mean);
    x MCCV; clipped to 0..255 (the map texture's 8-bit range). The height texture's repeats (the layer's own) and
    channel (alpha) are gated by the golden tests (8 repeats, 1 repeat and the red channel fit worse on 30_49's
    height chunks); its LOD (which stored mip) is INFERRED: one mip finer fits within 0.01 on every tile, no fit
    separates them
  block decodes: 'exact' (the format: 565 endpoints by bit replication, unrounded interpolants), 'hardware' (the
    four-colour interpolants and DXT5's interpolated alphas as THIS GPU decodes them, measured on the player's texel
    dumps, not a format rule: hardware_interp5/6, hardware_alpha; a DXT1 three-colour midpoint has no measured model
    and stays exact) or 'rounded' (the exact decode rounded to nearest, the midpoint's ties to even: the player's
    CPU decode of a block texture it uploads as RGBA32)
  validation sampling (V1): texture (and height) texel by integer index floor((u * repeats + phase_u) * width) mod
    width (rows likewise; the chunk phase as above); alpha by manual bilinear at the texel-centre alignment with
    clamp to the chunk; MCCV barycentric over the fan; float output, not clipped; a missing layer is a magenta/black
    checker, one square per 1/8 cell
  unresolved repeats (an MTXP scale nibble above 4: repeats 0, LayerEffectUnresolved, drawn only where a caller asks
    for non-drawable states): the layer is the checker, as a missing one (validation and map-texture mode, as C#
    TerrainCompositor.LayerTexel), and a height texture the height mix would read on it (MHID entry not 0, heights in
    the mix) has no defined coordinate: ValueError (as C# HeightAt); chunkdump refuses every chunk that is not drawable
  values are 0..255 floats throughout (texels, composites); dumps hold 0..1 and are scaled on reading

  python -I terrain_material_reference.py states <wdt> <x> <y> <root adt> <tex0 adt> [--missing id,id,...]
  python -I terrain_material_reference.py maptexture <audit data> <client> <wdt> <x> <y> <out base>
      writes <out base>.raw/.json/.png in the composite dump format (compare_terrain_material.py)
  python -I terrain_material_reference.py chunkdump <audit data> <client> <wdt> <x> <y> <chunk> <out base>
      writes the reference's own V1 chunk dump (validation sampling, size N) and <out base>_texels.json; a chunk that
      is not drawable (Textured, MissingTexture, LayerEffectStatic) is refused with its state and reason
  python -I terrain_material_reference.py golden <audit data> [--quick] [<label> ...]
  python -I terrain_material_reference.py converter <audit data>
"""
import hashlib
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import terrain_reference as TR  # noqa: E402
from wowfmt import adt as A  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

ALPHA_SIZE = 64
ALPHA_TEXELS = ALPHA_SIZE * ALPHA_SIZE
MAX_PARSED_LAYERS = 8               # more than this is malformed (the C# parser's limit as well)
MAX_DRAWN_LAYERS = 8                # more is LayerCountUnsupported; 6..8 layers composite by the same rule (map 2923)
BASE_REPEATS = 8                    # texture repeats per chunk: one per cell
MAX_SCALE_SHIFT = 4                 # MTXP scale nibbles verified 0..4; above: LayerEffectUnresolved (never observed)
ALPHA_FLAG = 0x100
RLE_FLAG = 0x200
STATIC_EFFECT_FLAGS = 0x7 | 0x38 | 0x40 | 0x80     # rotation, speed, animation, overbright: drawn static
CUBE_MAP_FLAG = 0x400               # MCLY cube-map layer: not established
KNOWN_LAYER_FLAGS = ALPHA_FLAG | RLE_FLAG | STATIC_EFFECT_FLAGS   # any other MCLY bit: LayerEffectUnresolved
KNOWN_MTXP_FLAGS = 0xF0             # the scale nibble; 0x1 = cube-map texture and every other bit: unresolved
DEFAULT_HEIGHT = (0.0, 1.0)         # (heightScale, heightOffset) of a texture without an MTXP entry
MPHD_8BIT = 0x84
MPHD_HEIGHT = 0x80                  # the height textures (MHID) and MTXP height values enter the mix
MCNK_DO_NOT_FIX_ALPHA = 0x8000
MCNK_HAS_MCCV = 0x40
MCCV_DIVISOR = 127.0                # 0x7F = 1.0; 127, 128 and 2/255 are not separable on the evidence
MAP_PIXELS_PER_CHUNK = 32
MAP_SIZE = 16 * MAP_PIXELS_PER_CHUNK
GOLDEN_ALLOWANCE = 0.25
STATES = ('Malformed', 'AlphaUnsupported', 'LayerCountUnsupported', 'LayerEffectUnresolved', 'HeightTextureMissing',
          'NoLayers', 'LayerEffectStatic', 'MissingTexture', 'Textured')
COUNTER_NAMES = {'Malformed': 'malformed', 'AlphaUnsupported': 'alphaUnsupported',
                 'LayerCountUnsupported': 'layerCountUnsupported', 'LayerEffectUnresolved': 'layerEffectUnresolved',
                 'HeightTextureMissing': 'heightTextureMissing', 'NoLayers': 'noLayers',
                 'LayerEffectStatic': 'layerEffectStatic', 'MissingTexture': 'missingTexture', 'Textured': 'textured'}
DRAWABLE = ('Textured', 'MissingTexture', 'LayerEffectStatic')
# Chunks that get vertex colours (and count as MCCV applied): the drawable ones and the two neutral states that keep
# their layers for the labelled plain-weights diagnostic view; every other chunk (NoLayers included) gets none.
VERTEX_COLOUR_STATES = DRAWABLE + ('LayerEffectUnresolved', 'HeightTextureMissing')
# What the golden tests and V2 compare: the states drawn with the composite. LayerEffectStatic keeps its precedence over
# MissingTexture (a static chunk whose texture failed stays LayerEffectStatic and draws the checker for that layer), so
# material_keep also drops every chunk with a checker layer (texture id 0 or failed) whatever its state.
GOLDEN_STATES = ('Textured', 'LayerEffectStatic')
HEIGHT_NO_ALPHA = 'decode failed: no alpha channel (BLP alphaDepth 0); the height is the alpha'
DECODE_MODELS = ('exact', 'hardware', 'rounded')
DECODED_UPLOAD = 'R8G8B8A8_UNorm'   # the upload format of a texture the player decodes on the CPU (texel-dump entry)
HEIGHT_MODES = ('design', 'default', 'full')
CHECKER_ON = (255.0, 0.0, 255.0)
CHECKER_OFF = (0.0, 0.0, 0.0)

# The 8 symmetries of a square grid: (name, transpose, mirror rows, mirror columns), applied in that order.
SYMMETRIES = [('identity', 0, 0, 0), ('mirror columns (east-west)', 0, 0, 1), ('mirror rows (north-south)', 0, 1, 0),
              ('rotate 180', 0, 1, 1), ('transpose', 1, 0, 0), ('transpose + mirror columns', 1, 0, 1),
              ('transpose + mirror rows', 1, 1, 0), ('anti-transpose', 1, 1, 1)]


class MaterialUnavailable(Exception):
    """The tex0 (or root) cannot give a material at all; every chunk would be neutral."""


def alpha_texel_centre(i):
    """Where alpha texel i sits across the chunk, 0..1 (one named function: INFERRED for the client)."""
    return (i + 0.5) / ALPHA_SIZE


def sym_source(sym, r, c, n):
    """Source (row, col) in an n x n grid of output (r, c) under symmetry `sym` (an index into SYMMETRIES)."""
    _, tr, fr, fc = SYMMETRIES[sym]
    if fc:
        c = n - 1 - c
    if fr:
        r = n - 1 - r
    return (c, r) if tr else (r, c)


def transform_grid(values, n, sym, channels=1):
    """`values` (n x n, row-major, `channels` interleaved) under a symmetry."""
    if sym == 0:
        return list(values)
    out = [0] * (n * n * channels)
    for r in range(n):
        for c in range(n):
            sr, sc = sym_source(sym, r, c, n)
            s, d = (sr * n + sc) * channels, (r * n + c) * channels
            out[d:d + channels] = values[s:s + channels]
    return out


def sha12(values):
    return hashlib.sha256(bytes(values)).hexdigest()[:12]


def md5_hex(data):
    """The MD5 of a file's bytes, lowercase hex: what the host sends as the answer's content key."""
    return hashlib.md5(data).hexdigest()


def read_bytes(path):
    with open(path, 'rb') as f:
        return f.read()


# ------------------------------------------------------------------------------------------------- BLP

class BlpError(ValueError):
    pass


def expand565(c):
    """A 565 colour as 8-bit values by bit replication (what the GPU's block decoder does)."""
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return float((r << 3) | (r >> 2)), float((g << 2) | (g >> 4)), float((b << 3) | (b >> 2))


# The 'hardware' block decode: how THIS GPU decodes the interpolated texels, MEASURED on the player's texel dumps (a
# property of the GPU, not of the format; the format allows its own rounding). Colour: every four-colour interpolant
# of the stage's 9 integration texel dumps (Retail 32_50, 34_46, 32_48, 30_49, Beta 31_47, Era map 2853 34_50) equals
# hardware_interp5/6 exactly (the player self-test's model): 4,193,774 texels, 2,690,652 of them DXT3 (75 texture
# entries) and 1,503,122 DXT5 (15), the sum of compare_terrain_material.py texels' colour interpolant counts. No
# real DXT1 interpolant exists: the one DXT1 terrain texture dumped (188903, 8 x 8) has none, so for DXT1 four-colour
# blocks the model is confirmed on the player self-test's synthetic DXT1 only.
# DXT5 alpha: alpha = (a0 * (2048 - W) + a1 * W + 1024) >> 11 with the per-selector weights below; each weight is the
# only one of 0..2048 that matches every interpolated alpha of the Retail 30_49 dump (72,666 distinct (a0, a1,
# selector) inputs), and the fit matches every one of the 31,964 inputs of the Retail 34_46 / 32_48 and Beta 31_47
# dumps it never saw (84,366 distinct inputs in all, 0 off). The weights are symmetric (W_s + W_(9-s) = 2048) and
# none is the format's k/7 or k/5.
HARDWARE_ALPHA_WEIGHTS8 = (289, 578, 892, 1156, 1470, 1759)     # selectors 2..7 of the eight-value mode (a0 > a1)
HARDWARE_ALPHA_WEIGHTS6 = (385, 770, 1278, 1663)                # selectors 2..5 of the six-value mode (a0 <= a1)


def hardware_interp5(a, b):
    """MEASURED: a 5-bit channel's four-colour interpolant nearer a, from the STORED 5-bit values: ((2a + b) * 22) >> 3."""
    return float(((2 * a + b) * 22) >> 3)


def hardware_interp6(a, b):
    """MEASURED: the 6-bit channel's interpolant nearer a, from the bit-replicated 8-bit values a, b:
    (256 a + trunc((b - a) / 4) + 128 + 80 (b - a)) >> 8 (never negative before the shift)."""
    d = b - a
    q = abs(d) // 4
    return float((256 * a + (q if d >= 0 else -q) + 128 + 80 * d) >> 8)


def hardware_alpha(a0, a1, sel):
    """MEASURED: DXT5's interpolated alpha of selector `sel` (2..7 with a0 > a1, 2..5 with a0 <= a1)."""
    w = HARDWARE_ALPHA_WEIGHTS8[sel - 2] if a0 > a1 else HARDWARE_ALPHA_WEIGHTS6[sel - 2]
    return float((a0 * (2048 - w) + a1 * w + 1024) >> 11)


def nearest(n, d):
    """n / d (integers, n >= 0, d > 0) rounded to the nearest integer, ties to even, as a float: the 'rounded' block
    decode (the player's RGBA32 fallback, BlpDecoder's exact-blocks mode). For d = 3, 5 and 7 no tie occurs; for
    d = 2 an odd n is a tie."""
    q, rem = divmod(n, d)
    if 2 * rem > d or (2 * rem == d and q & 1):
        q += 1
    return float(q)


class Blp:
    """A BLP2 file: header, stored mip table and per-level decoding to RGBA (0..255 floats, rows as stored, row 0
    first). Encodings: 1 palettised (alpha depth 0/1/4/8), 2 block-compressed (alpha type 0 = DXT1, 1 = DXT3,
    7 = DXT5), 3 uncompressed BGRA. model 'exact' (default): block colours by bit replication and unrounded
    interpolation, DXT5 alpha ramps unrounded (the format); model 'hardware': the four-colour interpolants and DXT5's
    interpolated alphas by the measured GPU decode (hardware_interp5/6, hardware_alpha), everything else as 'exact'
    (endpoints, DXT3 alpha, DXT5 a0/a1/0/255 and a DXT1 three-colour block's midpoint, which has no measured model);
    model 'rounded': the format's decode rounded to the nearest 8-bit value (nearest(): interpolants, DXT5 alpha ramps
    and the DXT1 three-colour midpoint, ties to even), what the player uploads when it decodes a block texture on the
    CPU (BlpDecoder's exact-blocks mode, the self-test's FallbackError), everything else as 'exact'.
    DXT3/DXT5 colour blocks are always four-colour; a DXT1 three-colour block's index 3 is (0, 0, 0, 0).
    Malformed input raises BlpError; nothing is read past the file."""

    HEADER = 148
    PALETTE = 1024

    def __init__(self, buf, what='BLP', model='exact'):
        if model not in DECODE_MODELS:
            raise ValueError('decode model %r (%s)' % (model, ', '.join(DECODE_MODELS)))
        self.model = model
        self.buf = buf
        self.what = what
        if len(buf) < self.HEADER or buf[:4] != b'BLP2':
            raise BlpError('%s: not a BLP2 file' % what)
        self.version = struct.unpack_from('<I', buf, 4)[0]
        self.encoding, self.alpha_depth, self.alpha_type, self.has_mips = buf[8], buf[9], buf[10], buf[11]
        self.width, self.height = struct.unpack_from('<II', buf, 12)
        offsets = struct.unpack_from('<16I', buf, 20)
        sizes = struct.unpack_from('<16I', buf, 84)
        if not (0 < self.width <= 65536 and 0 < self.height <= 65536):
            raise BlpError('%s: size %dx%d' % (what, self.width, self.height))
        if self.encoding not in (1, 2, 3):
            raise BlpError('%s: encoding %d is not supported' % (what, self.encoding))
        if self.encoding == 2 and self.alpha_type not in (0, 1, 7):
            raise BlpError('%s: block alpha type %d is not supported' % (what, self.alpha_type))
        if self.encoding == 1 and self.alpha_depth not in (0, 1, 4, 8):
            raise BlpError('%s: palettised alpha depth %d is not supported' % (what, self.alpha_depth))
        chain = max(self.width, self.height).bit_length()
        self.levels = []
        for lv in range(min(16, chain)):
            o, s = offsets[lv], sizes[lv]
            if not o or not s:
                break
            need = self.level_bytes(lv)
            if s < need or o + need > len(buf):
                raise BlpError('%s: level %d (%d bytes at %d) needs %d bytes inside the %d-byte file'
                               % (what, lv, s, o, need, len(buf)))
            self.levels.append(o)
        if not self.levels:
            raise BlpError('%s: no stored level' % what)
        self._cache = {}

    @property
    def format(self):
        if self.encoding == 2:
            return {0: 'DXT1', 1: 'DXT3', 7: 'DXT5'}[self.alpha_type]
        return 'palettised' if self.encoding == 1 else 'BGRA'

    def level_size(self, lv):
        return max(1, self.width >> lv), max(1, self.height >> lv)

    def level_bytes(self, lv):
        w, h = self.level_size(lv)
        if self.encoding == 2:
            return ((w + 3) // 4) * ((h + 3) // 4) * (8 if self.alpha_type == 0 else 16)
        if self.encoding == 3:
            return 4 * w * h
        return w * h + {0: 0, 1: (w * h + 7) // 8, 4: (w * h + 1) // 2, 8: w * h}[self.alpha_depth]

    def decode(self, lv=0):
        """(width, height, flat RGBA list of 0..255 floats) of stored level `lv`."""
        if not 0 <= lv < len(self.levels):
            raise BlpError('%s: level %d is not stored (%d levels)' % (self.what, lv, len(self.levels)))
        if lv not in self._cache:
            w, h = self.level_size(lv)
            if self.encoding == 2:
                px = self._decode_blocks(self.levels[lv], w, h)
            elif self.encoding == 3:
                px = self._decode_bgra(self.levels[lv], w, h)
            else:
                px = self._decode_palettised(self.levels[lv], w, h)
            self._cache[lv] = (w, h, px)
        return self._cache[lv]

    def _decode_bgra(self, o, w, h):
        b = self.buf
        out = [0.0] * (4 * w * h)
        for i in range(w * h):
            p = o + 4 * i
            out[4 * i:4 * i + 4] = (float(b[p + 2]), float(b[p + 1]), float(b[p]), float(b[p + 3]))
        return out

    def _decode_palettised(self, o, w, h):
        b = self.buf
        n = w * h
        pal = [(float(b[self.HEADER + 4 * k + 2]), float(b[self.HEADER + 4 * k + 1]), float(b[self.HEADER + 4 * k]))
               for k in range(256)]
        a0 = o + n
        out = [0.0] * (4 * n)
        for i in range(n):
            if self.alpha_depth == 0:
                a = 255.0
            elif self.alpha_depth == 1:
                a = 255.0 if (b[a0 + i // 8] >> (i % 8)) & 1 else 0.0
            elif self.alpha_depth == 4:
                a = float(((b[a0 + i // 2] >> (4 * (i % 2))) & 15) * 17)
            else:
                a = float(b[a0 + i])
            out[4 * i:4 * i + 4] = pal[b[o + i]] + (a,)
        return out

    def _decode_blocks(self, o, w, h):
        b = self.buf
        bw, bh = (w + 3) // 4, (h + 3) // 4
        dxt1 = self.alpha_type == 0
        size = 8 if dxt1 else 16
        out = [0.0] * (4 * w * h)
        for by in range(bh):
            for bx in range(bw):
                p = o + size * (by * bw + bx)
                alphas = None
                if not dxt1:
                    alphas = self._block_alpha(p)
                    p += 8
                c0, c1, bits = struct.unpack_from('<HHI', b, p)
                p0, p1 = expand565(c0), expand565(c1)
                if (c0 > c1 or not dxt1) and self.model == 'hardware':
                    r, g, bb = ((c0 >> 11) & 31, (c1 >> 11) & 31), (p0[1], p1[1]), (c0 & 31, c1 & 31)
                    pal = [p0 + (255.0,), p1 + (255.0,),
                           (hardware_interp5(r[0], r[1]), hardware_interp6(int(g[0]), int(g[1])),
                            hardware_interp5(bb[0], bb[1]), 255.0),
                           (hardware_interp5(r[1], r[0]), hardware_interp6(int(g[1]), int(g[0])),
                            hardware_interp5(bb[1], bb[0]), 255.0)]
                elif (c0 > c1 or not dxt1) and self.model == 'rounded':
                    pal = [p0 + (255.0,), p1 + (255.0,),
                           tuple(nearest(int(2 * x + y), 3) for x, y in zip(p0, p1)) + (255.0,),
                           tuple(nearest(int(x + 2 * y), 3) for x, y in zip(p0, p1)) + (255.0,)]
                elif c0 > c1 or not dxt1:
                    pal = [p0 + (255.0,), p1 + (255.0,),
                           tuple((2 * x + y) / 3.0 for x, y in zip(p0, p1)) + (255.0,),
                           tuple((x + 2 * y) / 3.0 for x, y in zip(p0, p1)) + (255.0,)]
                else:
                    mid = (tuple(nearest(int(x + y), 2) for x, y in zip(p0, p1)) if self.model == 'rounded' else
                           tuple((x + y) / 2.0 for x, y in zip(p0, p1)))
                    pal = [p0 + (255.0,), p1 + (255.0,), mid + (255.0,), (0.0, 0.0, 0.0, 0.0)]
                for k in range(16):
                    y, x = by * 4 + k // 4, bx * 4 + k % 4
                    if x >= w or y >= h:
                        continue
                    c = pal[(bits >> (2 * k)) & 3]
                    d = 4 * (y * w + x)
                    out[d:d + 3] = c[:3]
                    out[d + 3] = c[3] if alphas is None else alphas[k]
        return out

    def _block_alpha(self, p):
        b = self.buf
        if self.alpha_type == 1:        # DXT3: 16 explicit 4-bit values, low nibble first
            return [float(((b[p + k // 2] >> (4 * (k % 2))) & 15) * 17) for k in range(16)]
        a0, a1 = float(b[p]), float(b[p + 1])
        bits = int.from_bytes(b[p + 2:p + 8], 'little')
        if self.model == 'hardware':
            pal = [a0, a1] + [hardware_alpha(b[p], b[p + 1], s) for s in range(2, 8 if a0 > a1 else 6)]
            pal += [] if a0 > a1 else [0.0, 255.0]
        elif self.model == 'rounded':
            n = 7 if a0 > a1 else 5
            pal = [a0, a1] + [nearest((n - i) * b[p] + i * b[p + 1], n) for i in range(1, n)]
            pal += [] if a0 > a1 else [0.0, 255.0]
        elif a0 > a1:
            pal = [a0, a1] + [((7 - i) * a0 + i * a1) / 7.0 for i in range(1, 7)]
        else:
            pal = [a0, a1] + [((5 - i) * a0 + i * a1) / 5.0 for i in range(1, 5)] + [0.0, 255.0]
        return [pal[(bits >> (3 * k)) & 7] for k in range(16)]


def channels(px):
    """Split a flat RGBA list into (R, G, B) lists."""
    return px[0::4], px[1::4], px[2::4]


# ------------------------------------------------------------------------------------------------- tex0 and root

def decode_rle(buf, start, end):
    """(4096 values, bytes used) or (None, reason). A count byte n: top bit set = fill the next byte n & 0x7F times,
    clear = copy n & 0x7F bytes; a run past the 4096th value or past `end` is an error, never truncated."""
    out = bytearray()
    p = start
    while len(out) < ALPHA_TEXELS:
        if p >= end:
            return None, 'compressed alpha ends after %d of 4096 values' % len(out)
        n = buf[p]
        p += 1
        count = n & 0x7F
        if count > ALPHA_TEXELS - len(out):
            return None, 'a run of %d at value %d crosses the end of the 4096-value map' % (count, len(out))
        if n & 0x80:
            if p >= end:
                return None, 'a fill run has no value byte'
            out.extend(bytes([buf[p]]) * count)
            p += 1
        else:
            if count > end - p:
                return None, 'a copy run of %d byte(s) passes the end of MCAL' % count
            out.extend(buf[p:p + count])
            p += count
    return bytes(out), p - start


def decode_alpha_maps(buf, mcal, layers):
    """Per layer (None for layer 0): (storage, 4096 values) with storage 'rle', 'raw8' or 'raw4'; or raises ValueError
    with the reason the layer list and MCAL disagree."""
    for k, lay in enumerate(layers):
        if bool(lay['flags'] & ALPHA_FLAG) != (k >= 1):
            raise ValueError('layer %d has flag 0x100 %s; exactly the layers after the first have it'
                             % (k, 'set' if lay['flags'] & ALPHA_FLAG else 'clear'))
    maps = [None] * len(layers)
    if len(layers) <= 1:
        return maps
    if mcal is None:
        raise ValueError('%d layers but no MCAL' % len(layers))
    base, size = mcal.data_offset, mcal.size
    running = 0
    for k in range(1, len(layers)):
        off = layers[k]['alpha_offset']
        if off != running:
            raise ValueError("layer %d's alpha starts at +%d, expected +%d" % (k, off, running))
        if layers[k]['flags'] & RLE_FLAG:
            values, used = decode_rle(buf, base + off, base + size)
            if values is None:
                raise ValueError('layer %d: %s' % (k, used))
            maps[k] = ('rle', values)
        else:
            nxt = layers[k + 1]['alpha_offset'] if k + 1 < len(layers) else size
            if nxt < off or nxt > size:
                raise ValueError('layer %d: uncompressed alpha [%d, %d) is outside the %d-byte MCAL' % (k, off, nxt, size))
            used = nxt - off
            p = base + off
            if used == ALPHA_TEXELS:
                maps[k] = ('raw8', bytes(buf[p:p + ALPHA_TEXELS]))
            elif used == ALPHA_TEXELS // 2:
                v = bytearray(ALPHA_TEXELS)
                for i in range(ALPHA_TEXELS // 2):
                    v[2 * i] = buf[p + i] & 15
                    v[2 * i + 1] = buf[p + i] >> 4
                maps[k] = ('raw4', bytes(v))
            else:
                raise ValueError('layer %d: uncompressed alpha of %d byte(s); only 4096 and 2048 are established' % (k, used))
        running += used
    if running != size:
        raise ValueError('the alpha maps use %d of MCAL\'s %d byte(s)' % (running, size))
    return maps


def read_tex0(buf):
    """The tex0 parts the material needs: MDID, MHID, MTXP entries (flags, height scale, height offset, word 3), and
    per MCNK its layers, alpha maps (or the reason they cannot be used) and whether MCSH is present. A malformed tile
    table is kept as `tile_problem` (MDID, MHID) or `mtxp_problem` (MTXP); it makes every chunk with a layer
    Malformed. Raises MaterialUnavailable when the file has no usable MCNK table."""
    sf = A.SplitFile(buf, root=False)
    if sf.walk.problems:
        raise MaterialUnavailable('tex0: ' + sf.walk.problems[0])
    if len(sf.mcnk) != 256:
        raise MaterialUnavailable('tex0 has %d MCNKs, 256 expected' % len(sf.mcnk))
    tile_problem = None

    def u32_table(cid):
        nonlocal tile_problem
        found = sf.walk.all(cid)
        if not found:
            return None
        if len(found) > 1:
            tile_problem = tile_problem or '%s appears %d times (which copy the game reads is not established)' % (
                cid, len(found))
        c = found[0]
        if c.size % 4:
            tile_problem = tile_problem or '%s is %d bytes, not whole u32 FileDataIDs' % (cid, c.size)
            return []
        return A.u32s(buf, c)

    mdid = u32_table('MDID')
    mhid = u32_table('MHID')
    mhid_present = mhid is not None
    mdid = mdid or []
    mhid = mhid or []
    if mhid_present and tile_problem is None and len(mhid) != len(mdid):
        tile_problem = 'MHID has %d entries, MDID %d (a missing height id is never read as "none")' % (len(mhid), len(mdid))
    mtxp = None
    mtxp_problem = None
    found = sf.walk.all('MTXP')
    if found:
        c = found[0]
        if len(found) > 1:
            mtxp_problem = 'MTXP appears %d times (which copy the game reads is not established)' % len(found)
        elif c.size % 16:
            mtxp_problem = 'MTXP is %d bytes, not whole 16-byte entries' % c.size
        mtxp = [struct.unpack_from('<IffI', buf, c.data_offset + 16 * k) for k in range(c.size // 16)]
        if mtxp_problem is None and len(mtxp) != len(mdid):
            mtxp_problem = 'MTXP has %d entries, MDID %d' % (len(mtxp), len(mdid))
    chunks = []
    for n, (mc, _, sub) in enumerate(sf.mcnk):
        ch = {'n': n, 'layers': [], 'alpha': [], 'problem': None, 'mcsh': False}
        chunks.append(ch)
        if sub.problems:
            ch['problem'] = 'tex0 MCNK sub-chunks: ' + sub.problems[0]
            continue
        mcly = sub.all('MCLY')
        mcal = sub.all('MCAL')
        ch['mcsh'] = bool(sub.all('MCSH'))
        if len(mcly) > 1 or len(mcal) > 1:
            ch['problem'] = 'MCLY or MCAL appears more than once'
            continue
        if mcly and (mcly[0].size % 16 or mcly[0].size // 16 > MAX_PARSED_LAYERS):
            ch['problem'] = 'MCLY of %d bytes (whole 16-byte layers, at most %d)' % (mcly[0].size, MAX_PARSED_LAYERS)
            continue
        ch['layers'] = A.mcly(buf, mcly[0]) if mcly else []
        bad = [k for k, lay in enumerate(ch['layers']) if lay['texture'] >= len(mdid)]
        if bad:
            ch['problem'] = 'layer %d names texture %d, but MDID has %d' % (bad[0], ch['layers'][bad[0]]['texture'], len(mdid))
            continue
        try:
            ch['alpha'] = decode_alpha_maps(buf, mcal[0] if mcal else None, ch['layers'])
        except ValueError as e:
            ch['problem'] = str(e)
    return {'mdid': mdid, 'mhid': mhid, 'mhid_present': mhid_present, 'mtxp': mtxp, 'mtxp_problem': mtxp_problem,
            'tile_problem': tile_problem, 'chunks': chunks}


def read_root(buf):
    """Per root MCNK: header flags, MCCV bytes (580) or None, hole mask, and the problem that makes the C# root
    parser mark it Malformed (terrain_reference's rules: stage 03 then builds no geometry for it)."""
    try:
        rows = TR.read_chunks(buf)
    except TR.BuildFailed as e:
        raise MaterialUnavailable('root: %s' % e)
    sf = A.SplitFile(buf, root=True)
    out = []
    for n, ((h, hv, problem), (_, _, sub)) in enumerate(zip(rows, sf.mcnk)):
        ch = {'n': n, 'flags': None, 'mccv': None, 'holes': 0, 'problem': problem,
              'geometry': 'Skipped' if problem else 'Built'}
        out.append(ch)
        if h is None:
            continue
        ch['flags'] = h['flags']
        if sub is not None:
            m = sub.all('MCCV')
            if len(m) == 1 and m[0].size == 4 * 145:
                ch['mccv'] = bytes(buf[m[0].data_offset:m[0].data_offset + 580])
        if h['flags'] & 0x10000:
            ch['holes'] = h['holes_high']
        elif h['holes_low']:
            ch['geometry'] = 'Unsupported'
    return out


# ------------------------------------------------------------------------------------------------- material

def _missing_text(missing, fd):
    return missing[fd] if isinstance(missing, dict) else 'failed'


def float32(x):
    """x rounded to float32 (round to nearest, as a float32 addition of float32 operands rounds: double rounding is
    innocuous for one addition); +-inf where float32 overflows."""
    try:
        return struct.unpack('<f', struct.pack('<f', x))[0]
    except OverflowError:
        return math.copysign(float('inf'), x)


def height_values_undefined(scale, offset, height_id):
    """True when H = h * scale + offset (h in 0..1; h = 0 without a height texture) can fall below 0 or overflow
    float32 for the stored float32 MTXP values: offset < 0, or with a height texture (MHID entry not 0) scale + offset
    < 0 or not finite in float32. Otherwise H stays in [min(o, s + o), max(o, s + o)], all >= 0 and finite."""
    if offset < 0.0:
        return True
    if height_id:
        top = float32(scale + offset)
        return not math.isfinite(top) or top < 0.0
    return False


def scale_repeats(nibble):
    """Texture repeats per chunk of MTXP texture scale nibble n: 8 / 2^n, one texture repeat per 2^n cells (PROVEN
    for the map generator for n = 0..4: whole numbers for n <= 3, the float 0.5 for n = 4 -- never the integer
    8 >> 4 = 0); 0 above MAX_SCALE_SHIFT (never observed: LayerEffectUnresolved, no repeats). Whole repeats stay
    ints, so every result of n <= 3 is unchanged."""
    if nibble > MAX_SCALE_SHIFT:
        return 0
    r = BASE_REPEATS / float(1 << nibble)
    return int(r) if r == int(r) else r


def chunk_phase(n, repeats):
    """The (u, v) phase, in repeats, chunk n adds to a layer's texture coordinate s = uv_cells * repeats / 8:
    (frac(ix * repeats), frac(iy * repeats)) with ix = n % 16 east, iy = n // 16 south -- the UV runs on across
    chunks. 0 for whole repeats (n <= 3: chunk-local and continuous UV agree), (0.5 * (ix % 2), 0.5 * (iy % 2)) at
    nibble 4. PROVEN for the map generator (map 2923), INFERRED for draw time; continuous over the tile and over the
    map cannot be told apart below a period of one tile (nibble 8, never observed)."""
    fu, fv = (n % 16) * repeats, (n // 16) * repeats
    return fu - math.floor(fu), fv - math.floor(fv)


def chunk_state(tex, tch, rch, mphd_flags, missing, height_missing=()):
    """(state, reason) of one chunk by the design's precedence table (v3 section 2). `missing`: ids whose texture
    failed, diffuse or height (a set, or a dict id -> state text); `height_missing`: ids failed for the height role
    only (a height texture without an alpha channel), likewise."""
    if tch['problem']:
        return 'Malformed', tch['problem']
    layers = tch['layers']
    if rch['problem']:
        return 'Malformed', 'root %s (no geometry is built)' % rch['problem']
    if tex['tile_problem'] and layers:
        return 'Malformed', tex['tile_problem']
    if tex['mtxp_problem'] and layers:
        return 'Malformed', tex['mtxp_problem']
    if tex['mtxp'] is not None:
        for k, lay in enumerate(layers):
            e = tex['mtxp'][lay['texture']]
            if not (math.isfinite(e[1]) and math.isfinite(e[2])):
                return 'Malformed', 'layer %d: MTXP height scale %r and offset %r are not finite numbers' % (k, e[1], e[2])
    maps = tch['alpha']
    sums = None
    for k in range(1, len(layers)):
        values = maps[k][1]
        sums = list(values) if sums is None else [a + b for a, b in zip(sums, values)]
    if sums is not None and max(sums) > 255:
        i = max(range(ALPHA_TEXELS), key=lambda t: sums[t])
        return 'Malformed', 'alpha sum %d > 255 at texel %d (row %d, col %d)' % (sums[i], i, i // 64, i % 64)
    eight = bool(mphd_flags & MPHD_8BIT)
    for k in range(1, len(layers)):
        if (maps[k][0] != 'raw4') != eight:
            return 'Malformed', 'MPHD 0x%x says %s alpha, layer %d is stored %s' % (
                mphd_flags, '8-bit' if eight else '4-bit', k, maps[k][0])
    if any(maps[k][0] == 'raw4' for k in range(1, len(layers))):
        return 'AlphaUnsupported', '4-bit alpha: its draw-time expansion is not established'
    if not rch['flags'] & MCNK_DO_NOT_FIX_ALPHA:
        return 'AlphaUnsupported', 'MCNK flag 0x8000 clear: the 63 x 63 alpha fix-up is not established'
    if len(layers) > MAX_DRAWN_LAYERS:
        return 'LayerCountUnsupported', '%d layers; at most %d are drawn (%d is the most observed)' % (
            len(layers), MAX_DRAWN_LAYERS, MAX_DRAWN_LAYERS)
    for k, lay in enumerate(layers):
        f = lay['flags']
        if f & CUBE_MAP_FLAG:
            return 'LayerEffectUnresolved', 'layer %d has MCLY flags 0x%x: 0x400 (cube map) is not established' % (k, f)
        if f & ~KNOWN_LAYER_FLAGS:
            return 'LayerEffectUnresolved', 'layer %d has MCLY flags 0x%x: bit(s) 0x%x never observed' % (
                k, f, f & ~KNOWN_LAYER_FLAGS)
        if tex['mtxp'] is not None:
            idx = lay['texture']
            e = tex['mtxp'][idx]
            if e[0] & ~KNOWN_MTXP_FLAGS:
                odd = e[0] & ~KNOWN_MTXP_FLAGS
                return 'LayerEffectUnresolved', 'layer %d: MTXP entry %d flags 0x%x: %s' % (
                    k, idx, e[0], '0x1 (cube-map texture) is not established' if odd == 0x1 else
                    'bit(s) 0x%x never observed' % odd)
            s = (e[0] >> 4) & 0xF
            if s > MAX_SCALE_SHIFT:
                return 'LayerEffectUnresolved', ('layer %d: MTXP texture scale nibble %d: never observed (0..%d verified)'
                                                 % (k, s, MAX_SCALE_SHIFT))
            if e[3]:
                return 'LayerEffectUnresolved', 'layer %d: MTXP entry %d word 3 = %d (0 in every audited entry)' % (
                    k, idx, e[3])
            hid = tex['mhid'][idx] if idx < len(tex['mhid']) else 0
            if mphd_flags & MPHD_HEIGHT and height_values_undefined(e[1], e[2], hid):
                return 'LayerEffectUnresolved', ('layer %d: MTXP height values (scale %r, offset %r) let H = h*s + o fall '
                                                 'below 0 or overflow float32; the height mix is not defined there'
                                                 % (k, e[1], e[2]))
    if mphd_flags & MPHD_HEIGHT:
        for k, lay in enumerate(layers):
            idx = lay['texture']
            hid = tex['mhid'][idx] if idx < len(tex['mhid']) else 0
            if hid and hid in missing:
                return 'HeightTextureMissing', 'layer %d: height texture %d %s' % (k, hid, _missing_text(missing, hid))
            if hid and hid in height_missing:
                return 'HeightTextureMissing', 'layer %d: height texture %d %s' % (
                    k, hid, _missing_text(height_missing, hid))
    if not layers:
        return 'NoLayers', 'no layers'
    for k, lay in enumerate(layers):
        if lay['flags'] & STATIC_EFFECT_FLAGS:
            return 'LayerEffectStatic', ('layer %d has MCLY flags 0x%x: drawn with the static composite (animation and '
                                         'overbright are not applied)' % (k, lay['flags']))
    for k, lay in enumerate(layers):
        fd = tex['mdid'][lay['texture']]
        if fd == 0:
            return 'MissingTexture', 'layer %d has no texture id (MDID 0)' % k
        if fd in missing:
            return 'MissingTexture', 'layer %d: texture %d %s' % (k, fd, _missing_text(missing, fd))
    return 'Textured', None


def build_material(root_buf, tex0_buf, mphd_flags, missing=(), height_missing=()):
    """The material of one tile: per chunk state, reason, layers, alpha maps, vertex colours and notes; tile counters
    and the unique texture ids of drawable chunks in first-use order (diffuse; height ids on MPHD 0x80 maps). Each
    layer carries its MHID entry and MTXP height values as stored (`heightTextureID`, `heightScale`, `heightOffset`);
    layer_height() says how they enter the mix. `missing`: texture ids that failed, diffuse or height (a set, or a
    dict id -> state text); `height_missing`: ids failed for the height role only (resolve_material: a height
    texture without an alpha channel). Each chunk's `checkerLayers` lists the layers drawn as the missing-texture
    checker (texture id 0 or failed): a MissingTexture chunk's, and a LayerEffectStatic chunk's whose texture failed
    (it keeps its state by the precedence; material_keep masks it all the same). With `missing`, the unique ids and
    references cover the chunks still drawable after the failures (the player reports the lists from before them)."""
    tex = read_tex0(tex0_buf)
    root = read_root(root_buf)
    heights = bool(mphd_flags & MPHD_HEIGHT)
    chunks = []
    counters = dict((COUNTER_NAMES[s], 0) for s in STATES)
    notes = {'mcsh': 0, 'mccvFlagMismatch': 0, 'mccvApplied': 0}
    unique, unique_h = [], []
    seen, seen_h = set(), set()
    references = 0
    zero_references = 0
    for n in range(256):
        tch, rch = tex['chunks'][n], root[n]
        state, reason = chunk_state(tex, tch, rch, mphd_flags, missing, height_missing)
        flags = rch['flags'] or 0
        has_flag, has_mccv = bool(flags & MCNK_HAS_MCCV), rch['mccv'] is not None
        mismatch = has_flag != has_mccv and not rch['problem']       # a malformed root chunk says nothing reliable
        layers = []
        if state != 'Malformed':
            for k, lay in enumerate(tch['layers']):
                idx = lay['texture']
                e = tex['mtxp'][idx] if tex['mtxp'] is not None else None
                shift = (e[0] >> 4) & 0xF if e is not None else 0
                layers.append({'fileDataID': tex['mdid'][idx], 'mdidIndex': idx, 'flags': lay['flags'],
                               'repeats': scale_repeats(shift), 'scaleNibble': shift,
                               'storage': tch['alpha'][k][0] if k else None,
                               'alpha': tch['alpha'][k][1] if k else None,
                               'heightTextureID': tex['mhid'][idx] if idx < len(tex['mhid']) else 0,
                               'heightScale': e[1] if e is not None else DEFAULT_HEIGHT[0],
                               'heightOffset': e[2] if e is not None else DEFAULT_HEIGHT[1]})
        checker = [k for k, lay in enumerate(layers) if lay['fileDataID'] == 0 or lay['fileDataID'] in missing]
        ch = {'n': n, 'ix': n % 16, 'iy': n // 16, 'state': state, 'reason': reason, 'layers': layers,
              'checkerLayers': checker if state in DRAWABLE else [], 'vertexColors': None, 'holes': rch['holes'],
              'geometry': rch['geometry'], 'mcsh': tch['mcsh'], 'mccvFlagMismatch': mismatch}
        if has_flag and has_mccv and state in VERTEX_COLOUR_STATES:
            m = rch['mccv']
            ch['vertexColors'] = [(m[4 * k + 2], m[4 * k + 1], m[4 * k]) for k in range(145)]
        chunks.append(ch)
        counters[COUNTER_NAMES[state]] += 1
        notes['mcsh'] += tch['mcsh']
        notes['mccvFlagMismatch'] += ch['mccvFlagMismatch']
        notes['mccvApplied'] += ch['vertexColors'] is not None
        if state in DRAWABLE:
            for lay in layers:
                references += 1
                fd = lay['fileDataID']
                if fd == 0:
                    zero_references += 1
                elif fd not in seen:
                    seen.add(fd)
                    unique.append(fd)
            if heights:
                for lay in layers:
                    hid = lay['heightTextureID']
                    if hid and hid not in seen_h:
                        seen_h.add(hid)
                        unique_h.append(hid)
    return {'mphdFlags': mphd_flags, 'chunks': chunks, 'counters': counters, 'notes': notes, 'uniqueTextures': unique,
            'uniqueHeightTextures': unique_h, 'layerReferences': references, 'zeroIdReferences': zero_references,
            'mdid': tex['mdid'], 'mhid': tex['mhid'], 'mtxp': tex['mtxp']}


def layer_height(mat, lay, mode='design'):
    """How layer `lay` enters the height mix: None for H = 1, else (height texture id, scale, offset) with
    H = h * scale + offset (h = 0 when the id is 0). mode 'design': the stored values when MPHD & 0x80 is set, H = 1
    when it is clear; 'default': H = 1 everywhere (the cutoff with default heights); 'full': the stored values
    whatever MPHD says (an alternative the 0x80-clear golden tile must fit worse)."""
    if mode not in HEIGHT_MODES:
        raise ValueError(mode)
    if mode == 'default' or (mode == 'design' and not mat['mphdFlags'] & MPHD_HEIGHT):
        return None
    hid, s, o = lay['heightTextureID'], lay['heightScale'], lay['heightOffset']
    if (hid == 0 or s == 0.0) and o == 1.0:
        return None                                     # h * 0 + 1 (or 0 * s + 1) is exactly 1
    return hid, s, o


def chunk_heights(mat, ch, mode='design'):
    """layer_height of every layer of `ch`, or None when every layer has H = 1."""
    hs = [layer_height(mat, lay, mode) for lay in ch['layers']]
    return hs if any(h is not None for h in hs) else None


def heights_bound(mat, mode='design'):
    """True when the height textures are bound (enter the mix) under layer_height mode `mode`."""
    return mode == 'full' or (mode == 'design' and bool(mat['mphdFlags'] & MPHD_HEIGHT))


def needed_textures(mat, ch, mode='design'):
    """[(fileDataID, role)] a drawn chunk binds: its diffuse ids and, where heights enter the mix (MPHD 0x80 under
    'design'), every non-zero height id (role 'height'), as the state rule asks for them all (HeightTextureMissing)."""
    out = [(lay['fileDataID'], 'diffuse') for lay in ch['layers'] if lay['fileDataID']]
    if heights_bound(mat, mode):
        out += [(lay['heightTextureID'], 'height') for lay in ch['layers'] if lay['heightTextureID']]
    return out


def bound_pairs(mat, ch, mode='design'):
    """[(fileDataID, repeats)] per layer: what each layer binds at its own repeats (its diffuse texture and, where
    heights enter the mix, its height texture), as the player binds them. One id may appear at several repeats."""
    out = [(lay['fileDataID'], lay['repeats']) for lay in ch['layers'] if lay['fileDataID']]
    if mat is not None and heights_bound(mat, mode):
        out += [(lay['heightTextureID'], lay['repeats']) for lay in ch['layers'] if lay['heightTextureID']]
    return out


def texture_roles(need):
    """{fileDataID: [role, ...]} of needed_textures' list, roles in first-use order: an id used as a diffuse and as a
    height texture has both."""
    roles = {}
    for fd, role in need:
        rs = roles.setdefault(fd, [])
        if role not in rs:
            rs.append(role)
    return roles


# ------------------------------------------------------------------------------------------------- sampling helpers

def _axis(positions, size, wrap):
    """Per position (i0, i1, f): bilinear neighbours on an axis of `size` texels, wrapped or clamped."""
    out = []
    for p in positions:
        f0 = math.floor(p)
        f = p - f0
        if wrap:
            out.append((int(f0) % size, (int(f0) + 1) % size, f))
        else:
            out.append((min(max(int(f0), 0), size - 1), min(max(int(f0) + 1, 0), size - 1), f))
    return out


def alpha_positions(px, placement=('centre', 0.0)):
    """Alpha texel coordinates (texel i centred at i) of the px pixel centres across a chunk: 'centre' with a shift in
    texels (the design's alignment alpha_texel_centre, shift 0), or 'i/63' (texel i at i/63 of the chunk)."""
    kind, shift = placement[0], placement[1] if len(placement) > 1 else 0.0
    out = []
    for x in range(px):
        u = (x + 0.5) / px
        out.append(u * (ALPHA_SIZE - 1) if kind == 'i/63' else u * ALPHA_SIZE - 0.5 + shift)
    return out


def resample_alpha(values, axis):
    """A 64 x 64 alpha map (0..255) at len(axis)^2 positions by bilinear interpolation (clamp): 0..1 floats."""
    rows = []
    for i0, i1, f in axis:
        r0, r1 = values[i0 * 64:i0 * 64 + 64], values[i1 * 64:i1 * 64 + 64]
        rows.append([a + (b - a) * f for a, b in zip(r0, r1)])
    out = []
    for row in rows:
        out.extend([(row[j0] + (row[j1] - row[j0]) * g) / 255.0 for j0, j1, g in axis])
    return out


def mccv_fan(colors, u, v):
    """Vertex colour (R, G, B bytes as floats) at chunk-local (u east, v south, 0..1), interpolated over the cell's
    four-triangle fan (outer corners and inner vertex), as a rasteriser interpolates the stage 03 mesh."""
    c = min(7, int(u * 8))
    r = min(7, int(v * 8))
    fu, fv = u * 8 - c, v * 8 - r
    nw, ne = colors[17 * r + c], colors[17 * r + c + 1]
    sw, se = colors[17 * (r + 1) + c], colors[17 * (r + 1) + c + 1]
    m = colors[17 * r + 9 + c]
    dn, ds, dw, de = fv, 1.0 - fv, fu, 1.0 - fu
    k = min((dn, 0), (de, 1), (ds, 2), (dw, 3))[1]
    if k == 0:
        wc, w1 = 2 * fv, fu - fv
        p, q = nw, ne
    elif k == 2:
        wc, w1 = 2 * (1 - fv), fu - (1 - fv)
        p, q = sw, se
    elif k == 3:
        wc, w1 = 2 * fu, fv - fu
        p, q = nw, sw
    else:
        wc, w1 = 2 * (1 - fu), fv - (1 - fu)
        p, q = ne, se
    w0 = 1.0 - wc - w1
    return tuple(w0 * p[i] + w1 * q[i] + wc * m[i] for i in range(3))


def mccv_image(colors, px):
    """Per pixel of a px x px chunk image: the MCCV multiplier (byte / 127) per channel, three flat lists."""
    out = ([], [], [])
    for y in range(px):
        v = (y + 0.5) / px
        for x in range(px):
            col = mccv_fan(colors, (x + 0.5) / px, v)
            for i in range(3):
                out[i].append(col[i] / MCCV_DIVISOR)
    return out


def srgb_to_linear(c):
    x = c / 255.0
    return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4


def linear_to_srgb(y):
    y = min(max(y, 0.0), 1.0)
    return 255.0 * (y * 12.92 if y <= 0.0031308 else 1.055 * y ** (1 / 2.4) - 0.055)


def weights(alphas, rule):
    """Per-pixel raw layer weights from the overlay alphas (lists of 0..1, one per layer after the first): 'weights'
    (w0 = 1 - sum, w_k = a_k; the height mix starts from these) or 'sequential' (each layer lerped over the result so
    far: an alternative that must fit worse)."""
    n = len(alphas[0]) if alphas else 0
    if rule == 'weights':
        if not alphas:
            return None
        total = [sum(t) for t in zip(*alphas)]
        return [[1.0 - s for s in total]] + alphas
    if rule == 'sequential':
        w = [[1.0] * n]
        for a in alphas:
            w = [[x * (1.0 - b) for x, b in zip(wi, a)] for wi in w] + [a]
        return w
    raise ValueError(rule)


def mix(w, heights=None, rule='heightmix'):
    """The composite weights per pixel from the raw weights `w` (per layer, lists).
    'heightmix' (the design): p_k = max(w_k, 0) * H_k with heights[k] None (H = 1), a number or a per-pixel list;
    m = max_k p_k; p_k *= 1 - clamp(m - p_k, 0, 1); p_k / S when S = sum_k p_k > 0, else max(w_k, 0).
    'plain': w as it is (plain weights). 'pow<e>': max(w_k, 0)^e normalised (a generic sharpening control)."""
    if rule == 'plain':
        return w
    n = len(w[0])
    cols = [[x if x > 0.0 else 0.0 for x in wk] for wk in w]
    out = [[0.0] * n for _ in w]
    L = len(w)
    if rule.startswith('pow'):
        e = float(rule[3:])
        for i, c in enumerate(zip(*cols)):
            p = [x ** e for x in c]
            s = sum(p)
            if s <= 0:
                p = c
                s = sum(c) or 1.0
            for k in range(L):
                out[k][i] = p[k] / s
        return out
    if rule != 'heightmix':
        raise ValueError(rule)
    ps = cols
    if heights is not None and any(h is not None for h in heights):
        ps = []
        for c, h in zip(cols, heights):
            if h is None:
                ps.append(c)
            elif isinstance(h, float):
                ps.append([x * h for x in c])
            else:
                ps.append([x * y for x, y in zip(c, h)])
    for i, p in enumerate(zip(*ps)):
        m = max(p)
        q = [x * (1.0 - min(max(m - x, 0.0), 1.0)) for x in p]
        s = sum(q)
        if s > 0:
            for k in range(L):
                out[k][i] = q[k] / s
        else:
            for k in range(L):
                out[k][i] = cols[k][i]
    return out


# The composite rules map_composite and validation_rows know: name -> (raw weights, mix rule). 'heightmix' is the
# design; the others are alternatives the golden tests show fit worse ('plain weights' is also the labelled
# diagnostic view's rule).
RULES = {'heightmix': ('weights', 'heightmix'), 'plain weights': ('weights', 'plain'),
         'sequential': ('sequential', 'plain'), 'sequential heightmix': ('sequential', 'heightmix'),
         'pow1.25': ('weights', 'pow1.25'), 'pow1.5': ('weights', 'pow1.5')}


def composite_weights(alphas, n, rule='heightmix', heights=None):
    """Per-layer composite weights (lists of n) of a chunk from its overlay alphas (one list per layer after the
    first) under `rule` (a RULES name), heights as for mix(). A single layer has weight 1 under every rule."""
    if not alphas:
        return [[1.0] * n]
    raw, how = RULES[rule]
    return mix(weights(alphas, raw), heights, how)


def check_height_coordinates(mat, ch, heights, rule, repeats):
    """Raise ValueError when a composite of chunk `ch` would read a height texture that has no defined coordinate:
    `rule` mixes the heights (a RULES name whose mix is the height mix), the heights are bound (heights_bound under
    layer_height mode `heights`), the layer's MHID entry is not 0 and repeats[k] -- the repeats its height texture is
    read at -- are unresolved (<= 0: an MTXP scale nibble above 4). C# TerrainCompositor.HeightAt throws there too;
    texel 0 is never read in its place. Nothing to check without `mat` (every H is 1)."""
    if mat is None or RULES[rule][1] != 'heightmix' or not heights_bound(mat, heights):
        return
    for k, (lay, r) in enumerate(zip(ch['layers'], repeats)):
        if lay['heightTextureID'] and r <= 0:
            raise ValueError('layer %d has unresolved repeats (MTXP texture scale nibble %d): its height texture %d has '
                             'no defined coordinate' % (k, lay['scaleNibble'], lay['heightTextureID']))


def checker(px):
    """The missing-texture checker of a px x px chunk image (one square per 1/8 cell, magenta where row + col even)."""
    out = ([], [], [])
    for y in range(px):
        sy = int((y + 0.5) * 64 / px)
        for x in range(px):
            col = CHECKER_ON if (sy + int((x + 0.5) * 64 / px)) % 2 == 0 else CHECKER_OFF
            for i in range(3):
                out[i].append(col[i])
    return out


# ------------------------------------------------------------------------------------------------- map-texture mode

def texture_level(blp, repeats, px=MAP_PIXELS_PER_CHUNK):
    """(level, exact, note) for sampling `blp` at `repeats` per chunk on px pixels per chunk: LOD log2(texels per map
    pixel) of the width, clamped to the stored levels; exact when that level has one texel per pixel on BOTH axes.
    Whenever it is not exact (a non-square texture, magnification, a footprint that is not a power of two, a level
    that is not stored) the note says so: the level is then sampled bilinearly, never silently. The width axis is
    kept (which axis the baker would use is not established; the note marks every such case)."""
    ratio = blp.width * repeats / float(px)
    lod = math.log2(ratio) if ratio > 0 else 0.0
    lv = int(round(lod)) if lod > 0 else 0
    notes = []
    if abs(lod - round(lod)) > 1e-9 and lod > 0:
        notes.append('footprint of %.3f texels per pixel is not a power of two: level %d used' % (ratio, lv))
    if lv >= len(blp.levels):
        notes.append('level %d is not stored (%d levels): level %d used' % (lv, len(blp.levels), len(blp.levels) - 1))
        lv = len(blp.levels) - 1
    w, h = blp.level_size(lv)
    exact = w * repeats == px and h * repeats == px
    if not exact:
        notes.append('no stored level of one texel per pixel (%dx%d at %g repeats on %d px): level %d (%dx%d) sampled '
                     'bilinearly' % (blp.width, blp.height, repeats, px, lv, w, h))
    return lv, exact, '; '.join(notes) if notes else None


def layer_image(blp, repeats, px=MAP_PIXELS_PER_CHUNK, sym=0, phase=(0, 0), linear=False, chans=(0, 1, 2),
                cphase=(0, 0)):
    """One layer's texture over a chunk of px x px pixels (one flat list per channel in `chans`, 0..255, chunk-local),
    from the stored level texture_level picks, sampled at pixel centres (bilinear with wrap; one texel per pixel lands
    on texel centres, so it is the texel itself). `cphase` is the chunk's phase in repeats (chunk_phase: 0 for whole
    repeats, so then the image is the same for every chunk). `sym` transforms the level image (square textures only);
    `phase` shifts texel indices by (columns, rows); `linear` linearises the colour channels. A height texture is read
    with chans=(3,): its alpha."""
    lv, _, note = texture_level(blp, repeats, px)
    w, h, rgba = blp.decode(lv)
    if sym:
        if w != h:
            raise ValueError('texture symmetries need a square texture (%dx%d)' % (w, h))
        rgba = transform_grid(rgba, w, sym, 4)
    srcs = [rgba[c::4] for c in chans]
    if linear:
        srcs = [[srgb_to_linear(v) for v in ch] if c < 3 else ch for ch, c in zip(srcs, chans)]
    cols = _axis([(x + 0.5) * w * repeats / px - 0.5 + phase[0] + cphase[0] * w for x in range(px)], w, True)
    rows = _axis([(y + 0.5) * h * repeats / px - 0.5 + phase[1] + cphase[1] * h for y in range(px)], h, True)
    out = tuple([] for _ in chans)
    for i0, i1, f in rows:
        for ch, src in zip(out, srcs):
            r0, r1 = src[i0 * w:(i0 + 1) * w], src[i1 * w:(i1 + 1) * w]
            row = [a + (b - a) * f for a, b in zip(r0, r1)] if f else r0
            ch.extend([row[j0] + (row[j1] - row[j0]) * g if g else row[j0] for j0, j1, g in cols])
    return out, note


def height_values(hp, texture, repeats, px, sym=0, phase=(0, 0), images=None, chan=3, cphase=(0, 0)):
    """H over a px x px chunk for one layer's height parameters hp = (height id, scale, offset) (layer_height):
    a constant (a float) when the id is 0 or the scale is 0, else h * scale + offset per pixel with h the alpha
    (channel `chan`: 3, other values only for the golden alternatives) of the height texture's own stored mip, / 255
    (map-texture sampling, like the layer, at `repeats` and the chunk phase `cphase`: the layer's own texture
    coordinate unless an alternative overrides them).
    A height texture that is not available, or whose BLP has no alpha channel (alpha depth 0), gives h = 0 with a
    note (its chunks are HeightTextureMissing in a material built by resolve_material). (H, note)."""
    hid, s, o = hp
    if hid == 0 or s == 0.0:
        return 0.0 * s + o, None
    key = ('h', hid, s, o, repeats, px, sym, phase, chan, cphase)
    if images is not None and key in images:
        return images[key]
    blp = texture(hid)
    if blp is None:
        res = (0.0 * s + o, 'height texture %d is not available: h = 0 used' % hid)
    elif blp.alpha_depth == 0:
        res = (0.0 * s + o, 'height texture %d: %s: h = 0 used' % (hid, HEIGHT_NO_ALPHA))
    else:
        (a,), note = layer_image(blp, repeats, px, sym, phase, chans=(chan,), cphase=cphase)
        res = ([v / 255.0 * s + o for v in a], ('height texture %d: %s' % (hid, note)) if note else None)
    if images is not None:
        images[key] = res
    return res


def layer_uv(lay, n, nibble4=None):
    """(repeats, chunk phase) of layer `lay` in chunk n: the layer's own repeats and the continuous-UV phase
    (chunk_phase). `nibble4` = (repeats, continuous) replaces both for a layer whose MTXP scale nibble is 4 (the golden
    alternatives: chunk-local UV is continuous False, phase 0)."""
    r, continuous = lay['repeats'], True
    if nibble4 is not None and lay.get('scaleNibble') == 4:
        r, continuous = nibble4
    return r, (chunk_phase(n, r) if continuous else (0, 0))


def map_composite(mat, texture, rule='heightmix', heights='design', linear=False, mccv=True, alpha_sym=0, tex_sym=0,
                  phase=(0, 0), placement=('centre', 0.0), clip=True, px=MAP_PIXELS_PER_CHUNK, states=DRAWABLE,
                  cache=None, height_repeats=None, height_chan=3, nibble4=None):
    """The tile composite in map-texture mode: (R, G, B) flat lists of (16 px)^2, north row first, 0..255 (clipped
    unless clip=False), plus info {'notes', 'clipped', 'composited'}. `texture(fileDataID)` gives a Blp or None, for
    diffuse and height ids alike. `rule` is a RULES name, `heights` a layer_height mode. Chunks in `states` are
    composited (the drawable ones by default); other chunks stay 0. A missing layer is the checker, and so is a layer
    of unresolved repeats (0: only in a non-drawable state); a height texture the height mix would read at unresolved
    repeats raises ValueError (check_height_coordinates). Each layer (and
    its height texture) is sampled at its texture coordinate (layer_uv: its repeats plus the chunk phase). The texture
    symmetry and phase alternatives move the height textures with the layer's UV; `height_repeats` (every height
    texture at these repeats, and their chunk phase, instead of its layer's) and `height_chan` (a channel other than
    the alpha) are the golden test's height-sampling alternatives, `nibble4` (layer_uv) its nibble-4 UV alternatives.
    `cache` (a dict kept between calls on one material and texture source) holds the layer images and the design's
    composite weights."""
    size = 16 * px
    out = ([0.0] * (size * size), [0.0] * (size * size), [0.0] * (size * size))
    axis = _axis(alpha_positions(px, placement), ALPHA_SIZE, False)
    images = cache.setdefault('images', {}) if cache is not None else {}
    wcache = cache.setdefault('weights', {}) if cache is not None else None
    notes = []
    composited = []
    clipped = 0
    for ch in mat['chunks']:
        if ch['state'] not in states:
            continue
        layers = ch['layers']
        uvs = [layer_uv(lay, ch['n'], nibble4) for lay in layers]
        hreps = [r if height_repeats is None else height_repeats for r, _ in uvs]
        check_height_coordinates(mat, ch, heights, rule, hreps)
        imgs = []
        for lay, (r, cph) in zip(layers, uvs):
            fd = lay['fileDataID']
            key = (fd, r, px, tex_sym, phase, linear, cph)
            if key not in images:
                blp = texture(fd) if fd and r > 0 else None       # unresolved repeats: the checker, never bound
                if blp is None:
                    images[key] = checker(px)
                else:
                    images[key], note = layer_image(blp, r, px, tex_sym, phase, linear, cphase=cph)
                    if note:
                        notes.append('texture %d: %s' % (fd, note))
            imgs.append(images[key])
        hs = chunk_heights(mat, ch, heights)
        hvals = None
        if hs is not None:
            hvals = []
            for hp, (r, cph) in zip(hs, uvs):
                if hp is None:
                    hvals.append(None)
                    continue
                if height_repeats is not None:
                    r, cph = height_repeats, chunk_phase(ch['n'], height_repeats)
                if r <= 0 and hp[0]:
                    hvals.append(None)          # unresolved repeats under a rule without the height mix: never read
                    continue
                v, note = height_values(hp, texture, r, px, tex_sym, phase, images, height_chan, cph)
                if note:
                    notes.append(note)
                hvals.append(v)
        sampled = hvals is not None and any(isinstance(v, list) for v in hvals)
        wkey = (ch['n'], px) + ((tex_sym, phase) if sampled else ())
        reuse = wcache is not None and (rule, heights, alpha_sym, placement, height_repeats, height_chan, nibble4) == (
            'heightmix', 'design', 0, ('centre', 0.0), None, 3, None)
        if reuse and wkey in wcache:
            w = wcache[wkey]
        else:
            alphas = [resample_alpha(transform_grid(lay['alpha'], ALPHA_SIZE, alpha_sym), axis) for lay in layers[1:]]
            w = composite_weights(alphas, px * px, rule, hvals)
            if reuse:
                wcache[wkey] = w
        vc = None
        if mccv and ch['vertexColors'] is not None:
            vcache = ch.setdefault('mccvImages', {})       # per chunk and size: the same in every variant
            if px not in vcache:
                vcache[px] = mccv_image(ch['vertexColors'], px)
            vc = vcache[px]
        acc = []
        for c in range(3):
            v = [0.0] * (px * px)
            for wk, img in zip(w, imgs):
                v = [a + b * t for a, b, t in zip(v, wk, img[c])]
            if linear:
                v = [linear_to_srgb(a) for a in v]
            if vc is not None:
                v = [a * m for a, m in zip(v, vc[c])]
            acc.append(v)
        composited.append(ch['n'])
        x0, y0 = ch['ix'] * px, ch['iy'] * px
        clipped += sum(1 for r, g, b in zip(*acc) if not (0.0 <= r <= 255.0 and 0.0 <= g <= 255.0 and 0.0 <= b <= 255.0))
        for y in range(px):
            d = (y0 + y) * size + x0
            for c in range(3):
                row = acc[c][y * px:(y + 1) * px]
                out[c][d:d + px] = [0.0 if a < 0.0 else 255.0 if a > 255.0 else a for a in row] if clip else row
    return out, {'notes': notes, 'clipped': clipped, 'composited': composited}


# ------------------------------------------------------------------------------------------------- validation (V1)

def validation_size(ch, texel_size, mat=None, heights='design'):
    """N for rendering chunk `ch` alone: max over every texture each layer binds at that layer's repeats (its diffuse
    texture and, with `mat` given and heights in the mix, its height texture: bound_pairs) of (width or height x
    repeats; repeats may be the float 0.5: 2048 x 0.5 = 1024), a whole power of two <= 8192; None (with the reason)
    when that is not possible. texel_size(fileDataID) -> (width, height) or None."""
    n = 0
    for fd, repeats in bound_pairs(mat, ch, heights):
        s = texel_size(fd) if fd else None
        if s is None:
            continue
        n = max(n, s[0] * repeats, s[1] * repeats)
    if n == 0:
        n = ALPHA_SIZE
    if n != int(n) or n > 8192 or int(n) & (int(n) - 1):
        return None, 'validation grid on texel boundary (%s texels per chunk)' % (int(n) if n == int(n) else n)
    return int(n), None


def validation_rows(ch, texels, size, placement=('centre', 0.0), mat=None, heights='design', rule='heightmix'):
    """Yield (y, R, G, B) rows (0..255 floats, west to east) of chunk `ch` rendered alone at size x size in validation
    sampling, north row first. texels(fileDataID) -> (width, height, flat RGBA of mip 0[, scale to 0..255]), or None
    (the checker; a height texture that is not given reads h = 0). Layer k's texel (and its height texture's) at
    chunk-local (u, v): (floor((u * repeats + phase_u) * W) mod W, floor((v * repeats + phase_v) * H) mod H) with the
    chunk phase (chunk_phase of ch['n']: 0 for whole repeats). A layer of unresolved repeats (0: an MTXP scale nibble
    above 4, only in a non-drawable state) is the checker, as C# TerrainCompositor.LayerTexel draws it, and a height
    texture the height mix would read on it raises ValueError (check_height_coordinates, as C# HeightAt). The heights
    (layer_height mode `heights`) need `mat` (the tile material); without it every H is 1. Hole cells are not special
    here (the caller masks them)."""
    layers = ch['layers']
    us = [(x + 0.5) / size for x in range(size)]
    acols = _axis([u * ALPHA_SIZE - 0.5 + placement[1] if placement[0] == 'centre' else u * (ALPHA_SIZE - 1) for u in us],
                  ALPHA_SIZE, False)

    def sampler(fd, r):
        t = texels(fd) if fd else None
        if t is None:
            return None
        w, h, rgba = t[:3]
        pu, pv = chunk_phase(ch['n'], r)
        return (w, h, channels(rgba) + (rgba[3::4],), [int(math.floor((u * r + pu) * w)) % w for u in us], r,
                t[3] if len(t) > 3 else 1.0, pv)

    check_height_coordinates(mat, ch, heights, rule, [lay['repeats'] for lay in layers])
    tex = [sampler(lay['fileDataID'], lay['repeats']) if lay['repeats'] > 0 else None for lay in layers]
    hs = chunk_heights(mat, ch, heights) if mat is not None else None
    htex = []
    for k, lay in enumerate(layers):
        hp = hs[k] if hs else None
        if hp is None:
            htex.append(None)
        elif hp[0] == 0 or hp[1] == 0.0:
            htex.append(0.0 * hp[1] + hp[2])
        elif lay['repeats'] <= 0:
            htex.append(None)       # unresolved repeats under a rule without the height mix: never read
        else:
            htex.append((hp, sampler(hp[0], lay['repeats'])))
    checker_cols = [int(u * 64) for u in us]
    vc = ch['vertexColors']
    for y in range(size):
        v = (y + 0.5) / size
        t = v * ALPHA_SIZE - 0.5 + placement[1] if placement[0] == 'centre' else v * (ALPHA_SIZE - 1)
        (i0, i1, f), = _axis([t], ALPHA_SIZE, False)
        alphas = []
        for lay in layers[1:]:
            a = lay['alpha']
            row = [p + (q - p) * f for p, q in zip(a[i0 * 64:i0 * 64 + 64], a[i1 * 64:i1 * 64 + 64])]
            alphas.append([(row[j0] + (row[j1] - row[j0]) * g) / 255.0 for j0, j1, g in acols])
        hrow = None
        if hs is not None:
            hrow = []
            for ht in htex:
                if ht is None or isinstance(ht, float):
                    hrow.append(ht)
                    continue
                (hid, s, o), smp = ht
                if smp is None:
                    hrow.append(0.0 * s + o)
                    continue
                tw, th, chans, txs, r, scale, pv = smp
                ty = int(math.floor((v * r + pv) * th)) % th
                src = chans[3]
                hrow.append([src[ty * tw + i] * scale / 255.0 * s + o for i in txs])
        w = composite_weights(alphas, size, rule, hrow)
        sq = int(v * 64)
        acc = [[0.0] * size for _ in range(3)]
        for wk, tx in zip(w, tex):
            if tx is None:
                img = [[(CHECKER_ON if (sq + cc) % 2 == 0 else CHECKER_OFF)[c] for cc in checker_cols] for c in range(3)]
            else:
                tw, th, chans, txs, r, scale, pv = tx
                ty = int(math.floor((v * r + pv) * th)) % th
                img = [[src[ty * tw + i] * scale for i in txs] for src in chans[:3]]
            for c in range(3):
                acc[c] = [a + b * p for a, b, p in zip(acc[c], wk, img[c])]
        if vc is not None:
            mult = [mccv_fan(vc, u, v) for u in us]
            for c in range(3):
                acc[c] = [a * m[c] / MCCV_DIVISOR for a, m in zip(acc[c], mult)]
        yield y, acc[0], acc[1], acc[2]


def hole_cell(ch, u, v):
    """True when chunk-local (u, v) lies in a hole cell of the chunk's 64-bit mask (bit row * 8 + col)."""
    return bool((ch['holes'] >> (min(7, int(v * 8)) * 8 + min(7, int(u * 8)))) & 1)


# ------------------------------------------------------------------------------------------------- metrics

def block_means(img, size, block=4):
    """Per block (row-major, (size / block)^2) mean of one channel."""
    nb = size // block
    out = [0.0] * (nb * nb)
    inv = 1.0 / (block * block)
    for y in range(size):
        row = img[y * size:(y + 1) * size]
        base = (y // block) * nb
        for bx in range(nb):
            out[base + bx] += sum(row[bx * block:(bx + 1) * block])
    return [v * inv for v in out]


def block_rmse(pred, ref, keep, size=MAP_SIZE, block=4, ref_means=None):
    """(RMSE over kept blocks and the three channels, per-channel bias pred - ref, kept blocks). keep: per block."""
    sse = 0.0
    bias = [0.0, 0.0, 0.0]
    n = sum(1 for k in keep if k)
    for c in range(3):
        pm = block_means(pred[c], size, block)
        rm = ref_means[c] if ref_means else block_means(ref[c], size, block)
        for k, a, b in zip(keep, pm, rm):
            if k:
                d = a - b
                sse += d * d
                bias[c] += d
    if n == 0:
        return float('nan'), (0.0, 0.0, 0.0), 0
    return math.sqrt(sse / (3 * n)), tuple(b / n for b in bias), n


def pixel_rmse(pred, ref, keep_pixels):
    sse = 0.0
    n = 0
    for c in range(3):
        for k, a, b in zip(keep_pixels, pred[c], ref[c]):
            if k:
                d = a - b
                sse += d * d
                n += 1
    return math.sqrt(sse / n) if n else float('nan')


def pixel_keep(keep_blocks, size=MAP_SIZE, block=4):
    nb = size // block
    return [keep_blocks[(y // block) * nb + x // block] for y in range(size) for x in range(size)]


def material_keep(mat, size=MAP_SIZE, block=4, states=GOLDEN_STATES, holes=True, chunks=None):
    """Per block of the map texture (block x block px): kept when its chunk's state is in `states` (and, with
    `chunks` given, its index is in it), the chunk draws no checker layer (texture id 0 or failed, whatever its
    state: a LayerEffectStatic chunk keeps its state when a texture fails) and (holes=True) its cell is not a hole.
    The golden tests and V2 keep the chunks drawn with the composite and every texture (Textured, LayerEffectStatic
    without a checker layer): neutral, missing-texture and checker-drawing chunks are masked."""
    nb = size // block
    per_chunk = nb // 16
    keep = [False] * (nb * nb)
    for ch in mat['chunks']:
        if ch['state'] not in states or (chunks is not None and ch['n'] not in chunks) or ch.get('checkerLayers'):
            continue
        for by in range(per_chunk):
            for bx in range(per_chunk):
                if holes and ch['holes']:
                    cell = (by * 8 // per_chunk) * 8 + bx * 8 // per_chunk
                    if (ch['holes'] >> cell) & 1:
                        continue
                keep[(ch['iy'] * per_chunk + by) * nb + ch['ix'] * per_chunk + bx] = True
    return keep


def transform_image(img, size, sym):
    return tuple(transform_grid(c, size, sym) for c in img)


def shift_image(img, size, dx, dy):
    """Image shifted by (dx, dy) pixels (east, south); pixels shifted in from outside repeat the edge."""
    out = []
    for c in img:
        o = [0.0] * (size * size)
        for y in range(size):
            sy = min(max(y - dy, 0), size - 1)
            row = c[sy * size:(sy + 1) * size]
            o[y * size:(y + 1) * size] = [row[min(max(x - dx, 0), size - 1)] for x in range(size)]
        out.append(o)
    return tuple(out)


# ------------------------------------------------------------------------------------------------- audit data

def load_hashes():
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tests', 'corpus_hashes.json')
    with open(p) as f:
        return json.load(f)


class AuditData:
    """Files of the extracted audit data by client and FileDataID: the pinned corpus (corpus/<client>/azeroth, wdt/
    <client>), then materials/<client>/stage04_reference and stage04_golden (this stage's extractions), then every
    other materials/<client>/* folder. verify(): the file's SHA-256 against the pinned corpus hashes or its folder's
    manifest_sha256.tsv (whose build must be the pinned client build; its id column is named "id" or "fdid"); a file
    with neither is unverified. read() returns the first copy in that order that verifies (a folder may hold a truncated
    or unlisted copy of a file another folder holds verified) and names every copy it tried when none does."""

    PREFERRED = ('stage04_reference', 'stage04_golden')

    def __init__(self, root):
        self.root = root
        self.hashes = load_hashes()
        self._manifests = {}
        self._sets = {}

    def folders(self, client):
        out = [os.path.join(self.root, 'corpus', client, 'azeroth'), os.path.join(self.root, 'wdt', client)]
        out += [os.path.join(self.root, 'materials', client, d) for d in self.PREFERRED]
        mat = os.path.join(self.root, 'materials', client)
        if os.path.isdir(mat):
            out += [os.path.join(mat, d) for d in sorted(os.listdir(mat))
                    if d not in self.PREFERRED and os.path.isdir(os.path.join(mat, d))]
        return out

    def copies(self, client, fdid):
        """Every copy of the file in the audit data, in folder order."""
        return [p for p in (os.path.join(d, '%d.bin' % fdid) for d in self.folders(client)) if os.path.exists(p)]

    def find(self, client, fdid):
        found = self.copies(client, fdid)
        return found[0] if found else None

    def manifest(self, folder):
        if folder not in self._manifests:
            rows = {}
            p = os.path.join(folder, 'manifest_sha256.tsv')
            if os.path.exists(p):
                with open(p) as f:
                    head = f.readline().rstrip('\n').split('\t')
                    key = 'id' if 'id' in head else 'fdid'
                    for line in f:
                        r = dict(zip(head, line.rstrip('\n').split('\t')))
                        if key in r:
                            rows[r[key]] = r
            self._manifests[folder] = rows
        return self._manifests[folder]

    def set_digest(self, rel_folder):
        """The pinned set digest's recomputation (pin_corpus.py: SHA-256 over the sorted '<name> <sha256>' lines)."""
        if rel_folder not in self._sets:
            folder = os.path.join(self.root, rel_folder)
            names = sorted(n for n in os.listdir(folder) if n.endswith('.bin'))
            lines = ''.join('%s %s\n' % (n, hashlib.sha256(read_bytes(os.path.join(folder, n))).hexdigest()) for n in names)
            self._sets[rel_folder] = (len(names), hashlib.sha256(lines.encode('ascii')).hexdigest())
        return self._sets[rel_folder]

    def verify(self, client, path):
        """(ok, how): the file's SHA-256 against its pin, its pinned folder set or its folder's manifest row."""
        data = read_bytes(path)
        digest = hashlib.sha256(data).hexdigest()
        rel = os.path.relpath(path, self.root).replace(os.sep, '/')
        pinned = self.hashes['files'].get(rel)
        if pinned:
            return pinned == digest, 'corpus pin %s' % rel
        folder = rel.rsplit('/', 1)[0]
        pin_set = self.hashes['sets'].get(folder)
        if pin_set:
            n, d = self.set_digest(folder)
            return (n, d) == (pin_set['files'], pin_set['digest']), 'pinned set %s (%d files)' % (folder, n)
        row = self.manifest(os.path.dirname(path)).get(os.path.basename(path)[:-4])
        if row:
            build = self.hashes['client_builds'].get(client)
            ok = row.get('sha256') == digest and row.get('build') == build and row.get('status', 'OK') == 'OK'
            return ok, 'manifest %s (build %s, pinned %s)' % (rel, row.get('build'), build)
        return False, 'no pin or manifest row for %s' % rel

    def read_how(self, client, fdid, verify=True):
        """(bytes, how): the first copy that verifies and how it was verified ("unverified" with verify False)."""
        found = self.copies(client, fdid)
        if not found:
            raise FileNotFoundError('%s %d is not in the audit data' % (client, fdid))
        if not verify:
            return read_bytes(found[0]), 'unverified'
        tried = []
        for p in found:
            ok, how = self.verify(client, p)
            if ok:
                return read_bytes(p), how
            tried.append(how)
        raise ValueError('%s %d failed verification (%s)' % (client, fdid, '; '.join(tried)))

    def read(self, client, fdid, verify=True):
        return self.read_how(client, fdid, verify)[0]

    def texture_source(self, client, verify=True, missing=None, model='exact'):
        """texture(fileDataID) -> Blp (decoded by `model`) or None; ids not in the audit data go into `missing`."""
        cache = {}

        def get(fdid):
            if fdid not in cache:
                try:
                    cache[fdid] = Blp(self.read(client, fdid, verify), '%s texture %d' % (client, fdid), model)
                except FileNotFoundError:
                    if missing is not None:
                        missing[fdid] = 'not in the audit data'
                    cache[fdid] = None
            return cache[fdid]
        return get


# Golden tiles (design v3 section 4): label, client, WDT (relative to the audit data), tile, root, tex0, map texture
# (MAID words 0, 3, 5), and the design composite's measured 4 x 4-block RMSE against the map texture (own stored
# mips, the golden mask); threshold = that + GOLDEN_ALLOWANCE. 30_49 has height-textured chunks (MPHD 0x80: the full
# formula), 34_46 lava chunks (LayerEffectStatic), WDT 1923857 is an MPHD 0x80-clear map whose every chunk names
# height textures and MTXP height values that must not enter the mix, and Retail map 2923 32_24 (WDT 6838755) has
# layers at MTXP scale nibble 4 (0.5 repeats, tile-continuous UV) and chunks of 6..8 layers. Files beyond the pinned
# corpus are in materials/<client>/stage04_reference, stage04_golden and stage04_nibble4 (manifest_sha256.tsv with
# the build).
GOLDEN = [
    ('Retail 32_50', 'wow', 'wdt/wow/775971.bin', 32, 50, 778037, 778040, 1293881, 1.110),
    ('Retail 29_50', 'wow', 'wdt/wow/775971.bin', 29, 50, 777437, 777440, 1293857, 1.069),
    ('Retail 32_48', 'wow', 'wdt/wow/775971.bin', 32, 48, 778027, 778030, 1293745, 1.096),
    ('Retail 34_46', 'wow', 'wdt/wow/775971.bin', 34, 46, 778417, 778420, 1293753, 1.079),
    ('Retail 30_49', 'wow', 'wdt/wow/775971.bin', 30, 49, 777632, 777635, 1293863, 1.158),
    ('Beta 32_48', 'wow_classic_beta', 'wdt/wow_classic_beta/775971.bin', 32, 48, 778027, 778030, 1293745, 1.145),
    ('Era map 2853 34_50', 'wow_classic_era', 'wdt/wow_classic_era/6357503.bin', 34, 50, 6358761, 6358764, 6359006, 1.107),
    ('Retail WDT 1923857 27_19', 'wow', 'wdt/wow/1923857.bin', 27, 19, 1923654, 1923657, 1922956, 0.978),
    ('Retail map 2923 32_24', 'wow', 'wdt/wow/6838755.bin', 32, 24, 7473884, 7473887, 7473747, 1.057),
]

# The offline converter's 4-bit -> 8-bit table (n -> byte; 16n + 15 for n >= 4, one lower for 1..3).
CONVERTER_TABLE = [0, 30, 46, 62] + [16 * n + 15 for n in range(4, 16)]
# (label, client of the 8-bit copy, tex0, minimum share within +-1)
CONVERTER_CASES = [('32_50', 'wow', 778040, 0.995), ('Beta 32_48', 'wow_classic_beta', 778030, 0.990)]


def tile_ids(data_root, wdt_rel, x, y):
    """(MPHD flags, MAID words) of a tile, from the WDT."""
    w = WDT(read_bytes(os.path.join(data_root, wdt_rel)))
    if w.maid is None or not w.mphd_words:
        raise ValueError('%s has no MPHD/MAID' % wdt_rel)
    return w.mphd_words[0], w.maid[y * 64 + x]


def resolve_material(root_buf, tex0_buf, mphd, textures, missing=None, height_missing=None):
    """The tile material with every texture its drawable chunks need looked up through `textures` (fileDataID -> Blp
    or None; a source that records the ids it cannot give in `missing`, a dict): the material is rebuilt with those
    failed, and with every height texture whose BLP has no alpha channel failed for the height role only
    (`height_missing`, HEIGHT_NO_ALPHA: its chunks are HeightTextureMissing, the same id stays a usable diffuse
    texture)."""
    missing = {} if missing is None else missing
    height_missing = {} if height_missing is None else height_missing
    mat = build_material(root_buf, tex0_buf, mphd)
    for fd in mat['uniqueTextures'] + mat['uniqueHeightTextures']:
        textures(fd)
    for fd in mat['uniqueHeightTextures']:
        b = textures(fd)
        if b is not None and b.alpha_depth == 0:
            height_missing[fd] = HEIGHT_NO_ALPHA
    if missing or height_missing:
        mat = build_material(root_buf, tex0_buf, mphd, missing, height_missing)
    return mat


def tile_material(data, client, mphd, rid, tid, verify=True, missing=None, height_missing=None):
    """(material, texture source) of a tile from the audit data (resolve_material): every texture the drawable chunks
    need (diffuse, and height ids on MPHD 0x80 maps) is looked up; ids not in the audit data are `missing`, height
    textures without an alpha channel `height_missing`, and the material is rebuilt with them failed."""
    missing = {} if missing is None else missing
    textures = data.texture_source(client, verify, missing)
    root, tex0 = data.read(client, rid, verify), data.read(client, tid, verify)
    return resolve_material(root, tex0, mphd, textures, missing, height_missing), textures


def load_case(data, case, verify=True):
    """Material, map texture and texture source of a golden tile (ids checked against the WDT, every file against
    its pin or manifest row)."""
    label, client, wdt_rel, x, y, rid, tid, mid, _ = case
    mphd, maid = tile_ids(data.root, wdt_rel, x, y)
    if (maid[0], maid[3], maid[5]) != (rid, tid, mid):
        raise ValueError('%s: MAID words 0/3/5 are %d/%d/%d, pinned %d/%d/%d' % (label, maid[0], maid[3], maid[5], rid, tid, mid))
    if verify:
        ok, how = data.verify(client, os.path.join(data.root, wdt_rel))
        if not ok:
            raise ValueError('%s: WDT failed verification (%s)' % (label, how))
    missing, height_missing = {}, {}
    mat, textures = tile_material(data, client, mphd, rid, tid, verify, missing, height_missing)
    mw, mh, mpx = Blp(data.read(client, mid, verify), '%s map texture' % label).decode(0)
    if (mw, mh) != (MAP_SIZE, MAP_SIZE):
        raise ValueError('%s: map texture is %dx%d' % (label, mw, mh))
    return {'label': label, 'client': client, 'mphd': mphd, 'mat': mat, 'map': channels(mpx), 'textures': textures,
            'missing': missing, 'height_missing': height_missing}


def height_chunks(mat):
    """Indices of the chunks with stored height data: a layer with a height texture id or MTXP height values other
    than the defaults (what MPHD 0x80 lets into the mix)."""
    return set(ch['n'] for ch in mat['chunks'] if any(
        lay['heightTextureID'] or (lay['heightScale'], lay['heightOffset']) != DEFAULT_HEIGHT for lay in ch['layers']))


# Every alternative golden_table computes beside the design: name -> map_composite arguments. 'default heights' and
# 'full formula' are only computed where they differ from the design, and the height-sampling alternatives (the
# height texture at 8 or 1 repeats instead of its layer's, its red channel instead of the alpha) only where a drawn
# chunk samples a height texture (otherwise they are the design itself). The height texture's LOD has no
# alternative: one mip finer fits within 0.01 on every tile, so it is INFERRED, not gated. The nibble-4 alternatives
# (a nibble-4 layer and its height texture at 0.5 repeats with chunk-local UV, at 1 or 8 repeats, or at 0.25 repeats
# tile-continuous) are only computed where a drawn chunk has a nibble-4 layer (Retail map 2923).
ALTERNATIVES = [('plain weights', {'rule': 'plain weights'}), ('sequential', {'rule': 'sequential'}),
                ('sequential heightmix', {'rule': 'sequential heightmix'}), ('pow1.25', {'rule': 'pow1.25'}),
                ('pow1.5', {'rule': 'pow1.5'}), ('linear', {'linear': True}), ('no MCCV', {'mccv': False}),
                ('default heights', {'heights': 'default'}), ('full formula', {'heights': 'full'}),
                ('heights at 8 repeats', {'height_repeats': 8}), ('heights at 1 repeat', {'height_repeats': 1}),
                ('height red channel', {'height_chan': 0}),
                ('nibble 4 chunk-local', {'nibble4': (0.5, False)}), ('nibble 4 as 1 repeat', {'nibble4': (1, False)}),
                ('nibble 4 as 8 repeats', {'nibble4': (8, False)}),
                ('nibble 4 at 0.25 tile-continuous', {'nibble4': (0.25, True)}),
                ('alpha +0.25 texel', {'placement': ('centre', 0.25)}), ('alpha -0.25 texel', {'placement': ('centre', -0.25)}),
                ('alpha i/63', {'placement': ('i/63', 0.0)})] + \
               [('phase %+d,%+d' % d, {'phase': d}) for d in ((1, 0), (-1, 0), (0, 1), (0, -1))]
SYMMETRY_ALTERNATIVES = [('alpha ' + SYMMETRIES[s][0], {'alpha_sym': s}) for s in range(1, 8)] + \
                        [('texture ' + SYMMETRIES[s][0], {'tex_sym': s}) for s in range(1, 8)]


def golden_table(data, case, quick=False, verify=True):
    """Every number the golden test asserts for one tile: the design composite and each alternative, 4 x 4-block RMSE
    (golden mask: chunks not drawn with every texture and hole cells masked) and per-pixel RMSE over the same pixels,
    plus the same block RMSE over five subsets: the MCCV chunks, the LayerEffectStatic chunks, the chunks with
    stored height data, the chunks with a nibble-4 layer and the chunks with 6..8 layers."""
    c = load_case(data, case, verify)
    mat, M, tex = c['mat'], c['map'], c['textures']
    keep = material_keep(mat)
    keep_px = pixel_keep(keep)
    mref = [block_means(ch, MAP_SIZE) for ch in M]
    res = {'label': c['label'], 'measured': case[8], 'threshold': case[8] + GOLDEN_ALLOWANCE, 'counters': mat['counters'],
           'notes': mat['notes'], 'blocks': sum(keep), 'missing': c['missing'], 'height_missing': c['height_missing'],
           'mphd': c['mphd'], 'variants': []}
    subsets = {
        'mccv': set(ch['n'] for ch in mat['chunks'] if ch['state'] in GOLDEN_STATES and ch['vertexColors'] is not None),
        'static': set(ch['n'] for ch in mat['chunks'] if ch['state'] == 'LayerEffectStatic'),
        'height': set(n for n in height_chunks(mat) if mat['chunks'][n]['state'] in GOLDEN_STATES),
        'nibble4': set(ch['n'] for ch in mat['chunks'] if ch['state'] in GOLDEN_STATES and any(
            lay['scaleNibble'] == 4 for lay in ch['layers'])),
        'layers6to8': set(ch['n'] for ch in mat['chunks'] if ch['state'] in GOLDEN_STATES and len(ch['layers']) > 5)}
    keeps = dict((k, material_keep(mat, chunks=v)) for k, v in subsets.items())
    for k, v in subsets.items():
        res['%s_chunks_kept' % k] = len(v)
        res['%s_blocks' % k] = sum(keeps[k])
    differs = {}
    for mode in ('default', 'full'):
        differs[mode] = any(chunk_heights(mat, ch, mode) != chunk_heights(mat, ch, 'design')
                            for ch in mat['chunks'] if ch['state'] in DRAWABLE)
    differs['sampled'] = any(hp is not None and hp[0] != 0 and hp[1] != 0.0
                             for ch in mat['chunks'] if ch['state'] in DRAWABLE
                             for hp in (chunk_heights(mat, ch, 'design') or []))
    differs['nibble4'] = any(lay['scaleNibble'] == 4 for ch in mat['chunks'] if ch['state'] in DRAWABLE
                             for lay in ch['layers'])
    res['heights differ'] = differs
    cache = {}

    def run(name, **kw):
        img, info = map_composite(mat, tex, cache=cache, **kw)
        b = block_rmse(img, M, keep, ref_means=mref)
        r = {'block': b[0], 'bias': b[1], 'pixel': pixel_rmse(img, M, keep_px), 'notes': info['notes'],
             'clipped': info['clipped'], 'args': kw}
        for k in subsets:
            r['%s_block' % k] = block_rmse(img, M, keeps[k], ref_means=mref)[0] if subsets[k] else None
        res[name] = r
        res['variants'].append(name)
        return img
    run('design')
    for name, kw in ALTERNATIVES + ([] if quick else SYMMETRY_ALTERNATIVES):
        if ('heights' in kw and not differs[kw['heights']]) or (
                ('height_repeats' in kw or 'height_chan' in kw) and not differs['sampled']) or (
                'nibble4' in kw and not differs['nibble4']):
            res[name] = dict(res['design'], same_as_design=True, args=kw)
            res['variants'].append(name)
            continue
        run(name, **kw)
    return res


def converter_agreement(tex4_buf, tex8_buf, table=CONVERTER_TABLE):
    """Blizzard's offline 4-bit -> 8-bit conversion (documents U19; not a draw-time rule): in chunks whose layer
    texture lists are equal (>= 2 layers, all 4-bit in the first file, 8-bit in the second), each 4-bit overlay through
    `table`, layered one after another (w_k = a_k * prod_{j>k} (1 - a_j)), floored to a byte, against the 8-bit
    copy's byte. Returns {'texels', 'within1', 'exact', 'by_overlays': {n: (within1, texels)}}."""
    t4, t8 = read_tex0(tex4_buf), read_tex0(tex8_buf)
    tot = w1 = ex = 0
    by = {}
    for c4, c8 in zip(t4['chunks'], t8['chunks']):
        if c4['problem'] or c8['problem'] or len(c4['layers']) < 2:
            continue
        l4 = [t4['mdid'][l['texture']] for l in c4['layers']]
        l8 = [t8['mdid'][l['texture']] for l in c8['layers']]
        if l4 != l8 or any(m[0] != 'raw4' for m in c4['alpha'][1:]) or any(m[0] == 'raw4' for m in c8['alpha'][1:]):
            continue
        ov = [[table[v] / 255.0 for v in m[1]] for m in c4['alpha'][1:]]
        n = len(ov)
        for k in range(n):
            p = list(ov[k])
            for j in range(k + 1, n):
                p = [a * (1.0 - b) for a, b in zip(p, ov[j])]
            for a8, pv in zip(c8['alpha'][k + 1][1], p):
                d = abs(a8 - math.floor(pv * 255 + 1e-9))
                tot += 1
                w1 += d <= 1
                ex += d == 0
                s = by.setdefault(n, [0, 0])
                s[0] += d <= 1
                s[1] += 1
    return {'texels': tot, 'within1': w1 / tot if tot else float('nan'), 'exact': ex / tot if tot else float('nan'),
            'by_overlays': {k: (v[0] / v[1], v[1]) for k, v in sorted(by.items())}}


# ------------------------------------------------------------------------------------------------- dump writing

def tile_coverage(mat, composited, px=MAP_PIXELS_PER_CHUNK):
    """Per pixel of a (16 px)^2 tile image: 1.0 where a chunk was composited and the cell is not a hole, else 0.0 (what
    the player's dump holds in A)."""
    size = 16 * px
    cov = [0.0] * (size * size)
    for n in composited:
        ch = mat['chunks'][n]
        for y in range(px):
            for x in range(px):
                if not hole_cell(ch, (x + 0.5) / px, (y + 0.5) / px):
                    cov[(ch['iy'] * px + y) * size + ch['ix'] * px + x] = 1.0
    return cov


def write_floats(path, values):
    import array
    raw = array.array('f', values)
    if sys.byteorder != 'little':
        raw.byteswap()
    with open(path, 'wb') as f:
        f.write(raw.tobytes())


def write_composite_dump(base, img, size, meta, alpha=None, png=True):
    """Write <base>.raw (float32 RGBA, north row first, 0..1), <base>.json and <base>.png in the composite dump format
    of compare_terrain_material.py; img: (R, G, B) 0..255 lists; alpha: per pixel coverage (default all 1)."""
    vals = []
    for i in range(size * size):
        vals.extend((img[0][i] / 255.0, img[1][i] / 255.0, img[2][i] / 255.0, 1.0 if alpha is None else alpha[i]))
    write_floats(base + '.raw', vals)
    side = dict(meta)
    side.update({'format': 'wmv-terrain-composite', 'version': 1, 'width': size, 'height': size,
                 'raw': os.path.basename(base) + '.raw', 'sampleFormat': 'float32le', 'channels': 'RGBA',
                 'rowOrder': 'north-first'})
    if png:
        rows = [[tuple(max(0, min(255, int(round(img[c][y * size + x])))) for c in range(3)) for x in range(size)]
                for y in range(size)]
        from minimap_orientation import write_png
        write_png(base + '.png', rows)
        side['png'] = os.path.basename(base) + '.png'
    with open(base + '.json', 'w', newline='\n') as f:
        json.dump(side, f, indent=1)


def check_chunk_drawable(ch):
    """chunkdump's guard: ValueError for a chunk that is not drawn in the textured view (its state not in DRAWABLE),
    with its state and reason -- there is no V1 dump of it (compare_terrain_material.py v1 refuses one as well)."""
    if ch['state'] not in DRAWABLE:
        raise ValueError('MCNK %d is %s: %s (not drawn in the textured view; no V1 dump)' % (ch['n'], ch['state'],
                                                                                               ch['reason']))


def write_chunk_dump(base, ch, texels, size, meta, mat=None, rule='heightmix'):
    """The reference's own V1 dump of one chunk (validation sampling, holes cleared): the composite format with
    kind 'chunk'. texels as for validation_rows; `mat` (the tile material) brings the heights into the mix; `rule`
    (a RULES name) is the design's unless a test asks for an alternative. A chunk that is not drawable is refused
    (check_chunk_drawable) before anything is written.
    clippedPixels counts the non-hole pixels with a channel outside 0..255 (nothing is clamped)."""
    check_chunk_drawable(ch)
    vals = []
    clipped = 0
    for y, rr, gg, bb in validation_rows(ch, texels, size, mat=mat, rule=rule):
        v = (y + 0.5) / size
        for x in range(size):
            if hole_cell(ch, (x + 0.5) / size, v):
                vals.extend((0.0, 0.0, 0.0, 0.0))
            else:
                vals.extend((rr[x] / 255.0, gg[x] / 255.0, bb[x] / 255.0, 1.0))
                clipped += not (0.0 <= rr[x] <= 255.0 and 0.0 <= gg[x] <= 255.0 and 0.0 <= bb[x] <= 255.0)
    write_floats(base + '.raw', vals)
    side = dict(meta)
    side.update({'format': 'wmv-terrain-composite', 'version': 1, 'width': size, 'height': size, 'kind': 'chunk',
                 'chunk': ch['n'], 'pixelsPerChunk': size, 'raw': os.path.basename(base) + '.raw',
                 'sampleFormat': 'float32le', 'channels': 'RGBA', 'rowOrder': 'north-first', 'sampling': 'validation',
                 'saturated': False, 'clippedPixels': clipped, 'colourDomain': 'authored', 'view': 'textured'})
    with open(base + '.json', 'w', newline='\n') as f:
        json.dump(side, f, indent=1)


def write_texel_dump(base, blps, client=None, build=None, roles=None, md5s=None):
    """A texel dump (compare_terrain_material.py format 2) of each Blp's mip 0 by this file's decode (the Blp's
    model: 'exact', 'hardware' for what this GPU returns, or 'rounded' for the player's CPU decode of a block texture,
    written with the upload format DECODED_UPLOAD as the player writes it): what the player's dump would hold if the
    GPU decoded exactly like it. blps: {fileDataID: Blp}; roles: {fileDataID: 'diffuse', 'height' or a list of both}
    (default 'diffuse'): an id with both roles gets one entry per role, both naming the one raw file, as the player
    writes it; md5s: {fileDataID: content key} (default: the MD5 of the Blp's bytes)."""
    entries = []
    for fd in sorted(blps):
        b = blps[fd]
        w, h, px = b.decode(0)
        name = '%s.%d.raw' % (os.path.basename(base), fd)
        write_floats(os.path.join(os.path.dirname(os.path.abspath(base)), name), [v / 255.0 for v in px])
        rs = (roles or {}).get(fd) or ['diffuse']
        fmt = DECODED_UPLOAD if b.model == 'rounded' and b.encoding == 2 else b.format
        for role in ([rs] if isinstance(rs, str) else rs):
            entries.append({'fileDataID': fd, 'role': role, 'md5': (md5s or {}).get(fd) or md5_hex(b.buf), 'width': w,
                            'height': h, 'format': fmt, 'raw': name, 'sampleFormat': 'float32le',
                            'channels': 'RGBA', 'rowOrder': 'stored'})
    side = {'format': 'wmv-terrain-texels', 'version': 1, 'source': 'reference', 'client': client, 'textures': entries}
    if build:
        side['build'] = build
    with open(base + '.json', 'w', newline='\n') as f:
        json.dump(side, f, indent=1)


def input_meta(data, client, wdt_rel, maid):
    """The sidecar fields that name a tile's inputs: build (the pinned client build) and the MD5 of the WDT, root and
    tex0 bytes, as the player records the content keys it was sent."""
    md5 = lambda fd: md5_hex(data.read(client, fd)) if fd else None
    return {'build': data.hashes['client_builds'].get(client), 'wdtMd5': md5_hex(read_bytes(os.path.join(data.root, wdt_rel))),
            'rootMd5': md5(maid[0]), 'tex0Md5': md5(maid[3])}


# ------------------------------------------------------------------------------------------------- command line

def _print_states(mat):
    print('counters: %s' % ', '.join('%s %d' % (k, v) for k, v in mat['counters'].items()))
    print('notes: %s' % ', '.join('%s %d' % (k, v) for k, v in mat['notes'].items()))
    print('unique textures of drawable chunks (%d, first use): %s' % (len(mat['uniqueTextures']), mat['uniqueTextures']))
    print('unique height textures of drawable chunks (%d, MPHD 0x80 only): %s' % (len(mat['uniqueHeightTextures']),
                                                                                 mat['uniqueHeightTextures']))
    print('layer references %d (MDID 0: %d)' % (mat['layerReferences'], mat['zeroIdReferences']))
    for ch in mat['chunks']:
        if ch['state'] != 'Textured':
            print('  MCNK %3d (%2d,%2d) %-22s %s' % (ch['n'], ch['ix'], ch['iy'], ch['state'], ch['reason']))


def print_golden(res):
    print('%s (MPHD 0x%x): threshold %.3f (measured %.3f + %.2f); %d blocks kept; MCCV chunks %d (%d blocks), '
          'LayerEffectStatic chunks %d (%d blocks), height-data chunks %d (%d blocks), nibble-4 chunks %d (%d blocks), '
          '6..8-layer chunks %d (%d blocks)'
          % (res['label'], res['mphd'], res['threshold'], res['measured'], GOLDEN_ALLOWANCE, res['blocks'],
             res['mccv_chunks_kept'], res['mccv_blocks'], res['static_chunks_kept'], res['static_blocks'],
             res['height_chunks_kept'], res['height_blocks'], res['nibble4_chunks_kept'], res['nibble4_blocks'],
             res['layers6to8_chunks_kept'], res['layers6to8_blocks']))
    print('  states %s; notes %s' % (', '.join('%s %d' % (k, v) for k, v in res['counters'].items() if v),
                                     ', '.join('%s %d' % (k, v) for k, v in res['notes'].items())))
    for k in res['variants']:
        v = res[k]
        extra = ''.join('  %s %.3f' % (s, v['%s_block' % s]) for s in ('mccv', 'static', 'height', 'nibble4', 'layers6to8')
                        if v.get('%s_block' % s) is not None)
        print('  %-36s 4x4 RMSE %6.3f  pixel RMSE %6.3f  bias %s%s%s%s' % (
            k, v['block'], v['pixel'], ' '.join('%+.2f' % b for b in v['bias']), extra,
            '  (= design: no chunk differs)' if v.get('same_as_design') else '',
            ('  notes: ' + '; '.join(sorted(set(v['notes'])))) if v['notes'] else ''))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    mode, rest = argv[1], argv[2:]
    try:
        if mode == 'states':
            wdt, x, y, root, tex0 = rest[0], int(rest[1]), int(rest[2]), rest[3], rest[4]
            missing = set()
            if '--missing' in rest:
                missing = set(int(v) for v in rest[rest.index('--missing') + 1].split(',') if v)
            w = WDT(read_bytes(wdt))
            mat = build_material(read_bytes(root), read_bytes(tex0), w.mphd_words[0], missing)
            print('tile %d_%d, MPHD 0x%x' % (x, y, w.mphd_words[0]))
            _print_states(mat)
            return 0
        if mode == 'maptexture':
            data = AuditData(rest[0])
            client, wdt, x, y, base = rest[1], rest[2], int(rest[3]), int(rest[4]), rest[5]
            mphd, maid = tile_ids(data.root, wdt, x, y)
            mat, tex = tile_material(data, client, mphd, maid[0], maid[3])
            raw, info = map_composite(mat, tex, clip=False)
            cover = tile_coverage(mat, info['composited'])
            img = tuple([0.0 if a < 0.0 else 255.0 if a > 255.0 else a for a in c] for c in raw)
            info['clipped'] = sum(1 for k, r, g, b in zip(cover, *raw) if k and not (
                0.0 <= r <= 255.0 and 0.0 <= g <= 255.0 and 0.0 <= b <= 255.0))
            meta = {'source': 'reference', 'client': client, 'wdt': wdt, 'tile': [x, y], 'rootFileDataID': maid[0],
                    'tex0FileDataID': maid[3], 'mapTextureFileDataID': maid[5], 'mphdFlags': mphd, 'kind': 'tile',
                    'pixelsPerChunk': MAP_PIXELS_PER_CHUNK, 'sampling': 'production', 'clippedPixels': info['clipped'],
                    'saturated': True, 'colourDomain': 'authored', 'view': 'textured', 'states': mat['counters']}
            meta.update(input_meta(data, client, wdt, maid))
            write_composite_dump(base, img, MAP_SIZE, meta, cover)
            print('wrote %s.raw/.json/.png (%d chunks composited, %d px clipped)' % (base, len(info['composited']), info['clipped']))
            return 0
        if mode == 'chunkdump':
            data = AuditData(rest[0])
            client, wdt, x, y, n, base = rest[1], rest[2], int(rest[3]), int(rest[4]), int(rest[5]), rest[6]
            mphd, maid = tile_ids(data.root, wdt, x, y)
            mat, tex = tile_material(data, client, mphd, maid[0], maid[3])
            ch = mat['chunks'][n]
            check_chunk_drawable(ch)                       # before the texel dump is written
            need = needed_textures(mat, ch)
            blps = dict((fd, tex(fd)) for fd, _ in need if tex(fd))
            roles = texture_roles(need)                    # an id used as diffuse and height gets both
            size, why = validation_size(ch, lambda fd: (blps[fd].width, blps[fd].height) if fd in blps else None, mat)
            if size is None:
                raise ValueError(why)
            meta = input_meta(data, client, wdt, maid)
            write_texel_dump(base + '_texels', blps, client, meta['build'], roles)
            meta.update({'source': 'reference', 'client': client, 'wdt': wdt, 'tile': [x, y], 'rootFileDataID': maid[0],
                         'tex0FileDataID': maid[3], 'mphdFlags': mphd, 'texelDump': os.path.basename(base) + '_texels.json',
                         'chunks': [{'n': n, 'state': ch['state'], 'layers': [{'fileDataID': l['fileDataID'], 'repeats': l['repeats']}
                                                                              for l in ch['layers']]}]})
            write_chunk_dump(base, ch, lambda fd: blps[fd].decode(0) if fd in blps else None, size, meta, mat)
            print('wrote %s.raw/.json (MCNK %d, %s, %d px) and %s_texels.json' % (base, n, ch['state'], size, base))
            return 0
        if mode == 'golden':
            data = AuditData(rest[0])
            labels = [a for a in rest[1:] if not a.startswith('--')]
            for case in GOLDEN:
                if labels and case[0] not in labels:
                    continue
                print_golden(golden_table(data, case, quick='--quick' in rest))
            return 0
        if mode == 'converter':
            data = AuditData(rest[0])
            for label, client, fd, need in CONVERTER_CASES:
                r = converter_agreement(data.read('wow_classic_era', fd), data.read(client, fd))
                print('%s: Era 4-bit vs %s 8-bit, %d texels, within +-1 %.2f%% (exact %.2f%%; minimum %.1f%%), by overlays %s'
                      % (label, client, r['texels'], 100 * r['within1'], 100 * r['exact'], 100 * need,
                         {k: '%.2f%% of %d' % (100 * v[0], v[1]) for k, v in r['by_overlays'].items()}))
            return 0
    except (ValueError, IndexError, OSError, KeyError, MaterialUnavailable) as e:
        print('error: %s: %s' % (type(e).__name__, e))
        return 2
    print('unknown mode %s' % mode)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
