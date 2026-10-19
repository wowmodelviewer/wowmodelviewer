// WmvTerrainMaterials.cs
//
// THE TERRAIN TEXTURE MATERIAL (stage 04 design, sections 1 and 4): the pure material data of
// Wow/World/TerrainMaterial.cs (TerrainMaterialBuilder: chunk states, layers, alpha slices, vertex colours) and the
// BLP textures the host serves become Unity objects here. WmvTerrainSession fetches and prepares; WmvTerrainBuilder
// calls the builders below while it builds a tile, and tracks every object the moment it exists.
//
//   - TEXTURES (WmvTerrainTexture): one per FileDataID per tile, whatever role the tile gives it -- a layer's DIFFUSE
//     texture (MDID) or, on an MPHD 0x80 map, a layer's HEIGHT texture (MHID; the height mix reads its alpha); an id
//     with both roles (never observed) is one file, fetched and uploaded once. Prepared OFF the main thread once its
//     bytes are in --
//     md5 against the host's content key, then BlpDecoder.ReadHeader: a DXT1/DXT3/DXT5 file keeps its bytes and its
//     stored mip table and is uploaded NATIVELY (GraphicsFormat RGBA_DXT1/3/5_UNorm, never sRGB: the blocks and the
//     file's own mips exactly as stored); a palettized or raw BGRA file is decoded per stored mip with BlpDecoder to
//     RGBA32 (R8G8B8A8_UNorm). A block-compressed file the GPU cannot take as blocks (format not sampled, or a size
//     that is not whole 4x4 blocks) is decoded to RGBA32 too, with BlpDecoder's EXACT block mode (bit-replicated 565
//     endpoints, interpolants rounded to the nearest 8-bit value), and its detail says so. Rows go up as stored, never
//     flipped (WmvModelBuilder.CreateTexture flips, so it is not used): row 0 is v 0, the north edge of every repeat.
//     Uploaded only in BuildTile: Repeat, trilinear, anisoLevel LayerAnisoLevel, mip bias 0 (Unity's default, never
//     set), Apply(false, makeNoLongerReadable: true). Until then the prepared data live in the load's job (managed
//     memory, dropped with it). A HEIGHT texture is read for its alpha: a file whose BLP header declares no alpha
//     (alphaDepth 0) cannot serve as one -- its height role fails ("decode failed", NoAlphaChannel), its chunks become
//     HeightTextureMissing, and a diffuse role of the same id (never observed) stays usable.
//   - THE ALPHA ARRAY: one Texture2DArray per tile, 64 x 64 R8G8B8A8_UNorm, one slice per chunk drawn textured (layers
//     1..4 in R, G, B, A) and a second one for a chunk of more than 5 layers (layers 5..7 in R, G, B;
//     TerrainTileMaterial.BuildAlphaArray), rows as stored, Clamp, bilinear, anisoLevel 0, no mips.
//   - MATERIALS: one Wmv/TerrainTextured material per chunk drawn textured (up to 8 layers), every value in the material
//     (no MaterialPropertyBlock): its layer textures, repeats (8 / 2^n, a float: 0.5 for MTXP scale nibble 4), UV phases
//     (TerrainMaterialLayer.PhaseU/PhaseV: 0 for whole repeats), missing flags, alpha slice(s), whether the mesh carries
//     vertex colours, and the height mix's inputs -- whether the tile's MPHD has 0x80, each layer's height texture and its
//     MTXP height scale and offset. A layer slot without a texture holds the tile's SAMPLER CARRIER
//     (CreateSamplerCarrier): the shader samples height texture k with layer slot k's sampler.
//
// The game's terrain anisotropy is UNKNOWN; LayerAnisoLevel (9) is what every other texture of this player is sampled
// with today (the project forces anisotropic filtering on, which raises levels 1..9 to 9), so terrain matches the model
// path. It has no effect on a top-down view (the validation renders and the 512-pixel composite).
//
// Every state a texture can end in is a string the report carries verbatim: the host's ("available", "not in client
// storage", "not installed locally", "unreadable", "unsupported", "not checked", "stale", "refused") or the player's own
// (StateNoTextureId .. StateTransportFailure below).

using System;
using System.Collections.Generic;
using System.Diagnostics;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using Wmv.Wow;
using Wmv.Wow.World;

/// <summary>Which block-compressed formats this GPU samples (asked once, on the main thread).</summary>
public struct WmvTerrainFormatSupport
{
    public bool Dxt1, Dxt3, Dxt5;

    static bool queried;
    static WmvTerrainFormatSupport cached;

    /// <summary>SystemInfo's answer for RGBA_DXT1/3/5_UNorm sampling, asked the first time (main thread only).</summary>
    public static WmvTerrainFormatSupport Query()
    {
        if (!queried)
        {
            queried = true;
            cached = new WmvTerrainFormatSupport
            {
                Dxt1 = SystemInfo.IsFormatSupported(GraphicsFormat.RGBA_DXT1_UNorm, GraphicsFormatUsage.Sample),
                Dxt3 = SystemInfo.IsFormatSupported(GraphicsFormat.RGBA_DXT3_UNorm, GraphicsFormatUsage.Sample),
                Dxt5 = SystemInfo.IsFormatSupported(GraphicsFormat.RGBA_DXT5_UNorm, GraphicsFormatUsage.Sample),
            };
        }
        return cached;
    }

    public bool Supports(BlpPixelFormat f)
    {
        return f == BlpPixelFormat.Dxt1 ? Dxt1 : f == BlpPixelFormat.Dxt3 ? Dxt3 : f == BlpPixelFormat.Dxt5 && Dxt5;
    }

