"""Compare the player's terrain material dumps (stage 04) with the independent CPU reference
(terrain_material_reference.py) and with the game's own map texture (MAID word 5).

  python -I compare_terrain_material.py v1 <composite json> [<composite json> ...] --texels <texel json>
                                        (--corpus <audit data> | --root <root adt> --tex0 <tex0 adt> --wdt <wdt>)
                                        [--json <out.json>]
  python -I compare_terrain_material.py v2 <composite json | 8-bit png> (--corpus <audit data> | --root <root adt>
                                        --tex0 <tex0 adt> --wdt <wdt> --map-texture <blp> --textures <folder>)
                                        [--sidecar <json> for a png] [--tile x,y --client c] [--threshold <rmse>]
                                        [--reference-tolerance <0..255>] [--json <out.json>]
  python -I compare_terrain_material.py texels <texel json> (--corpus <audit data> | --textures <folder>) [--json ..]

With --corpus, every file is found by client and FileDataID in the audit data (terrain_material_reference.AuditData)
and verified against its pin or its folder's manifest row, whose build must be the pinned client build: the WDT,
root, tex0, every texture and height texture and (v2) the map texture; the ids come from the sidecar and are checked
against the WDT's MAID words. The sidecar's build must be that pinned build and its md5 fields must equal the MD5 of
those verified files, so a dump of other bytes (another client, another build) is never judged against the corpus.

DUMP FORMATS (the player writes them; terrain_material_reference.py writes the same formats)

1. Composite dump: <base>.raw, <base>.json and optionally <base>.png with one base name.
   <base>.raw   width x height pixels x 4 float32, little-endian, R G B A per pixel; rows NORTH FIRST (row 0 is the
                north edge of the rendered area, Unity +Z), columns WEST TO EAST (column 0 is the west edge, Unity
                -X); so pixel (0, 0) is the north-west corner, as in the game's map texture.
                R, G, B: the composite in authored values, 0..1 = 0..255 of the stored texels, no display encoding.
                In validation sampling never clamped (MCCV pushes some pixels above 1); in production sampling
                clamped exactly when `saturated` is true.
                A: coverage, 1.0 where a terrain fragment was written, 0.0 where nothing was (hole cells are cleared
                to 0, chunks that were not drawn too).
                Kind "chunk" (one chunk alone, N x N): pixel (x, y) is the chunk at u = (x + 0.5) / N east,
                v = (y + 0.5) / N south of its north-west corner (mesh UV0 = 8u, 8v cells). Kind "tile": pixel (x, y)
                lies in chunk (x // P, y // P), P = pixelsPerChunk, at u = (x % P + 0.5) / P, v likewise.
                Validation sampling at (u, v), as terrain_material_reference.validation_rows computes it: layer k's
                texel (floor((u * repeats_k + frac(ix * repeats_k)) * W) mod W, floor((v * repeats_k +
                frac(iy * repeats_k)) * H) mod H) -- repeats_k = 8 / 2^n for MTXP scale nibble n = 0..4 (0.5 at
                n = 4), the phase of chunk c (ix = c % 16 east, iy = c / 16 south) 0 for whole repeats: the UV runs on
                across chunks -- and on MPHD 0x80 maps its height texture's texel the same way (its alpha, / 255, is
                h_k); alpha k by bilinear at texel
                coordinates (64u - 0.5, 64v - 0.5), indices clamped to 0..63, / 255; w_0 = 1 - sum(a_k), w_k = a_k;
                the height mix: H_k = h_k * heightScale_k + heightOffset_k (MTXP; (0, 1) without it) with MPHD 0x80,
                1 without; p_k = max(w_k, 0) * H_k, m = max p, p_k *= 1 - clamp(m - p_k, 0, 1), S = sum p;
                colour = sum(p_k * texel_k) / S (sum(max(w_k, 0) * texel_k) when S <= 0) x MCCV / 127 (interpolated
                over the cell's four-triangle fan); a missing layer is magenta (1, 0, 1) where floor(64u) +
                floor(64v) is even, black elsewhere.
   <base>.png   8-bit RGB view (clamped, rounded); v2 accepts it in place of the json, as the 512-px production view,
                as a WEAKER check: 8 bits (its per-pixel limit adds the 0.5/255 rounding), and with no raw values the
                coverage (A) and the saturated range are not checked.
   <base>.json  sidecar, a JSON object (ASCII content; UTF-8 with or without a byte-order mark is read):
     format "wmv-terrain-composite", version 1, source "player" (or "reference")
     client             audit-data product folder: "wow", "wow_classic_era", "wow_classic_beta", "wow_classic"
     build              the client build the player loaded from (e.g. "12.1.0.69933"; the host's `build`)
     tile               [x, y]; wdtFileDataID (or wdt: a path relative to the audit data); rootFileDataID;
                        tex0FileDataID; mapTextureFileDataID (MAID word 5; v2); mphdFlags (MPHD word 0)
     wdtMd5, rootMd5, tex0Md5
                        the content keys (MD5 of the bytes, lowercase hex) of the WDT, root and tex0 the player received
     kind               "tile" (16 x 16 chunks) or "chunk" (one chunk rendered alone; `chunk` = its index 0..255)
     width, height      pixels (square); pixelsPerChunk (tile: width / 16, 32 for v2; chunk: width)
     sampling           "validation" (-wmvTerrainValidate: texels by Load at integer indices, alpha by four Loads and
                        float weights) or "production" (the shader's own sampling)
     view               "textured" (any other view is refused: it is not the authored composite)
     raw, png           file names next to the json; sampleFormat "float32le"; channels "RGBA"; rowOrder
                        "north-first"; colourDomain "authored" (anything else is refused)
     saturated          true when R, G, B were clamped to 0..1 before writing (production only); clippedPixels:
                        covered pixels where any channel left 0..1 before clamping
     states             optional: chunk-state counters by name (malformed, alphaUnsupported, layerCountUnsupported,
                        layerEffectUnresolved, heightTextureMissing, noLayers, layerEffectStatic, missingTexture,
                        textured)
     chunks             optional: [{n, state, layers: [{fileDataID, repeats}]}], state by the design's names
     texelDump          optional: the texel dump's json file name (v1 uses it when --texels is not given)
2. Texel dump: <base>.json plus one raw file per texture.
   <base>.json  format "wmv-terrain-texels", version 1, source, client, build, textures: [{fileDataID, role
                ("diffuse" or "height": a layer texture or an MHID height texture; absent = "diffuse"), md5 (the
                content key of the file the player received), width, height, format (the upload format, e.g.
                "RGBA_DXT1_UNorm"; "R8G8B8A8_UNorm" for a texture the player decoded on the CPU), raw (file name
                next to the json), sampleFormat "float32le", channels "RGBA", rowOrder "stored"}]; an id is listed
                once per role it has in the tile
   <raw>        mip 0 as the GPU decodes it (Load at every texel): width x height x 4 float32, little-endian, 0..1;
                rows AS STORED in the BLP (row 0 = the first stored row = v 0, the north edge of every repeat), no
                flip. A height texture's mix value is its alpha channel.

ACCEPTANCE
  v1 (validation, stage 04 design section 8): each chunk dump against the reference composed from the dump's own
     texels (diffuse and height): the chunk is drawable (Textured, MissingTexture or LayerEffectStatic; a height
     texture missing from the texel dump, or whose BLP has no alpha channel, makes it HeightTextureMissing) and its
     state and layers equal the sidecar's (when given); the size is the design's N (max over every texture each layer
     binds at its own repeats, height textures included, of texture width x repeats, a power of two <= 8192); every
     non-hole pixel covered
     (A = 1) and every hole pixel cleared (A = 0); |dump - reference| <= 0.5/255 per channel at every non-hole pixel.
     Production sampling: every texture each layer binds must be at one texel per pixel at that layer's repeats (an id
     bound at two repeats is checked at both), tolerance 2/255. A saturated dump (production
     only; `saturated` true with validation sampling is refused) is compared with the reference clamped to 0..255,
     every dump value must lie in 0..1, and clippedPixels must equal the reference's count of non-hole pixels with a
     channel outside 0..255 -- exactly, except that pixels whose reference lies within the tolerance below 255 (a
     composite is never negative) may count either way (the range of counts is printed); an unclamped dump's
     clippedPixels must also equal its own count of pixels outside 0..1, exactly.
  v2 (oracle): the 512-px production tile composite against the map texture (verified like every other input): 4 x
     4-block RMSE over the blocks of Textured and LayerEffectStatic chunks without a checker layer (neutral,
     missing-texture and checker-drawing chunks and hole cells masked) <= the golden threshold of
     terrain_material_reference.GOLDEN (or --threshold); the identity is the best of the 8 symmetries of the dump (4 x
     4-block RMSE); zero phase is better than +-1 px in x and in y (per-pixel RMSE); every kept pixel covered; a
     saturated raw dump holds only 0..1. The dump against the reference composite, per pixel: every kept pixel within
     0.26/255 (V2_REFERENCE_TOLERANCE, V2's own error budget, about 0.13/255, doubled: the float16 dump's truncating
     store, below 2^-11 = 0.1245/255 in the compared range 0..1, plus the difference before the store, measured at
     least 0.0045/255 (a lower bound; no upper bound is measured; source INFERRED)) -- 0.76/255 for an 8-bit PNG
     input (V2_PNG_REFERENCE_TOLERANCE: the PNG's 8-bit rounding, up to 0.5/255, on top of the float limit; the PNG
     path is the weaker check), the limit named in the check;
     --reference-tolerance replaces either -- of the reference composed under the better-fitting of three
     NAMED block decodes, which the check names: the exact decode (the format), the hardware model (this GPU's measured
     interpolants: terrain_material_reference.hardware_interp5/6, hardware_alpha) and the rounded decode (the format
     rounded to 8 bits: what the player uploads when it decodes a block texture on the CPU); the GPU decodes
     interpolated block texels with its own rounding (up to 5.3/255 colour, 6.4/255 DXT5 alpha off the exact ramp), so
     the exact decode alone is not the player's input. Which decode a texture may legitimately have is the texels
     check's (by its upload format); v2 has no per-texture format and names one decode for the whole tile. The
     plain-weights composite's RMSE is reported.
  inputs (v1, v2, texels): with --corpus the sidecar's build equals the pinned client build and its md5 fields equal
     the verified audit files' MD5; without --corpus the md5 fields are checked against the given files and the build
     is reported.
  texels: every texture's GPU texels against this file's BLP decode, dimensions equal to the BLP's mip 0. Textures
     whose BLP is not block-compressed, block endpoints (colour indices 0 and 1, a DXT1 three-colour block's
     transparent black), DXT3's explicit alpha and DXT5's alpha a0, a1 and its 6-value mode's 0 and 255 must come back
     exact (0.01/255): the endpoint check keys on the BLP's encoding, not on the upload format, so a block-compressed
     BLP uploaded decoded must still give the format's endpoints. The interpolated texels key on the entry's upload
     format. Uploaded as blocks (any format but R8G8B8A8_UNorm): every four-colour interpolant (all three channels)
     must equal ONE named decode within 0.01/255 for the whole texture -- the hardware model or the exact ramp -- and
     the check names it (mixed or neither: FAIL); DXT5's interpolated alphas likewise; a DXT1 three-colour block's
     midpoint has no measured model: it must lie between its two endpoints (+-1/255) and is counted as unmodelled.
     Uploaded as R8G8B8A8_UNorm (the player decoded the blocks on the CPU: its RGBA32 fallback): every interpolated
     colour (the midpoint included) and every interpolated DXT5 alpha must equal the 'rounded' decode (the format
     rounded to nearest, the DXT1 midpoint's ties to even: BlpDecoder's exact-blocks mode), and nothing else. The
     deviation from the exact decode is reported with a histogram.
Exit code: 0 PASS, 1 FAIL, 2 usage or input error.
"""
import array
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import minimap_orientation as MO  # noqa: E402
import terrain_material_reference as R  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

COMPOSITE_FORMAT = 'wmv-terrain-composite'
TEXEL_FORMAT = 'wmv-terrain-texels'
FORMAT_VERSION = 1
V1_TOLERANCE = 0.5            # /255, float dump in validation sampling
V1_PRODUCTION_TOLERANCE = 2.0  # /255: 8-bit sub-texel filter grid + UNorm rounding (measured 1.06-1.40)
# V2's per-pixel limit against the reference under the better-fitting named decode, /255, from V2's own error budget
# (not V1's: the V2 render has no sub-texel filter grid -- at 512 px every layer and height texture lands on a texel
# centre of a whole mip and the alpha is sampled at fraction exactly 0.5 -- and no UNorm target).
#   store: the dump is a float16 render (R16G16B16A16_SFloat) whose store truncates. The compared values lie in 0..1
#     (both sides clamped: the dump by image_from, the reference by map_composite's clip; 1.0 is stored exactly),
#     where one float16 step is at most 2^-11 = 0.1245/255 (in [0.5, 1)), so the store is off by less than 0.1245/255.
#   before the store: on the nine golden-tile dumps the GPU value differs from the reference by at least 0.0028/255
#     above it (a truncating store cannot raise a value) and by at least 0.0045/255 below it (a value more than one
#     float16 step under the reference, 2923 32_24). These are lower bounds; no upper bound is measured. Source (the
#     GPU's arithmetic) INFERRED.
#   budget: 0.1245 + 0.0045 = about 0.13/255. Limit: twice the budget, 0.26/255, the margin for the unmeasured
#   pre-store part. Measured:
#   0.1235-0.1259/255 on the real V2 dumps of the nine golden tiles (hardware model); a player dividing MCCV by 127.25
#   instead of 127 measures 0.425/255 and must fail.
# --reference-tolerance overrides it (and V2_PNG_REFERENCE_TOLERANCE).
V2_REFERENCE_TOLERANCE = 0.26
# The same limit for an 8-bit PNG input (the documented <base>.png in place of the json): the PNG is the float
# composite rounded to 8 bits, at most 0.5/255 off it, on top of the float limit. A weaker check (see v2 below).
V2_PNG_REFERENCE_TOLERANCE = V2_REFERENCE_TOLERANCE + 0.5
TEXEL_ENDPOINT_TOLERANCE = 0.01  # /255: block endpoints (and DXT3 alpha, DXT5 alpha endpoints/0/255) must come back
#                                  exact, and an interpolant must equal its named decode: a float32 of k/255 is k
TEXEL_BOUND_SLACK = 1.0          # /255: a DXT1 three-colour midpoint (no measured model) lies between its endpoints
ROLES = ('diffuse', 'height')
MODEL_NAMES = {'exact': 'exact decode', 'hardware': 'hardware model', 'rounded': 'rounded decode'}
# The texels check's named decodes of an interpolated block texel, by the dump entry's upload format: a block format
# (the GPU decodes the stored blocks) -> the hardware model or the exact ramp; R.DECODED_UPLOAD (the player decoded
# the blocks on the CPU, BlpDecoder's exact-blocks mode, and uploaded RGBA32) -> the rounded decode only.
TEXEL_MODEL_LABELS = (('hardware', 'hardware model'), ('exact', 'exact ramp'), ('rounded', 'rounded'))