    public override string ToString()
    {
        return string.Format("RGBA_DXT1_UNorm {0}, RGBA_DXT3_UNorm {1}, RGBA_DXT5_UNorm {2}", Dxt1 ? "yes" : "NO", Dxt3 ? "yes" : "NO",
                             Dxt5 ? "yes" : "NO");
    }
}

/// <summary>
/// One terrain texture of a load: what became of it (the report's entry) and, from its preparation until the tile is
/// built, the data to upload. Prepare runs on a worker thread; Upload on the main thread, inside BuildTile.
/// </summary>
public sealed class WmvTerrainTexture
{
    public const string StateAvailable = "available";
    /// <summary>The player's own states (design section 2).</summary>
    public const string StateNoTextureId = "no texture id", StateNotRequested = "not requested", StateMd5Mismatch = "md5 mismatch",
                        StateDecodeFailed = "decode failed", StateTransportFailure = "transport failure";
    /// <summary>Asked for and not yet answered or not yet prepared, and listed but not yet asked for (both only in the report
    /// of a load that ended before its textures were settled, never in a "built" one).</summary>
    public const string StateRequested = "requested", StateQueued = "queued";
    /// <summary>The detail of a height role whose file declares no alpha channel (its state is StateDecodeFailed).</summary>
    public const string NoAlphaChannel = "no alpha channel (BLP alphaDepth 0); the height is the alpha";
    /// <summary>The detail's suffix of a block-compressed file decoded to RGBA32 (BlpDecoder's exact block mode).</summary>
    public const string ExactBlocksNote = " (bit-replicated endpoints, interpolants rounded)";

    public uint FileDataID;
    public string State = StateQueued, Detail = "";
    public long Bytes = -1;
    public string Md5 = "";
    public int Width = -1, Height = -1, Mips = -1;
    /// <summary>The upload format (a GraphicsFormat name), "" until prepared.</summary>
    public string Format = "";
    /// <summary>The BLP's own encoding name (dxt1, dxt3, dxt5, palettized/aN, bgra8888).</summary>
    public string Encoding = "";
    /// <summary>The BLP header's alpha depth (0, 1, 4 or 8 bits), -1 until prepared. 0: the file has no alpha channel, so it
    /// cannot serve as a height texture (the height is the alpha).</summary>
    public int AlphaDepth = -1;
    public double RoundTripMs = -1, PrepareMs = -1, UploadMs = -1;
    /// <summary>The GPU bytes of the upload (every mip as uploaded), 0 before it.</summary>
    public long GpuBytes;

    GraphicsFormat graphicsFormat;
    byte[] source;            // block-compressed: the file's bytes, the levels at mipOffsets
    int[] mipOffsets;
    int[] mipBytes;
    byte[][] decoded;         // palettized / raw: RGBA32 per stored mip
    int[] mipWidths, mipHeights;

    /// <summary>The bytes the prepared data hold (0 once uploaded or released).</summary>
    public long PreparedBytes
    {
        get
        {
            long n = source != null ? source.Length : 0;
            if (decoded != null)
                foreach (byte[] m in decoded)
                    n += m != null ? m.Length : 0;
            return n;
        }
    }

    /// <summary>Prepared and still holding the data to upload.</summary>
    public bool Ready { get { return State == StateAvailable && (source != null || decoded != null); } }
    /// <summary>Available: answered, verified and prepared (uploaded or not).</summary>
    public bool Available { get { return State == StateAvailable; } }
    /// <summary>Available and its file has an alpha channel: it can serve as a height texture.</summary>
    public bool AvailableAsHeight { get { return Available && AlphaDepth != 0; } }
    /// <summary>Still in a request state (asked and not yet answered or prepared, or not yet asked): not settled.</summary>
    public bool Unsettled { get { return State == StateRequested || State == StateQueued; } }
    public GraphicsFormat GraphicsFormat { get { return graphicsFormat; } }

    public void Fail(string state, string detail)
    {
        State = state;
        Detail = detail ?? "";
        ReleaseUploadData();
    }

    /// <summary>Streaming (stage 08): this texture as a tile that binds the scene's upload of it sees it -- available, the
    /// facts it was prepared with, no data (nothing to upload, UploadMs -1).</summary>
    public WmvTerrainTexture ResidentCopy()
    {
        return new WmvTerrainTexture
        {
            FileDataID = FileDataID, State = StateAvailable, Detail = "resident: uploaded for another tile of the streaming session",
            Bytes = Bytes, Md5 = Md5, Width = Width, Height = Height, Mips = Mips, Format = Format, Encoding = Encoding,
            AlphaDepth = AlphaDepth, GpuBytes = GpuBytes, graphicsFormat = graphicsFormat,
        };
    }

    /// <summary>Drop the prepared data (after the upload, or with the load).</summary>
    public void ReleaseUploadData()
    {
        source = null;
        decoded = null;
    }