class InputError(Exception):
    """A usage or input problem (exit 2), never a FAIL."""


class Report:
    def __init__(self):
        self.checks = []

    def check(self, ok, name, detail=''):
        self.checks.append({'name': name, 'result': 'PASS' if ok else 'FAIL', 'detail': detail})
        print('  %s  %s%s' % ('PASS' if ok else 'FAIL', name, (' (%s)' % detail) if detail else ''))
        return ok

    def info(self, name, detail=''):
        self.checks.append({'name': name, 'result': 'INFO', 'detail': detail})
        print('  INFO  %s%s' % (name, (' (%s)' % detail) if detail else ''))

    @property
    def failed(self):
        return sum(1 for c in self.checks if c['result'] == 'FAIL')


# ------------------------------------------------------------------------------------------------- reading dumps

def read_json(path):
    try:
        with open(path, 'r', encoding='utf-8-sig') as f:
            return json.load(f)
    except (ValueError, UnicodeDecodeError) as e:
        raise InputError('%s: not a JSON file (%s)' % (path, e))


def read_floats(path, count):
    data = R.read_bytes(path)
    if len(data) != 4 * count:
        raise InputError('%s: %d bytes, %d float32 expected' % (path, len(data), count))
    a = array.array('f')
    a.frombytes(data)
    if sys.byteorder != 'little':
        a.byteswap()
    return a


def check_view(side, path):
    """A composite that is not the authored textured composite is refused (input error, never compared)."""
    if 'view' in side and side['view'] != 'textured':
        raise InputError('%s: view %r is not the authored composite (the dump must be taken in the textured view)'
                         % (path, side['view']))
    if 'colourDomain' in side and side['colourDomain'] != 'authored':
        raise InputError('%s: colourDomain %r (the format holds authored values)' % (path, side['colourDomain']))


def read_composite(path):
    """(sidecar, float RGBA array) of a composite dump json."""
    side = read_json(path)
    if side.get('format') != COMPOSITE_FORMAT or side.get('version') != FORMAT_VERSION:
        raise InputError('%s: not a %s version %d sidecar (%r %r)' % (path, COMPOSITE_FORMAT, FORMAT_VERSION,
                                                                    side.get('format'), side.get('version')))
    for k, want in (('sampleFormat', 'float32le'), ('channels', 'RGBA'), ('rowOrder', 'north-first')):
        if side.get(k) != want:
            raise InputError('%s: %s is %r, the format has %r' % (path, k, side.get(k), want))
    check_view(side, path)
    w, h = side.get('width'), side.get('height')
    if not isinstance(w, int) or not isinstance(h, int) or w <= 0 or w != h:
        raise InputError('%s: width/height %r x %r (square, positive)' % (path, w, h))
    raw = os.path.join(os.path.dirname(os.path.abspath(path)), side.get('raw', ''))
    return side, read_floats(raw, w * h * 4)


def read_texels(path):
    """(sidecar, fileDataID -> (width, height, float32 RGBA 0..1 array, 255.0 scale, [entries])) of a texel dump
    json; an id listed for both roles has two entries (same size; the first raw is read)."""
    side = read_json(path)
    if side.get('format') != TEXEL_FORMAT or side.get('version') != FORMAT_VERSION:
        raise InputError('%s: not a %s version %d sidecar' % (path, TEXEL_FORMAT, FORMAT_VERSION))
    out = {}
    base = os.path.dirname(os.path.abspath(path))
    for t in side.get('textures', []):
        for k, want in (('sampleFormat', 'float32le'), ('channels', 'RGBA'), ('rowOrder', 'stored')):
            if t.get(k) != want:
                raise InputError('%s: texture %r %s is %r, the format has %r' % (path, t.get('fileDataID'), k, t.get(k), want))
        w, h, fd = t.get('width'), t.get('height'), t.get('fileDataID')
        if not all(isinstance(v, int) and v > 0 for v in (w, h, fd)):
            raise InputError('%s: texture entry %r' % (path, t))
        role = t.get('role', 'diffuse')
        if role not in ROLES:
            raise InputError('%s: texture %d role %r (%s)' % (path, fd, role, ' or '.join(ROLES)))
        if fd in out:
            if role in [e.get('role', 'diffuse') for e in out[fd][4]]:
                raise InputError('%s: texture %d listed twice as %s' % (path, fd, role))
            if (w, h) != out[fd][:2]:
                raise InputError('%s: texture %d listed with sizes %dx%d and %dx%d' % ((path, fd) + out[fd][:2] + (w, h)))
            out[fd][4].append(t)
            continue
        out[fd] = (w, h, read_floats(os.path.join(base, t.get('raw', '')), w * h * 4), 255.0, [t])
    return side, out


def texel_roles(entry):
    return set(e.get('role', 'diffuse') for e in entry[4])


# ------------------------------------------------------------------------------------------------- reference inputs

class Inputs:
    """WDT, root, tex0, MPHD flags, map texture and textures of one tile, from --corpus (by the sidecar's ids, checked
    against the WDT, every file verified) or from explicit files."""

    def __init__(self, side, args, need_map=False):
        self.args = args
        self.client = side.get('client') or args.get('client')
        tile = side.get('tile') or ([int(v) for v in args['tile'].split(',')] if args.get('tile') else None)
        if not tile or len(tile) != 2:
            raise InputError('no tile: the sidecar names none (give --tile x,y)')
        self.tile = tuple(tile)
        self.data = R.AuditData(args['corpus']) if args.get('corpus') else None
        self.map_bytes = None
        self.map_how = None
        x, y = self.tile
        if self.data:
            if not self.client:
                raise InputError('no client: the sidecar names none (give --client)')
            wdt_rel = side.get('wdt') or ('wdt/%s/%d.bin' % (self.client, side.get('wdtFileDataID', 775971)))
            ok, how = self.data.verify(self.client, os.path.join(self.data.root, wdt_rel))
            if not ok:
                raise InputError('%s failed verification (%s)' % (wdt_rel, how))
            self.wdt = R.read_bytes(os.path.join(self.data.root, wdt_rel))
            self.mphd, maid = R.tile_ids(self.data.root, wdt_rel, x, y)
            want = {'rootFileDataID': maid[0], 'tex0FileDataID': maid[3], 'mapTextureFileDataID': maid[5]}
            for k, v in want.items():
                if k in side and side[k] != v:
                    raise InputError('sidecar %s %r, but the WDT (%s) tile %d_%d names %d' % (k, side[k], wdt_rel, x, y, v))
            self.ids = want
            try:
                self.root = self.data.read(self.client, maid[0])
                self.tex0 = self.data.read(self.client, maid[3])
                if need_map:
                    if not maid[5]:
                        raise InputError('the WDT names no map texture for tile %d_%d (MPHD 0x%x)' % (x, y, self.mphd))
                    self.map_bytes, self.map_how = self.data.read_how(self.client, maid[5])
            except (FileNotFoundError, ValueError) as e:
                raise InputError(str(e))
        else:
            for k in ('root', 'tex0', 'wdt'):
                if not args.get(k):
                    raise InputError('give --corpus, or --root, --tex0 and --wdt')
            self.wdt = R.read_bytes(args['wdt'])
            w = WDT(self.wdt)
            self.mphd = w.mphd_words[0]
            self.ids = {}
            self.root, self.tex0 = R.read_bytes(args['root']), R.read_bytes(args['tex0'])
            if need_map:
                if not args.get('map-texture'):
                    raise InputError('give --map-texture (or --corpus)')
                self.map_bytes = R.read_bytes(args['map-texture'])
                self.map_how = 'given file %s (not verified)' % args['map-texture']
        if 'mphdFlags' in side and side['mphdFlags'] != self.mphd:
            raise InputError('sidecar mphdFlags 0x%x, the WDT has 0x%x' % (side['mphdFlags'], self.mphd))

    def texture_bytes(self, fd):
        """(bytes, how) of a texture by FileDataID: verified from the audit data, or from --textures; (None, why)."""
        if self.data:
            try:
                return self.data.read(self.client, fd), 'verified'
            except FileNotFoundError:
                return None, 'not in the audit data'
            except ValueError as e:
                raise InputError(str(e))
        folder = self.args.get('textures')
        p = os.path.join(folder, '%d.bin' % fd) if folder else None
        if p and os.path.exists(p):
            return R.read_bytes(p), 'given folder'
        return None, 'not in %s' % folder

    def texture_source(self, missing, model='exact'):
        """texture(fileDataID) -> Blp decoded by `model` (terrain_material_reference.DECODE_MODELS) or None."""
        if self.data:
            return self.data.texture_source(self.client, True, missing, model)
        cache = {}

        def get(fd):
            if fd not in cache:
                b, why = self.texture_bytes(fd)
                if b is None:
                    missing[fd] = why
                cache[fd] = R.Blp(b, 'texture %d' % fd, model) if b is not None else None
            return cache[fd]
        return get

    def material(self, missing=None, height_missing=None):
        """The tile material with every texture the drawable chunks need looked up (failed ones `missing`; height
        textures without an alpha channel `height_missing`): terrain_material_reference.resolve_material."""
        missing = {} if missing is None else missing
        tex = self.texture_source(missing)
        return R.resolve_material(self.root, self.tex0, self.mphd, tex, missing, height_missing), tex


def check_inputs(rep, side, inputs, label):
    """The sidecar's build and md5 fields against the inputs the reference uses (see ACCEPTANCE: inputs)."""
    if inputs.data:
        want = inputs.data.hashes['client_builds'].get(inputs.client)
        rep.check(side.get('build') == want, label + ': sidecar build is the pinned client build',
                  'sidecar %r, pinned %r' % (side.get('build'), want))
    else:
        rep.info(label + ': sidecar build (no corpus to check it against)', repr(side.get('build')))
    for key, data in (('wdtMd5', inputs.wdt), ('rootMd5', inputs.root), ('tex0Md5', inputs.tex0)):
        got, want = side.get(key), R.md5_hex(data)
        rep.check(isinstance(got, str) and got.lower() == want, label + ': sidecar %s is the MD5 of the %s used' % (
            key, 'verified audit file' if inputs.data else 'given file'), 'sidecar %r, file %s' % (got, want))


def check_texel_inputs(rep, tside, texels, inputs, label, ids=None):
    """The texel dump's build and per-texture md5 against the verified (or given) BLP files; `ids`: only these.
    Without --corpus or --textures there are no files to check against (reported)."""
    if inputs.data:
        want = inputs.data.hashes['client_builds'].get(inputs.client)
        rep.check(tside.get('build') == want, label + ': texel dump build is the pinned client build',
                  'texel dump %r, pinned %r' % (tside.get('build'), want))
    elif not inputs.args.get('textures'):
        rep.info(label + ': texel dump md5 not checked (give --corpus or --textures)')
        return
    bad = []
    checked = 0
    for fd in sorted(texels):
        if ids is not None and fd not in ids:
            continue
        b, why = inputs.texture_bytes(fd)
        if b is None:
            bad.append('%d: %s' % (fd, why))
            continue
        want = R.md5_hex(b)
        for e in texels[fd][4]:
            checked += 1
            got = e.get('md5')
            if not (isinstance(got, str) and got.lower() == want):
                bad.append('%d (%s): dump %r, file %s' % (fd, e.get('role', 'diffuse'), got, want))
    rep.check(not bad, label + ': texel dump md5 of every texture is the MD5 of the %s' % (
        'verified audit file' if inputs.data else 'given file'), '; '.join(bad[:6]) or '%d entries' % checked)


def compare_states(rep, side, mat, label):
    """The sidecar's optional counters and chunk entries against the reference material."""
    states = side.get('states')
    if states:
        bad = ['%s %r vs %d' % (k, states[k], v) for k, v in mat['counters'].items() if k in states and states[k] != v]
        unknown = sorted(k for k in states if k not in mat['counters'])
        rep.check(not bad, label + ': chunk-state counters equal the reference', '; '.join(bad) or 'reference %s' % mat['counters'])
        if unknown:
            rep.info(label + ': counters the reference does not define', ', '.join(unknown))
    for ent in side.get('chunks') or []:
        n = ent.get('n')
        if not isinstance(n, int) or not 0 <= n < 256:
            rep.check(False, label + ': chunk entry', 'n %r' % n)
            continue
        ch = mat['chunks'][n]
        rep.check(str(ent.get('state')).lower() == ch['state'].lower(), label + ': MCNK %d state' % n,
                  'dump %r, reference %r (%s)' % (ent.get('state'), ch['state'], ch['reason']))
        if 'layers' in ent:
            got = [(l.get('fileDataID'), l.get('repeats')) for l in ent['layers']]
            want = [(l['fileDataID'], l['repeats']) for l in ch['layers'][:R.MAX_DRAWN_LAYERS]]
            rep.check(got == want, label + ': MCNK %d layers (texture, repeats)' % n, 'dump %s, reference %s' % (got, want))


# ------------------------------------------------------------------------------------------------- v1