    /// <summary>
    /// Worker thread: verify the bytes against the host's md5 (its content key; "md5 mismatch" when they differ) and
    /// prepare the upload (BlpDecoder.ReadHeader; "decode failed" with the decoder's words when the file is refused).
    /// Block-compressed files whose format the GPU samples, at a size of whole blocks, go up as stored; every other
    /// file is decoded per stored mip to RGBA32 -- a block-compressed one with BlpDecoder's exact block mode, and the
    /// detail says why and how ("...; decoded to RGBA32 (bit-replicated endpoints, interpolants rounded)").
    /// </summary>
    public void Prepare(byte[] bytes, string hostMd5, WmvTerrainFormatSupport support)
    {
        var sw = Stopwatch.StartNew();
        try
        {
            Bytes = bytes != null ? bytes.Length : 0;
            if (bytes == null)
            {
                Fail(StateTransportFailure, "the answer carries no bytes");
                return;
            }
            Md5 = WmvTerrainSession.Md5Hex(bytes);
            if (!string.IsNullOrEmpty(hostMd5) && !string.Equals(hostMd5, Md5, StringComparison.OrdinalIgnoreCase))
            {
                Fail(StateMd5Mismatch, string.Format("the bytes received hash to {0}, the host served {1}", Md5, hostMd5));
                return;
            }
            BlpHeader h;
            try { h = BlpDecoder.ReadHeader(bytes); }
            catch (WowParseException e)
            {
                Fail(StateDecodeFailed, e.Message);
                return;
            }
            Width = h.Width;
            Height = h.Height;
            Mips = h.MipCount;
            Encoding = h.Encoding;
            AlphaDepth = h.AlphaDepth;
            int n = h.MipCount;
            mipWidths = new int[n];
            mipHeights = new int[n];
            for (int m = 0; m < n; m++)
            {
                mipWidths[m] = h.Mips[m].Width;
                mipHeights[m] = h.Mips[m].Height;
            }
            string fallback = null;
            if (h.IsBlockCompressed)
            {
                if (!support.Supports(h.Format))
                    fallback = "this GPU does not sample " + FormatOf(h.Format) + "; decoded to RGBA32" + ExactBlocksNote;
                else if (h.Width % 4 != 0 || h.Height % 4 != 0)
                    fallback = string.Format("{0}x{1} is not whole 4x4 blocks; decoded to RGBA32", h.Width, h.Height) + ExactBlocksNote;
                else
                {
                    graphicsFormat = FormatOf(h.Format);
                    mipOffsets = new int[n];
                    mipBytes = new int[n];
                    long gpu = 0;
                    for (int m = 0; m < n; m++)
                    {
                        mipOffsets[m] = h.Mips[m].Offset;
                        mipBytes[m] = h.Mips[m].DataSize;
                        gpu += h.Mips[m].DataSize;
                    }
                    source = bytes;
                    GpuBytes = gpu;
                    Format = graphicsFormat.ToString();
                    State = StateAvailable;
                    Detail = "";
                    return;
                }
            }
            graphicsFormat = GraphicsFormat.R8G8B8A8_UNorm;
            decoded = new byte[n][];
            long rgba = 0;
            for (int m = 0; m < n; m++)
            {
                BlpImage img;
                // A block-compressed file in the exact block mode (the format's own decode, rounded once); palettized and raw
                // files decode the same in either mode.
                try { img = BlpDecoder.Decode(bytes, m, h.IsBlockCompressed); }
                catch (WowParseException e)
                {
                    Fail(StateDecodeFailed, "mip " + m + ": " + e.Message);
                    return;
                }
                decoded[m] = img.Rgba;
                rgba += img.Rgba.Length;
            }
            GpuBytes = rgba;
            Format = graphicsFormat.ToString();
            State = StateAvailable;
            Detail = fallback ?? "";
        }
        catch (Exception e)
        {
            Fail(StateDecodeFailed, e.GetType().Name + ": " + e.Message);
        }
        finally
        {
            PrepareMs = sw.Elapsed.TotalMilliseconds;
        }
    }

    public static GraphicsFormat FormatOf(BlpPixelFormat f)
    {
        switch (f)
        {
            case BlpPixelFormat.Dxt1: return GraphicsFormat.RGBA_DXT1_UNorm;
            case BlpPixelFormat.Dxt3: return GraphicsFormat.RGBA_DXT3_UNorm;
            case BlpPixelFormat.Dxt5: return GraphicsFormat.RGBA_DXT5_UNorm;
            default: return GraphicsFormat.R8G8B8A8_UNorm;
        }
    }

    /// <summary>
    /// Main thread, inside BuildTile: the Texture2D of the prepared data -- every stored mip set as stored (rows not
    /// flipped), Repeat, trilinear, anisoLevel WmvTerrainMaterials.LayerAnisoLevel, then Apply(false, true): the CPU copy
    /// goes and the prepared data are dropped. track is called with the texture as soon as it exists, before any pixel
    /// data is set, so a texture whose upload throws is still the tile's and is destroyed with it (nothing is created
    /// when it throws before the constructor).
    /// </summary>
    public Texture2D Upload(Action<Texture2D> track)
    {
        if (!Ready)
            throw new InvalidOperationException("texture " + FileDataID + " has nothing to upload (" + State + ")");
        var sw = Stopwatch.StartNew();
        int n = Mips;
        var flags = TextureCreationFlags.DontInitializePixels | (n > 1 ? TextureCreationFlags.MipChain : TextureCreationFlags.None);
        var tex = new Texture2D(Width, Height, graphicsFormat, n, flags) { name = "WmvTerrain texture " + FileDataID };
        track(tex);
        for (int m = 0; m < n; m++)
        {
            if (source != null)
                tex.SetPixelData(source, m, mipOffsets[m]);
            else
                tex.SetPixelData(decoded[m], m, 0);
        }
        tex.wrapMode = TextureWrapMode.Repeat;
        tex.filterMode = FilterMode.Trilinear;
        tex.anisoLevel = WmvTerrainMaterials.LayerAnisoLevel;
        tex.Apply(false, true);
        ReleaseUploadData();
        UploadMs = sw.Elapsed.TotalMilliseconds;
        return tex;
    }