def compare_v1(side, img, texels, inputs, rep, label):
    n = side.get('chunk')
    if side.get('kind') != 'chunk' or not isinstance(n, int) or not 0 <= n < 256:
        raise InputError('%s: v1 compares chunk dumps (kind "chunk" with a chunk index), got kind %r chunk %r'
                         % (label, side.get('kind'), n))
    size = side['width']
    sampling = side.get('sampling')
    if sampling not in ('validation', 'production'):
        raise InputError('%s: sampling %r' % (label, sampling))
    saturated = side.get('saturated', False)
    if not isinstance(saturated, bool):
        raise InputError('%s: saturated %r (true or false)' % (label, saturated))
    if saturated and sampling == 'validation':
        raise InputError('%s: a validation dump is never clamped, but saturated is true' % label)
    mat0 = R.build_material(inputs.root, inputs.tex0, inputs.mphd)
    need = R.needed_textures(mat0, mat0['chunks'][n])
    missing = dict((fd, 'not in the texel dump') for fd, _ in need if fd not in texels)
    height_missing = {}
    for fd in sorted(set(fd for fd, role in need if role == 'height' and fd in texels)):
        b, why = inputs.texture_bytes(fd)
        if b is None:
            rep.info(label + ': height texture %d not available for the alpha-channel check' % fd, why)
        elif R.Blp(b, 'texture %d' % fd).alpha_depth == 0:
            height_missing[fd] = R.HEIGHT_NO_ALPHA
    mat = R.build_material(inputs.root, inputs.tex0, inputs.mphd, missing, height_missing) if (
        missing or height_missing) else mat0
    compare_states(rep, side, mat, label)
    ch = mat['chunks'][n]
    if not rep.check(ch['state'] in R.DRAWABLE, label + ': MCNK %d is drawable' % n, '%s: %s' % (ch['state'], ch['reason'])):
        return
    roles = ['%d listed as %s, used as %s' % (fd, '/'.join(sorted(texel_roles(texels[fd]))), role)
             for fd, role in need if fd in texels and role not in texel_roles(texels[fd])]
    rep.check(not roles, label + ': texel dump roles (diffuse, height) as the chunk uses the textures', '; '.join(roles))
    for fd in sorted(set(fd for fd, _ in need if fd in texels)):
        b, why = inputs.texture_bytes(fd)
        if b is None:
            rep.info(label + ': texture %d not available for the size check' % fd, why)
            continue
        blp = R.Blp(b, 'texture %d' % fd)
        w, h = texels[fd][0], texels[fd][1]
        rep.check((blp.width, blp.height) == (w, h), label + ': texture %d texel dump is the BLP mip 0 size' % fd,
                  'dump %dx%d, BLP %dx%d' % (w, h, blp.width, blp.height))
    want, why = R.validation_size(ch, lambda fd: texels[fd][:2] if fd in texels else None, mat)
    rep.check(want == size, label + ': size is the design N', 'dump %d, N %s%s' % (size, want, (' (%s)' % why) if why else ''))
    tol = V1_TOLERANCE
    if sampling == 'production':
        tol = V1_PRODUCTION_TOLERANCE
        off = ['%d (%dx%d x %g)' % (fd, texels[fd][0], texels[fd][1], r) for fd, r in R.bound_pairs(mat, ch)
               if fd in texels and (texels[fd][0] * r != size or texels[fd][1] * r != size)]
        rep.check(not off, label + ': production sampling with every texture at one texel per pixel (each layer at its '
                  'own repeats)', ', '.join(off))
    worst = [0.0, 0.0, 0.0]
    worst_at = None
    over = covered = uncovered = holes = hole_drawn = 0
    nan = 0
    out_of_range = 0
    must_clip = may_clip = 0
    dump_clipped = 0
    lo, hi = -tol, 255.0 + tol
    for y, rr, gg, bb in R.validation_rows(ch, lambda fd: texels[fd][:4] if fd in texels else None, size, mat=mat):
        v = (y + 0.5) / size
        base = y * size * 4
        for x in range(size):
            u = (x + 0.5) / size
            p = base + 4 * x
            a = img[p + 3]
            if R.hole_cell(ch, u, v):
                holes += 1
                hole_drawn += a != 0.0
                continue
            if a == 1.0:
                covered += 1
            else:
                uncovered += 1
            refs = (rr[x], gg[x], bb[x])
            if any(r < lo or r > hi for r in refs):
                must_clip += 1
            if any(r < 0.0 or r > 255.0 - tol for r in refs):        # a composite is never negative
                may_clip += 1
            outside = any(not 0.0 <= img[p + c] <= 1.0 for c in range(3))
            dump_clipped += outside
            if saturated:
                refs = tuple(0.0 if r < 0.0 else 255.0 if r > 255.0 else r for r in refs)
                out_of_range += outside
            bad = False
            for c, ref in enumerate(refs):
                d = abs(img[p + c] * 255.0 - ref)
                if d != d:
                    nan += 1
                    bad = True
                    continue
                if d > worst[c]:
                    worst[c] = d
                    if d == max(worst):
                        worst_at = (x, y, c, img[p + c] * 255.0, ref)
                if d > tol:
                    bad = True
            over += bad
    total = covered + uncovered
    rep.check(uncovered == 0, label + ': every non-hole pixel covered (A = 1)',
              '%d of %d covered (%.4f%%)' % (covered, total, 100.0 * covered / total if total else 0.0))
    rep.check(hole_drawn == 0, label + ': hole cells cleared (A = 0)', '%d hole pixels, %d drawn' % (holes, hole_drawn))
    rep.check(over == 0 and nan == 0, label + ': |dump - %sreference| <= %.1f/255 at every non-hole pixel'
              % ('clamped ' if saturated else '', tol),
              '%d pixel(s) over, %d NaN; max R %.4f G %.4f B %.4f /255%s' % (
                  over, nan, worst[0], worst[1], worst[2],
                  '; worst at x %d y %d channel %d: dump %.4f, reference %.4f' % worst_at if worst_at else ''))
    if saturated:
        rep.check(out_of_range == 0, label + ': saturated dump holds only 0..1', '%d pixel(s) outside' % out_of_range)
    clipped = side.get('clippedPixels')
    if clipped is not None or saturated:
        rep.check(isinstance(clipped, int) and must_clip <= clipped <= may_clip,
                  label + ': clippedPixels equals the reference pixels outside 0..255',
                  'dump %r; reference %d beyond the range by more than %.1f/255, %d within that of its edge or beyond'
                  % (clipped, must_clip, tol, may_clip))
        if not saturated:
            rep.check(clipped == dump_clipped, label + ': clippedPixels equals the count of the unclamped dump pixels outside 0..1',
                      'sidecar %r, dump %d' % (clipped, dump_clipped))
    return {'chunk': n, 'size': size, 'sampling': sampling, 'saturated': saturated, 'max': worst, 'over': over,
            'covered': covered, 'pixels': total, 'holes': holes, 'clipped': clipped,
            'reference_clipped': [must_clip, may_clip]}


# ------------------------------------------------------------------------------------------------- v2

def golden_threshold(client, tile, tex0):
    """The golden threshold of a tile named by client, tile and tex0 FileDataID (all three must match)."""
    for case in R.GOLDEN:
        if case[1] == client and (case[3], case[4]) == tuple(tile) and tex0 == case[6]:
            return case[8] + R.GOLDEN_ALLOWANCE, case[0]
    return None, None


def image_from(side, img, size):
    """(R, G, B) 0..255 (clamped to 0..1 first), the coverage per pixel and the count of covered pixels holding a
    value outside 0..1, from a float RGBA array."""
    out = ([], [], [])
    cov = []
    outside = 0
    for i in range(size * size):
        p = 4 * i
        bad = False
        for c in range(3):
            v = img[p + c]
            bad = bad or not 0.0 <= v <= 1.0
            out[c].append(255.0 * (0.0 if v < 0.0 else 1.0 if v > 1.0 else v))
        cov.append(img[p + 3] == 1.0)
        outside += bad and img[p + 3] == 1.0
    return out, cov, outside


def compare_v2(path, args, rep):
    outside = None
    png = path.lower().endswith('.png')
    if png:
        w, h, rows = MO.read_png(path)
        if w != h:
            raise InputError('%s: %dx%d, square expected' % (path, w, h))
        side = {'kind': 'tile', 'width': w, 'height': h, 'sampling': 'production'}
        if args.get('sidecar'):
            side.update(read_json(args['sidecar']))
            check_view(side, args['sidecar'])
        img = tuple([float(px[c]) for row in rows for px in row] for c in range(3))
        cov = None
        size = w
    else:
        side, raw = read_composite(path)
        size = side['width']
        img, cov, outside = image_from(side, raw, size)
    if side.get('kind') != 'tile' or size != R.MAP_SIZE:
        raise InputError('%s: v2 compares a %d-px tile composite (kind "tile"), got kind %r, %d px'
                         % (path, R.MAP_SIZE, side.get('kind'), size))
    if side.get('sampling', 'production') != 'production':
        rep.info('the dump is in %s sampling; v2 is defined on the production composite' % side.get('sampling'))
    inputs = Inputs(side, args, need_map=True)
    label = 'tile %d_%d (%s)' % (inputs.tile + (inputs.client,))
    rep.info(label + ': map texture', inputs.map_how)
    if png and not args.get('sidecar'):
        rep.info(label + ': no sidecar, so the build and md5 of the inputs are not checked')
    else:
        check_inputs(rep, side, inputs, label)
    missing = {}
    mat, tex = inputs.material(missing)
    if missing:
        rep.info(label + ': textures missing from the reference inputs', ', '.join('%d' % k for k in sorted(missing)))
    compare_states(rep, side, mat, label)
    mw, mh, mpx = R.Blp(inputs.map_bytes, 'map texture').decode(0)
    if (mw, mh) != (R.MAP_SIZE, R.MAP_SIZE):
        raise InputError('map texture is %dx%d' % (mw, mh))
    M = R.channels(mpx)
    keep = R.material_keep(mat)
    keep_px = R.pixel_keep(keep)
    mref = [R.block_means(c, R.MAP_SIZE) for c in M]
    tex0_id = inputs.ids.get('tex0FileDataID', side.get('tex0FileDataID'))
    if args.get('threshold'):
        threshold, name = float(args['threshold']), 'given'
    else:
        threshold, name = golden_threshold(inputs.client, inputs.tile, tex0_id)
    if threshold is None:
        raise InputError('%s is not a golden tile; give --threshold' % label)
    if cov is not None:
        uncovered = sum(1 for k, c in zip(keep_px, cov) if k and not c)
        rep.check(uncovered == 0, label + ': every kept pixel covered (A = 1)', '%d of %d not covered' % (uncovered, sum(keep_px)))
    saturated = side.get('saturated')
    if outside is not None:
        if saturated is True:
            rep.check(outside == 0, label + ': saturated dump holds only 0..1', '%d covered pixel(s) outside' % outside)
        else:
            rep.info(label + ': the dump is not saturated (the production view is)', '%d covered pixel(s) outside 0..1' % outside)
    rmse, bias, blocks = R.block_rmse(img, M, keep, ref_means=mref)
    rep.check(rmse <= threshold, label + ': 4x4-block RMSE against the map texture <= %.3f (%s)' % (threshold, name),
              '%.3f over %d blocks, bias %s' % (rmse, blocks, ' '.join('%+.2f' % b for b in bias)))
    table = []
    for s in range(8):
        t = R.transform_image(img, R.MAP_SIZE, s)
        table.append((R.block_rmse(t, M, keep, ref_means=mref)[0], R.SYMMETRIES[s][0]))
    best = min(table)
    rep.check(best[1] == 'identity' and all(v > table[0][0] for v, _ in table[1:]),
              label + ': identity is the best of the 8 symmetries (4x4-block RMSE)',
              '; '.join('%s %.3f' % (n, v) for v, n in table))
    base = R.pixel_rmse(img, M, keep_px)
    shifts = []
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        shifts.append((R.pixel_rmse(R.shift_image(img, R.MAP_SIZE, dx, dy), M, keep_px), dx, dy))
    rep.check(all(v > base for v, _, _ in shifts), label + ': zero phase beats +-1 px (per-pixel RMSE)',
              'zero %.3f; %s' % (base, '; '.join('%+d,%+d %.3f' % (dx, dy, v) for v, dx, dy in shifts)))
    cache = {}
    ref, info = R.map_composite(mat, tex, cache=cache)
    plain, _ = R.map_composite(mat, tex, rule='plain weights', cache=cache)
    plain_rmse = R.block_rmse(plain, M, keep, ref_means=mref)[0]
    rep.info(label + ': the plain-weights composite against the map texture', '%.3f (the design rule fits %.3f)'
             % (plain_rmse, R.block_rmse(ref, M, keep, ref_means=mref)[0]))
    # The per-pixel gate: the reference composed under each named block decode (separate texture sources and caches:
    # the decoded texels differ), and every kept pixel within the limit of the better-fitting one: the float dump's
    # limit, the 8-bit PNG's (the float limit plus its rounding) or the given one.
    if args.get('reference-tolerance'):
        tol, tol_name = float(args['reference-tolerance']), 'given'
    elif png:
        tol, tol_name = V2_PNG_REFERENCE_TOLERANCE, 'V2_PNG_REFERENCE_TOLERANCE: 8-bit PNG input, rounding included'
    else:
        tol, tol_name = V2_REFERENCE_TOLERANCE, 'V2_REFERENCE_TOLERANCE: float dump'
    models = {}
    for model in R.DECODE_MODELS:
        if model == 'exact':
            r_img, r_info = ref, info
        else:
            r_img, r_info = R.map_composite(mat, inputs.texture_source({}, model), cache={})
        worst = max((abs(a - b) for c in range(3) for k, a, b in zip(keep_px, img[c], r_img[c]) if k), default=0.0)
        models[model] = {'max': worst, 'block_rmse': R.block_rmse(img, r_img, keep)[0], 'clipped': r_info['clipped'],
                         'notes': sorted(set(r_info['notes']))}
    best = min(R.DECODE_MODELS, key=lambda m: (models[m]['max'], R.DECODE_MODELS.index(m)))
    detail = '; '.join('%s: per-pixel max %.3f /255, block RMSE %.3f, reference clipped %d px' % (
        MODEL_NAMES[m], models[m]['max'], models[m]['block_rmse'], models[m]['clipped']) for m in R.DECODE_MODELS)
    rep.check(models[best]['max'] <= tol, label + ': every kept pixel within %g/255 (%s) of the reference composite under '
              'the better-fitting named decode (%s)' % (tol, tol_name, MODEL_NAMES[best]),
              '%s; dump clippedPixels %r' % (detail, side.get('clippedPixels')))
    notes = sorted(set(n for m in R.DECODE_MODELS for n in models[m]['notes']))
    if notes:
        rep.info(label + ': reference notes', '; '.join(notes))
    return {'tile': list(inputs.tile), 'client': inputs.client, 'rmse': rmse, 'bias': list(bias), 'blocks': blocks,
            'threshold': threshold, 'symmetries': [(n, v) for v, n in table], 'phase_zero': base,
            'phase': [(dx, dy, v) for v, dx, dy in shifts], 'reference_model': best,
            'reference_block_rmse': models[best]['block_rmse'], 'reference_max': models[best]['max'],
            'reference_tolerance': tol, 'reference_limit': tol_name, 'reference_models': models,
            'plain_weights_rmse': plain_rmse}