    /// <summary>Stage 09 (a liquid's frames, WmvLiquidRegistry): the prepared data as element `element` of `array`, which the
    /// caller made with this texture's size, graphics format and mip count -- every stored mip set as stored (rows not
    /// flipped, as Upload). The prepared data stay (the caller releases them once every array that needs them has them).</summary>
    public void UploadSlice(Texture2DArray array, int element)
    {
        if (!Ready)
            throw new InvalidOperationException("texture " + FileDataID + " has nothing to upload (" + State + ")");
        for (int m = 0; m < Mips; m++)
        {
            if (source != null)
                array.SetPixelData(source, m, element, mipOffsets[m]);
            else
                array.SetPixelData(decoded[m], m, element, 0);
        }
    }

    /// <summary>Stage 09 (a liquid's frame list whose frames differ in format or mip count, WmvLiquidRegistry): the prepared
    /// data as RGBA32 (R8G8B8A8_UNorm) with the first `mips` stored levels -- a block-compressed file decoded per level with
    /// BlpDecoder's exact block mode, as Prepare's own fallback decodes it, an RGBA32 one cut to those levels -- so the list's
    /// frames go up as one array. False, nothing changed, when it holds no data, has fewer levels, or a level cannot be
    /// decoded.</summary>
    public bool ConvertToRgba(int mips)
    {
        if (!Ready || mips < 1 || mips > Mips)
            return false;
        if (source == null && decoded.Length == mips)
            return true;
        var d = new byte[mips][];
        long rgba = 0;
        for (int m = 0; m < mips; m++)
        {
            if (source != null)
            {
                try { d[m] = BlpDecoder.Decode(source, m, true).Rgba; }
                catch (WowParseException) { return false; }
            }
            else
                d[m] = decoded[m];
            rgba += d[m].Length;
        }
        bool wasBlocks = source != null;
        source = null;
        mipOffsets = null;
        mipBytes = null;
        decoded = d;
        Mips = mips;
        graphicsFormat = GraphicsFormat.R8G8B8A8_UNorm;
        Format = graphicsFormat.ToString();
        GpuBytes = rgba;
        Detail = (Detail.Length > 0 ? Detail + "; " : "") + (wasBlocks ? "decoded to RGBA32" + ExactBlocksNote : "cut to " + mips + " level(s)") +
                 " to share an array with frames of another format or mip count";
        return true;
    }

    /// <summary>The prepared RGBA32 of mip m (palettized / raw only; the self-test reads it), or null.</summary>
    public byte[] DecodedMip(int m) { return decoded != null && m >= 0 && m < decoded.Length ? decoded[m] : null; }

    /// <summary>The report's entry of this texture under one of its roles ("diffuse" or "height"). The height role of an
    /// available file without an alpha channel is "decode failed" (NoAlphaChannel), never uploaded in that role; its
    /// diffuse role, if it has one, is reported as it is.</summary>
    public WmvIpcClient.TerrainTextureReport ToReport(string role)
    {
        bool noAlpha = role == WmvTerrainTileContent.RoleHeight && Available && !AvailableAsHeight;
        return new WmvIpcClient.TerrainTextureReport
        {
            FileDataID = (int)Math.Min(FileDataID, int.MaxValue), Role = role ?? WmvTerrainTileContent.RoleDiffuse,
            State = noAlpha ? StateDecodeFailed : State, Detail = noAlpha ? NoAlphaChannel : Detail, Bytes = Bytes, Md5 = Md5,
            Width = Width, Height = Height, Format = Format, Mips = Mips, UploadMs = noAlpha ? -1 : UploadMs,
        };
    }
}

/// <summary>What a load hands BuildTile besides the mesh: the tile's material (always given: Unavailable when the load
/// has no tex0) and its textures by FileDataID (prepared, failed or not requested), and the files it came from (ids,
/// the md5s of the bytes parsed, the client build) for the dumps' provenance.</summary>
public sealed class WmvTerrainTileContent
{
    public const string RoleDiffuse = "diffuse", RoleHeight = "height";

    public TerrainTileMaterial Material;
    public Dictionary<uint, WmvTerrainTexture> Textures = new Dictionary<uint, WmvTerrainTexture>();
    public int Tex0FileDataID, MapTextureFileDataID;
    public uint MphdFlags;
    /// <summary>The client build the host named (loadTerrainTile's build), "" when unknown.</summary>
    public string Build = "";
    /// <summary>MD5 (lower-case hex) of the WDT, root and tex0 bytes the load parsed; "" for a file it did not have.</summary>
    public string WdtMd5 = "", RootMd5 = "", Tex0Md5 = "";
    /// <summary>Stage 09: the tile's liquid as the worker built it (null: none, or liquids not offered) and the scene's liquid
    /// registry the tile draws its types from (null: liquids not offered). Kept with the content, so a rebuild in another
    /// view draws the liquid again from them.</summary>
    public TerrainLiquidTile LiquidTile;
    public WmvLiquidRegistry Liquids;

    /// <summary>The roles `id` has in the tile's material: "diffuse" (a layer texture of the drawable or plain-diagnostic
    /// chunks: TextureIds, PlainOnlyTextureIds) and/or "height" (HeightTextureIds: an MPHD 0x80 map's MHID textures of
    /// the drawable chunks), in that order; empty for id 0 or an id the material does not list.</summary>
    public List<string> RolesOf(uint id)
    {
        var roles = new List<string>(2);
        TerrainTileMaterial m = Material;
        if (m == null || id == 0)
            return roles;
        if (Array.IndexOf(m.TextureIds, id) >= 0 || Array.IndexOf(m.PlainOnlyTextureIds, id) >= 0)
            roles.Add(RoleDiffuse);
        if (Array.IndexOf(m.HeightTextureIds, id) >= 0)
            roles.Add(RoleHeight);
        return roles;
    }