# ------------------------------------------------------------------------------------------------- texels

def block_texel_kinds(blp, lv=0):
    """Per texel of a block-compressed level: (colour kind, colour lo, colour hi, alpha kind). Colour kind 'endpoint'
    (the texel selects c0 or c1, or a DXT1 three-colour block's transparent black: every block decoder returns it
    exactly), 'interpolant' (a four-colour block's index 2 or 3: the decoder rounds it its own way, so it must equal
    one named decode -- the exact ramp or the measured hardware model, or for a CPU-decoded upload the rounded
    decode) or 'midpoint' (a DXT1 three-colour block's index 2: no measured GPU model; lo/hi, the per-channel bounds
    of its two endpoints, bound it; the rounded decode models it). Alpha kind 'exact' (DXT1, DXT3's explicit nibbles,
    DXT5's a0, a1 and the 6-value mode's 0 and 255) or 'interpolant' (DXT5's other selectors: a named decode as for
    colour). None for a BLP that is not block-compressed (every texel exact)."""
    if blp.encoding != 2:
        return None
    w, h = blp.level_size(lv)
    b = blp.buf
    o = blp.levels[lv]
    bw, bh = (w + 3) // 4, (h + 3) // 4
    dxt1 = blp.alpha_type == 0
    size = 8 if dxt1 else 16
    kinds = [None] * (w * h)
    for by in range(bh):
        for bx in range(bw):
            p = o + size * (by * bw + bx)
            akind = None
            if not dxt1:
                if blp.alpha_type == 7:
                    a0, a1 = b[p], b[p + 1]
                    abits = int.from_bytes(b[p + 2:p + 8], 'little')
                    akind = []
                    for k in range(16):
                        sel = (abits >> (3 * k)) & 7
                        akind.append('exact' if sel < 2 or (a0 <= a1 and sel >= 6) else 'interpolant')
                p += 8
            c0, c1, bits = struct.unpack_from('<HHI', b, p)
            e0, e1 = R.expand565(c0), R.expand565(c1)
            four = c0 > c1 or not dxt1
            lo = tuple(min(x, y) for x, y in zip(e0, e1))
            hi = tuple(max(x, y) for x, y in zip(e0, e1))
            for k in range(16):
                y, x = by * 4 + k // 4, bx * 4 + k % 4
                if x >= w or y >= h:
                    continue
                sel = (bits >> (2 * k)) & 3
                ckind = 'endpoint' if sel < 2 or (not four and sel == 3) else 'interpolant' if four else 'midpoint'
                kinds[y * w + x] = (ckind, lo, hi, akind[k] if akind is not None else 'exact')
    return kinds


def named_model(n, matches, neither):
    """(ok, name) of `n` interpolated texels of which matches[m] equal named decode m (the decodes accepted for the
    texture: TEXEL_MODEL_LABELS) and `neither` none of them: ok when every one equals the same named decode (named;
    several names when more than one fits every texel), 'mixed' when each equals one, but not the same one."""
    if n == 0:
        return True, 'none interpolated'
    whole = [label for m, label in TEXEL_MODEL_LABELS if matches.get(m) == n]
    if whole:
        return True, whole[0] if len(whole) == 1 else ' = '.join(whole) + ' on every one'
    return False, '%s (%s, neither %d of %d)' % ('neither' if neither else 'mixed', ', '.join(
        '%s %d' % (label, matches[m]) for m, label in TEXEL_MODEL_LABELS if m in matches), neither, n)