    /// <summary>
    /// The textures of the load settled: the material's chunks that use one that cannot be had (any failure state, the
    /// player's own included) are marked -- a failed DIFFUSE texture's layers draw the missing-texture checker and its
    /// Textured chunks become MissingTexture (a chunk in a state above it keeps that state: a LayerEffectStatic chunk
    /// stays LayerEffectStatic and draws the checker too; TerrainTileMaterial.ApplyTextureFailures), a failed HEIGHT
    /// texture makes the drawable chunks of an MPHD 0x80 map that read it HeightTextureMissing (drawn neutral;
    /// ApplyHeightTextureFailures); an id with both roles fails in both. An available file without an alpha channel
    /// (BLP alphaDepth 0) fails in its HEIGHT role only (the height is the alpha), its diffuse role stays usable. Only
    /// ids in Textures count (a texture the view never fetched is no failure), and only ANSWERED ones: a texture still
    /// requested or queued (the report of a load that ended before adoption) is no failure yet -- Unsettled counts them.
    /// The failed ids come back (diffuse, height) for the log. Idempotent: each call starts again from the states the
    /// material was built with.
    /// </summary>
    public void SettleFailures(out List<uint> diffuse, out List<uint> height)
    {
        diffuse = new List<uint>();
        height = new List<uint>();
        TerrainTileMaterial m = Material;
        if (m == null || m.State == TerrainTileMaterialState.Unavailable)
            return;
        foreach (KeyValuePair<uint, WmvTerrainTexture> kv in Textures)
        {
            WmvTerrainTexture t = kv.Value;
            if (kv.Key == 0 || t.Unsettled || t.AvailableAsHeight)
                continue;
            foreach (string role in RolesOf(kv.Key))
            {
                if (role == RoleHeight)
                    height.Add(kv.Key);
                else if (!t.Available)
                    diffuse.Add(kv.Key);
            }
        }
        diffuse.Sort();
        height.Sort();
        m.ApplyTextureFailures(diffuse);
        m.ApplyHeightTextureFailures(height);
    }

    /// <summary>The drawable chunks that draw the missing-texture checker, whatever their state (MissingTexture, and a
    /// LayerEffectStatic chunk, which keeps its state by the contract's precedence): `inPlace` those with a layer whose
    /// texture is one of `failedDiffuse` (`staticInPlace` of them LayerEffectStatic), `all` those with any missing layer
    /// (layers naming id 0 included). For the settle log.</summary>
    public void CheckerChunks(ICollection<uint> failedDiffuse, out int inPlace, out int staticInPlace, out int all)
    {
        inPlace = staticInPlace = all = 0;
        if (Material == null)
            return;
        foreach (TerrainChunkMaterial c in Material.Chunks)
        {
            if (!c.Drawable)
                continue;
            bool any = false, failedHere = false;
            foreach (TerrainMaterialLayer l in c.Layers)
                if (l.Missing)
                {
                    any = true;
                    failedHere |= failedDiffuse != null && failedDiffuse.Contains(l.TextureFileDataID);
                }
            if (any) all++;
            if (failedHere)
            {
                inPlace++;
                if (c.State == TerrainMaterialState.LayerEffectStatic) staticInPlace++;
            }
        }
    }

    /// <summary>The textures (distinct ids other than 0) still requested or queued: not settled by SettleFailures.</summary>
    public int Unsettled()
    {
        int n = 0;
        foreach (KeyValuePair<uint, WmvTerrainTexture> kv in Textures)
            if (kv.Key != 0 && kv.Value.Unsettled)
                n++;
        return n;
    }

    /// <summary>The material is textured or partial: the default view draws it textured.</summary>
    public bool HasDrawableChunks
    {
        get { return Material != null && (Material.State == TerrainTileMaterialState.Textured || Material.State == TerrainTileMaterialState.Partial); }
    }

    /// <summary>The uploadable texture of `id`, or null (failed, not requested, id 0).</summary>
    public WmvTerrainTexture Usable(uint id)
    {
        WmvTerrainTexture t;
        return id != 0 && Textures.TryGetValue(id, out t) && t.Available ? t : null;
    }

    /// <summary>The texture of `id` in its HEIGHT role: Usable and with an alpha channel (BLP alphaDepth not 0), or null.</summary>
    public WmvTerrainTexture UsableAsHeight(uint id)
    {
        WmvTerrainTexture t = Usable(id);
        return t != null && t.AvailableAsHeight ? t : null;
    }
}

public static class WmvTerrainMaterials
{
    /// <summary>The layer textures' anisotropy (the game's is UNKNOWN; see the file header).</summary>
    public const int LayerAnisoLevel = 9;
    /// <summary>The textured shader under Assets/Resources (never stripped from a player build).</summary>
    public const string ShaderResource = "WmvTerrainTextured";
    public const string ShaderName = "Wmv/TerrainTextured";

    // _WmvTexturedView values (Resources/WmvTerrainTextured.shader).
    public const float ViewTextured = 0f, ViewLit = 1f, ViewPlainWeights = 2f, ViewStateColour = 3f, ViewTexelCopy = 4f;

    /// <summary>The layers the shader draws: _WmvLayer0..7 and _WmvHeight0..7 (TerrainMaterialBuilder.MaxDrawnLayers).</summary>
    public const int ShaderLayers = 8;