def compare_texels(path, args, rep):
    side, tex = read_texels(path)
    client = side.get('client') or args.get('client')
    data = R.AuditData(args['corpus']) if args.get('corpus') else None
    if data and not client:
        raise InputError('%s names no client (give --client)' % path)
    if data:
        want = data.hashes['client_builds'].get(client)
        rep.check(side.get('build') == want, 'texel dump build is the pinned client build',
                  'texel dump %r, pinned %r' % (side.get('build'), want))
    bins = [0.0, 0.5, 1.0, 2.0]
    results = []
    for fd in sorted(tex):
        w, h, vals, scale, ents = tex[fd]
        if data:
            try:
                b = data.read(client, fd)
            except FileNotFoundError:
                rep.check(False, 'texture %d: BLP found for the comparison' % fd, 'not in the audit data')
                continue
            except ValueError as e:
                raise InputError(str(e))
        else:
            p = os.path.join(args.get('textures', ''), '%d.bin' % fd)
            if not os.path.exists(p):
                rep.check(False, 'texture %d: BLP found for the comparison' % fd, p)
                continue
            b = R.read_bytes(p)
        bad = ['%s: %r' % (e.get('role', 'diffuse'), e.get('md5')) for e in ents
               if not (isinstance(e.get('md5'), str) and e['md5'].lower() == R.md5_hex(b))]
        rep.check(not bad, 'texture %d: md5 is the MD5 of the %s' % (fd, 'verified audit file' if data else 'given file'),
                  '; '.join(bad) + ('; file %s' % R.md5_hex(b) if bad else ''))
        raw_bytes = b
        b = R.Blp(raw_bytes, 'texture %d' % fd)
        if not rep.check((b.width, b.height) == (w, h), 'texture %d: mip 0 size' % fd, 'dump %dx%d, BLP %dx%d' % (w, h, b.width, b.height)):
            continue
        _, _, ref = b.decode(0)
        kinds = block_texel_kinds(b, 0)
        uploads = sorted(set(str(e.get('format')) for e in ents))
        decoded = uploads == [R.DECODED_UPLOAD]          # the player decoded the blocks on the CPU (its RGBA32 fallback)
        models = ('rounded',) if decoded else ('hardware', 'exact')
        decodes = {'exact': ref}
        if kinds is not None:
            for m in models:
                if m != 'exact':
                    decodes[m] = R.Blp(raw_bytes, 'texture %d' % fd, m).decode(0)[2]
        tol = TEXEL_ENDPOINT_TOLERANCE
        hist = [0] * (len(bins) + 1)
        worst = [0.0] * 4
        exact_bad = exact_n = 0
        exact_worst = 0.0
        # per kind of interpolant: count, matches and max off per accepted decode, and the count that equal none
        col = {'n': 0, 'neither': 0, 'match': dict((m, 0) for m in models), 'max': dict((m, 0.0) for m in models)}
        alp = {'n': 0, 'neither': 0, 'match': dict((m, 0) for m in models), 'max': dict((m, 0.0) for m in models)}
        mid_n = mid_bad = 0

        def endpoint(d):
            nonlocal exact_n, exact_bad, exact_worst
            exact_n += 1
            if not d <= tol:
                exact_bad += 1
                exact_worst = max(exact_worst, d) if d == d else float('nan')

        def interpolant(acc, t, chans, g):
            acc['n'] += 1
            hit = False
            for m in models:
                dm = max(abs(g[c] - decodes[m][4 * t + c]) for c in chans)
                if dm <= tol:
                    acc['match'][m] += 1
                    hit = True
                acc['max'][m] = max(acc['max'][m], dm) if dm == dm else float('nan')
            acc['neither'] += not hit

        def detail(acc, what):
            return '%d %s: %s within %.2f/255' % (acc['n'], what, ', '.join(
                '%d equal the %s (max off %.3f)' % (acc['match'][m], {'rounded': 'rounded decode'}.get(
                    m, dict(TEXEL_MODEL_LABELS)[m]), acc['max'][m]) for m in models), tol)
        for t in range(w * h):
            g = [vals[4 * t + c] * scale for c in range(4)]
            r = ref[4 * t:4 * t + 4]
            d = [abs(g[c] - r[c]) for c in range(4)]
            for c in range(4):
                if d[c] > worst[c] or d[c] != d[c]:
                    worst[c] = d[c]
                if c < 3:
                    k = 0
                    while k < len(bins) and d[c] > bins[k]:
                        k += 1
                    hist[k] += 1
            if kinds is None:
                for c in range(4):
                    endpoint(d[c])
                continue
            ckind, lo, hi, akind = kinds[t]
            if ckind == 'endpoint':
                for c in range(3):
                    endpoint(d[c])
            elif ckind == 'interpolant' or decoded:       # the rounded decode models a decoded midpoint too
                interpolant(col, t, range(3), g)
            else:
                mid_n += 1
                mid_bad += not all(lo[c] - TEXEL_BOUND_SLACK <= g[c] <= hi[c] + TEXEL_BOUND_SLACK for c in range(3))
            if akind == 'exact':
                endpoint(d[3])
            else:
                interpolant(alp, t, (3,), g)
        label = 'texture %d (%s, %s, upload %s, %dx%d)' % (fd, '/'.join(sorted(texel_roles(tex[fd]))), b.format,
                                                         '/'.join(uploads), w, h)
        rep.check(exact_bad == 0, label + ': %s exact (the stored data are what the GPU returns)' % (
                  'every texel (the BLP is not block-compressed)' if kinds is None else 'endpoint texels'),
                  '%d of %d endpoint/explicit channel values off by more than %.2f/255 (worst %.3f)'
                  % (exact_bad, exact_n, tol, exact_worst))
        res = {'fileDataID': fd, 'roles': sorted(texel_roles(tex[fd])), 'format': b.format, 'upload': uploads,
               'max': worst, 'histogram': hist}
        if kinds is not None:
            to = ('of an %s upload equals the rounded decode' % R.DECODED_UPLOAD if decoded else
                  'equals one named decode')
            if col['n']:
                ok, name = named_model(col['n'], col['match'], col['neither'])
                rep.check(ok, label + ': every %s %s (%s)' % (
                          'interpolated colour (four-colour interpolants, three-colour midpoints)' if decoded else
                          'four-colour interpolant', to, name),
                          '%s; deviation from the exact decode: max R %.3f G %.3f B %.3f /255; R,G,B channels: = 0 %d, '
                          '<= 0.5 %d, <= 1 %d, <= 2 %d, > 2 %d' % ((detail(col, 'interpolants'),) + tuple(worst[:3] + hist)))
                res['colour_model'] = name
            else:
                rep.info(label + ': no interpolated colour (the named-decode check has nothing to check)')
                res['colour_model'] = 'none interpolated'
            if alp['n']:
                ok, name = named_model(alp['n'], alp['match'], alp['neither'])
                rep.check(ok, label + ': every interpolated DXT5 alpha %s (%s)' % (to, name),
                          detail(alp, 'interpolated alphas'))
                res['alpha_model'] = name
            if mid_n:
                rep.check(mid_bad == 0, label + ': DXT1 three-colour midpoints between their endpoints (no measured '
                          'model: %d unmodelled)' % mid_n, '%d of %d outside [lo - %.0f, hi + %.0f]/255'
                          % (mid_bad, mid_n, TEXEL_BOUND_SLACK, TEXEL_BOUND_SLACK))
            res['unmodelled'] = mid_n
            res['interpolants'] = {'colour': {'n': col['n'], 'match': col['match'], 'neither': col['neither']},
                                   'alpha': {'n': alp['n'], 'match': alp['match'], 'neither': alp['neither']}}
        results.append(res)
    return results


# ------------------------------------------------------------------------------------------------- command line

OPTIONS = ('--texels', '--corpus', '--root', '--tex0', '--wdt', '--json', '--map-texture', '--textures', '--threshold',
           '--reference-tolerance', '--tile', '--client', '--sidecar')


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    mode, rest = argv[1], argv[2:]
    args, pos = {}, []
    while rest:
        a = rest.pop(0)
        if a in OPTIONS and rest:
            args[a[2:]] = rest.pop(0)
        elif a.startswith('--'):
            print('unknown option %s' % a)
            return 2
        else:
            pos.append(a)
    rep = Report()
    out = {'mode': mode, 'inputs': pos}
    try:
        if mode == 'v1':
            texel_path = args.get('texels')
            results = []
            texels = None
            for p in pos:
                side, img = read_composite(p)
                tp = texel_path or (os.path.join(os.path.dirname(os.path.abspath(p)), side['texelDump']) if side.get('texelDump') else None)
                if not tp:
                    raise InputError('%s: no texel dump (give --texels)' % p)
                if texels is None or tp != texels[0]:
                    texels = (tp,) + read_texels(tp)
                inputs = Inputs(side, args)
                label = '%s MCNK %r' % (os.path.basename(p), side.get('chunk'))
                print('v1 %s (tile %d_%d %s, %s sampling, %d px)'
                      % ((p,) + inputs.tile + (inputs.client, side.get('sampling'), side['width'])))
                check_inputs(rep, side, inputs, label)
                n = side.get('chunk')
                if isinstance(n, int) and 0 <= n < 256:
                    mat0 = R.build_material(inputs.root, inputs.tex0, inputs.mphd)
                    ids = set(fd for fd, _ in R.needed_textures(mat0, mat0['chunks'][n]))
                    check_texel_inputs(rep, texels[1], texels[2], inputs, label, ids)
                results.append(compare_v1(side, img, texels[2], inputs, rep, label))
            if not pos:
                raise InputError('no composite dump given')
            out['results'] = results
        elif mode == 'v2':
            if len(pos) != 1:
                raise InputError('v2 takes one composite dump')
            print('v2 %s' % pos[0])
            out['results'] = compare_v2(pos[0], args, rep)
        elif mode == 'texels':
            if len(pos) != 1:
                raise InputError('texels takes one texel dump')
            print('texels %s' % pos[0])
            out['results'] = compare_texels(pos[0], args, rep)
        else:
            raise InputError('unknown mode %s' % mode)
    except (InputError, ValueError, IndexError, OSError, KeyError, TypeError, R.MaterialUnavailable, R.BlpError) as e:
        print('error: %s: %s' % (type(e).__name__, e))
        return 2
    print('RESULT: %s (%d checks, %d failed)' % ('PASS' if rep.failed == 0 else 'FAIL',
                                                  sum(1 for c in rep.checks if c['result'] != 'INFO'), rep.failed))
    if args.get('json'):
        out['checks'] = rep.checks
        with open(args['json'], 'w', newline='\n') as f:
            json.dump(out, f, indent=1)
    return 0 if rep.failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