    public static readonly int[] LayerIds = PropertyIds("_WmvLayer");
    /// <summary>The layers' height textures (MHID; the height mix reads their alpha; production samples height texture k
    /// with layer slot k's sampler).</summary>
    public static readonly int[] HeightIds = PropertyIds("_WmvHeight");
    public static readonly int LayerId0 = LayerIds[0];
    // Per-layer values, four to a vector: [0] = P (layers 0..3 in x, y, z, w), [1] = P47 (layers 4..7). LayerValue reads one.
    public static readonly int[] RepeatsIds = VectorPair("_WmvRepeats"), PhaseUIds = VectorPair("_WmvPhaseU"),
                                 PhaseVIds = VectorPair("_WmvPhaseV"), MissingIds = VectorPair("_WmvMissing"),
                                 HasHeightIds = VectorPair("_WmvHasHeight"), HeightScaleIds = VectorPair("_WmvHeightScale"),
                                 HeightOffsetIds = VectorPair("_WmvHeightOffset");
    public static readonly int UseHeightsId = Shader.PropertyToID("_WmvUseHeights");
    public static readonly int AlphaId = Shader.PropertyToID("_WmvAlpha"), AlphaSliceId = Shader.PropertyToID("_WmvAlphaSlice"),
                               AlphaSlice2Id = Shader.PropertyToID("_WmvAlphaSlice2"),
                               LayerCountId = Shader.PropertyToID("_WmvLayerCount"), ViewId = Shader.PropertyToID("_WmvTexturedView"),
                               HatchId = Shader.PropertyToID("_WmvHatch"), VertexColorsId = Shader.PropertyToID("_WmvVertexColors"),
                               StateColourId = Shader.PropertyToID("_WmvStateColour"), TexelMipId = Shader.PropertyToID("_WmvTexelMip"),
                               LinearOutId = Shader.PropertyToID("_WmvLinearOut"), CullId = Shader.PropertyToID("_Cull");

    static int[] PropertyIds(string prefix)
    {
        var ids = new int[ShaderLayers];
        for (int k = 0; k < ShaderLayers; k++)
            ids[k] = Shader.PropertyToID(prefix + k);
        return ids;
    }

    static int[] VectorPair(string name)
    {
        return new[] { Shader.PropertyToID(name), Shader.PropertyToID(name + "47") };
    }

    /// <summary>Layer k's value of a per-layer vector pair (RepeatsIds, PhaseUIds, ...) in material `m`.</summary>
    public static float LayerValue(Material m, int[] pair, int k)
    {
        return m.GetVector(pair[k / 4])[k % 4];
    }

    static void SetLayerValues(Material m, int[] pair, float[] v)
    {
        m.SetVector(pair[0], new Vector4(v[0], v[1], v[2], v[3]));
        m.SetVector(pair[1], new Vector4(v[4], v[5], v[6], v[7]));
    }
    /// <summary>The validation globals (WmvTerrainComposite sets and restores them).</summary>
    public static readonly int ValidateId = Shader.PropertyToID("_WmvTerrainValidate"), UnclampedId = Shader.PropertyToID("_WmvTerrainUnclamped");

    static Shader shader;
    static bool shaderResolved;

    /// <summary>Resources/WmvTerrainTextured.shader, once per session; null when the player was built without it.</summary>
    public static Shader ResolveShader(Action<string> log)
    {
        if (shaderResolved)
            return shader;
        shaderResolved = true;
        Shader s = Resources.Load<Shader>(ShaderResource);
        if (s != null && s.name != null && s.name.StartsWith("Hidden/", StringComparison.Ordinal))
            s = null;
        if (s != null && !s.isSupported)
        {
            if (log != null)
                log("terrain textured shader '" + s.name + "' is not supported on this GPU -- chunks are drawn neutral");
            s = null;
        }
        shader = s;
        if (log != null)
            log(s != null ? "terrain textured shader '" + s.name + "' (Resources/" + ShaderResource + "); " + WmvTerrainFormatSupport.Query()
                          : "Resources/" + ShaderResource + ".shader is missing from this player build -- no terrain chunk can be drawn textured");
        return shader;
    }

    /// <summary>The material state's colour in the materials view (the legend WmvTerrainBuilder logs).</summary>
    public static Color StateColour(TerrainMaterialState s)
    {
        switch (s)
        {
            case TerrainMaterialState.Malformed: return new Color(0.85f, 0.10f, 0.10f, 1f);
            case TerrainMaterialState.AlphaUnsupported: return new Color(0.95f, 0.55f, 0.10f, 1f);
            case TerrainMaterialState.LayerCountUnsupported: return new Color(0.95f, 0.90f, 0.20f, 1f);
            case TerrainMaterialState.LayerEffectUnresolved: return new Color(0.20f, 0.55f, 0.95f, 1f);
            case TerrainMaterialState.HeightTextureMissing: return new Color(0.55f, 0.25f, 0.80f, 1f);
            case TerrainMaterialState.NoLayers: return new Color(0.50f, 0.50f, 0.50f, 1f);
            case TerrainMaterialState.LayerEffectStatic: return new Color(0.10f, 0.85f, 0.85f, 1f);
            case TerrainMaterialState.MissingTexture: return new Color(0.95f, 0.35f, 0.75f, 1f);
            default: return new Color(0.25f, 0.75f, 0.30f, 1f);
        }
    }

    /// <summary>The materials view's colour of a tile without a material (no tex0, or none offered).</summary>
    public static readonly Color NoMaterialColour = new Color(0.12f, 0.12f, 0.14f, 1f);

    /// <summary>The legend, one line: state = colour (0..255) for every state, and the colour of no material.</summary>
    public static string Legend()
    {
        var parts = new List<string>();
        foreach (TerrainMaterialState s in Enum.GetValues(typeof(TerrainMaterialState)))
            parts.Add(TerrainTileMaterial.StateName(s) + " " + Rgb(StateColour(s)));
        parts.Add("no material " + Rgb(NoMaterialColour));
        return string.Join(", ", parts.ToArray());
    }

    static string Rgb(Color c)
    {
        return string.Format("({0},{1},{2})", Mathf.RoundToInt(c.r * 255f), Mathf.RoundToInt(c.g * 255f), Mathf.RoundToInt(c.b * 255f));
    }

    /// <summary>
    /// The tile's alpha array: `slices` slices of 64 x 64 R8G8B8A8_UNorm from `bytes` (TerrainTileMaterial.BuildAlphaArray,
    /// AlphaSliceBytes per slice, rows as stored), Clamp, bilinear, anisoLevel 0, no mips, no CPU copy. Tracked by the
    /// caller the moment it exists.
    /// </summary>
    public static Texture2DArray BuildAlphaArray(byte[] bytes, int slices, Action<Texture2DArray> track)
    {
        if (slices <= 0)
            throw new ArgumentOutOfRangeException("slices", "an alpha array has at least one slice");
        if (bytes == null || bytes.Length < slices * TerrainChunkMaterial.AlphaSliceBytes)
            throw new ArgumentException("the alpha array needs " + slices + " slices of " + TerrainChunkMaterial.AlphaSliceBytes + " bytes");
        var a = new Texture2DArray(64, 64, slices, GraphicsFormat.R8G8B8A8_UNorm, TextureCreationFlags.DontInitializePixels, 1)
        {
            name = "WmvTerrain alpha (" + slices + " slices)",
        };
        track(a);
        for (int s = 0; s < slices; s++)
            a.SetPixelData(bytes, 0, s, s * TerrainChunkMaterial.AlphaSliceBytes);
        a.wrapMode = TextureWrapMode.Clamp;
        a.filterMode = FilterMode.Bilinear;
        a.anisoLevel = 0;
        a.Apply(false, true);
        return a;
    }

    /// <summary>
    /// A tile's SAMPLER CARRIER: a 4 x 4 R8G8B8A8_UNorm texture with a full mip chain and the layer textures' sampler
    /// settings (Repeat, trilinear, anisoLevel LayerAnisoLevel, mip bias 0 -- WmvTerrainTexture.Upload's), mid grey, no
    /// CPU copy. ChunkMaterial binds it into every layer slot that holds no texture (a missing layer: its colour is the
    /// checker, so the carrier is never drawn), because the shader samples height texture k with layer slot k's sampler
    /// (D3D11's 16 samplers cannot hold one per height texture): an empty slot would hand that height read Unity's default
    /// texture's sampler. One per tile, made by WmvTerrainBuilder and released with the tile: `hold` gets it the moment it
    /// exists (before any pixel data is set), so the caller destroys it whatever happens after.
    /// </summary>
    public static Texture2D CreateSamplerCarrier(Action<Texture2D> hold)
    {
        const int Size = 4, Mips = 3;
        var t = new Texture2D(Size, Size, GraphicsFormat.R8G8B8A8_UNorm, Mips, TextureCreationFlags.MipChain)
        {
            name = "WmvTerrain sampler carrier (never drawn)",
        };
        hold(t);
        for (int m = 0; m < Mips; m++)
        {
            int w = Math.Max(1, Size >> m);
            var grey = new byte[w * w * 4];
            for (int i = 0; i < grey.Length; i++)
                grey[i] = (byte)((i & 3) == 3 ? 255 : 128);
            t.SetPixelData(grey, m, 0);
        }
        t.wrapMode = TextureWrapMode.Repeat;
        t.filterMode = FilterMode.Trilinear;
        t.anisoLevel = LayerAnisoLevel;
        t.Apply(false, true);
        return t;
    }

    /// <summary>
    /// One chunk's textured material: its layers (at most ShaderLayers = 8; more is refused) with their textures, repeats,
    /// UV phases and missing flags (a layer is drawn as the checker when the chunk material says Missing or its texture
    /// cannot be had), its alpha slice and -- more than 5 layers -- its second slice (layers 5..7), whether the mesh
    /// carries vertex colours, and the height mix's inputs: _WmvUseHeights = the tile's MPHD 0x80
    /// (TerrainChunkMaterial.UseHeightTextures), each layer's height texture where it names one (from `texture`, the same
    /// per-id lookup) and its MTXP height scale and offset. Every layer slot without a texture gets `samplerCarrier` (see
    /// CreateSamplerCarrier; required). `view` is ViewTextured, ViewLit or ViewPlainWeights; `plainWeights` draws the
    /// labelled plain-weights diagnostic (no cutoff, no heights, hatched) instead of the height mix. Throws (before
    /// anything is created) when a height-mix chunk's layer names a height texture `heightTexture` (the height role's
    /// lookup; `texture` when not given) cannot give: such a chunk is HeightTextureMissing, never drawn with the height
    /// mix; and when a chunk of more than 5 layers has no second alpha slice. A layer whose repeats are unresolved
    /// (RepeatsPerChunk 0: an MTXP scale nibble above 4, only on a LayerEffectUnresolved chunk, so only in the
    /// plain-weights diagnostic) is drawn as the missing-texture checker, never as one texel stretched over the chunk.
    /// </summary>
    public static Material ChunkMaterial(Shader s, TerrainChunkMaterial c, Func<uint, Texture2D> texture, Texture2DArray alpha,
                                         Texture2D samplerCarrier, float view, bool plainWeights, Func<uint, Texture2D> heightTexture = null)
    {
        if (samplerCarrier == null)
            throw new ArgumentNullException("samplerCarrier", "a layer slot without a texture needs the tile's sampler carrier");
        int n = c.Layers.Length;
        if (n > ShaderLayers)
            throw new ArgumentException("MCNK " + c.Index + " has " + n + " layers; Wmv/TerrainTextured draws at most " + ShaderLayers);
        if (n > 1 + TerrainChunkMaterial.LayersPerSlice && alpha != null && c.AlphaSlice2 < 0)
            throw new InvalidOperationException("MCNK " + c.Index + " has " + n + " layers but no second alpha slice (layers 5..7)");
        bool heights = !plainWeights && c.UseHeightTextures;
        var heightTextures = new Texture2D[ShaderLayers];
        if (heightTexture == null)
            heightTexture = texture;
        for (int k = 0; k < n && heights; k++)
        {
            uint hid = c.Layers[k].HeightTextureFileDataID;
            if (hid == 0)
                continue;
            heightTextures[k] = heightTexture(hid);
            if (heightTextures[k] == null)
                throw new InvalidOperationException("MCNK " + c.Index + " layer " + k + ": height texture " + hid +
                                                    " is not uploaded; the chunk cannot be drawn with the height mix");
        }
        var m = new Material(s) { name = "WmvTerrain MCNK " + c.Index + " (" + TerrainTileMaterial.StateName(c.State) + ")" };
        var repeats = new float[ShaderLayers];
        var phaseU = new float[ShaderLayers];
        var phaseV = new float[ShaderLayers];
        var missing = new float[ShaderLayers];
        var has = new float[ShaderLayers];
        var scale = new float[ShaderLayers];
        var offset = new float[ShaderLayers];
        for (int k = 0; k < ShaderLayers; k++)
        {
            repeats[k] = 8f;
            offset[k] = 1f;
            if (k >= n)
                continue;
            TerrainMaterialLayer l = c.Layers[k];
            repeats[k] = l.RepeatsPerChunk;
            phaseU[k] = l.PhaseU;
            phaseV[k] = l.PhaseV;
            Texture2D t = l.Missing || l.RepeatsPerChunk <= 0 ? null : texture(l.TextureFileDataID);
            if (t == null)
            {
                missing[k] = 1f;
                m.SetTexture(LayerIds[k], samplerCarrier);     // the slot's sampler for its height texture; never drawn
            }
            else
                m.SetTexture(LayerIds[k], t);
            if (!heights)
                continue;
            scale[k] = l.HeightScale;
            offset[k] = l.HeightOffset;
            if (heightTextures[k] != null)
            {
                has[k] = 1f;
                m.SetTexture(HeightIds[k], heightTextures[k]);
            }
        }
        m.SetFloat(LayerCountId, n);
        SetLayerValues(m, RepeatsIds, repeats);
        SetLayerValues(m, PhaseUIds, phaseU);
        SetLayerValues(m, PhaseVIds, phaseV);
        SetLayerValues(m, MissingIds, missing);
        m.SetFloat(UseHeightsId, heights ? 1f : 0f);
        SetLayerValues(m, HasHeightIds, has);
        SetLayerValues(m, HeightScaleIds, scale);
        SetLayerValues(m, HeightOffsetIds, offset);
        if (alpha != null)
            m.SetTexture(AlphaId, alpha);
        m.SetFloat(AlphaSliceId, Math.Max(0, c.AlphaSlice));
        m.SetFloat(AlphaSlice2Id, Math.Max(0, c.AlphaSlice2));
        m.SetFloat(VertexColorsId, c.VertexColors != null ? 1f : 0f);
        m.SetFloat(ViewId, view);
        m.SetFloat(HatchId, plainWeights ? 1f : 0f);
        m.SetFloat(LinearOutId, WmvModelBuilder.AuthoredTextureDomain ? 0f : 1f);
        return m;
    }

    /// <summary>A materials-view material: the state's flat colour under the diagnostic light.</summary>
    public static Material StateMaterial(Shader s, string state, Color colour)
    {
        var m = new Material(s) { name = "WmvTerrain state " + state };
        m.SetFloat(ViewId, ViewStateColour);
        // A vector, not a colour: a material colour is converted from sRGB in a linear project, and this one is authored.
        m.SetVector(StateColourId, (Vector4)colour);
        m.SetFloat(LinearOutId, WmvModelBuilder.AuthoredTextureDomain ? 0f : 1f);
        return m;
    }

    /// <summary>A texel-copy material (the texel dump): layer 0 = `t`, read at mip `mip` by Load.</summary>
    public static Material TexelCopyMaterial(Shader s, Texture2D t, int mip)
    {
        var m = new Material(s) { name = "WmvTerrain texel copy" };
        m.SetTexture(LayerId0, t);
        m.SetFloat(ViewId, ViewTexelCopy);
        m.SetFloat(TexelMipId, mip);
        return m;
    }

    /// <summary>UV0 of the 145 MCVT vertices in cell units (TerrainMaterialBuilder.VertexUv), the same for every chunk.</summary>
    public static Vector2[] ChunkUv()
    {
        var uv = new Vector2[TerrainVertexLayout.VertexCount];
        for (int k = 0; k < uv.Length; k++)
        {
            float u, v;
            TerrainMaterialBuilder.VertexUv(k, out u, out v);
            uv[k] = new Vector2(u, v);
        }
        return uv;
    }

    /// <summary>The 145 vertex colours of a chunk (MCCV / 127 as float RGB, alpha 1), or null without MCCV.</summary>
    public static Color[] ChunkColors(TerrainChunkMaterial c)
    {
        if (c.VertexColors == null)
            return null;
        var col = new Color[TerrainVertexLayout.VertexCount];
        for (int k = 0; k < col.Length; k++)
            col[k] = new Color(c.VertexColors[3 * k], c.VertexColors[3 * k + 1], c.VertexColors[3 * k + 2], 1f);
        return col;
    }

    /// <summary>Bytes the material adds to a chunk's vertices: UV0 (2 floats) and, with MCCV, a float RGBA colour.</summary>
    public static long VertexBytes(bool colours)
    {
        return TerrainVertexLayout.VertexCount * (8L + (colours ? 16L : 0L));
    }
}
