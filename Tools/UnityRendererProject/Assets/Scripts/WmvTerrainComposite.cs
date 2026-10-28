// WmvTerrainComposite.cs
//
// THE TERRAIN COMPOSITE AS NUMBERS (stage 04 design, sections 4 and 8): validation fixtures, never production. The
// textured chunks are rendered top-down by a CommandBuffer -- no camera, so no frame decode, no HDR intermediate, no
// bloom, no LUT -- into an explicit R16G16B16A16_SFloat target, with _WmvShaderEncode 0 for the render and restored
// after, and read back as floats. The formats are the ones Tools/world-audit/compare_terrain_material.py reads (its
// header is the reference):
//
//   -wmvTerrainCompositeDump=<name>[:<size>]   the tile at size x size (512 by default: 32 pixels per chunk, the game's
//                                              map texture's), <name>.raw (float32 RGBA, north row first, west column
//                                              first), <name>.json (the sidecar) and, up to 2048 px, <name>.png (8-bit,
//                                              clamped)
//   -wmvTerrainCompositeChunk=<n>[,<n>...]     each listed drawable chunk alone at N = max over every texture it binds
//                                              (its layers' diffuse textures and, on an MPHD 0x80 map, their height
//                                              textures) of (texture size x repeats, 0.5 for MTXP scale nibble 4), a
//                                              whole power of two <= 8192 (otherwise
//                                              the chunk is reported "validation grid on texel boundary"),
//                                              <name>_c<n>.raw/.json (and .png up to 2048)
//   -wmvTerrainTexelDump=<name>                every texture the tile uploaded (diffuse and height), mip 0 as the GPU
//                                              decodes it, read by Load (the shader's texel-copy view) into an
//                                              R32G32B32A32_SFloat target, so every texel arrives exactly: <name>.json
//                                              (the client build; per texture its role -- one entry per role it has in
//                                              the tile -- and the md5 of the file received) and <name>.<fileDataID>.raw,
//                                              float32 RGBA, rows as stored
//   -wmvTerrainValidate                        validation sampling (texels by Load at integer indices, the alpha by
//                                              four Loads and float weights) for the composites -- and for every
//                                              frame the player draws while the switch is set
//
// THE AUTHORED COMPOSITE ONLY. The composite dumps are taken in the textured view and refused (logged, nothing written)
// in any other: texturedlit multiplies by the diagnostic light, texturedplainweights is plain weights under a hatch, the
// materials view is state colours. For the render every drawn material's _WmvLinearOut (the A/B display modes' encode),
// _WmvHatch and _WmvTexturedView are pinned to the authored textured composite (0, 0, textured) and put back after, so
// the numbers never depend on the display mode. The sidecar records the client build and the md5s of the WDT, root and
// tex0 the player parsed, so a dump is only ever judged against the same bytes.
//
// SAMPLING AND RANGE. Validation composites are written unclamped (MCCV takes some above 1). Production composites are
// the shader's own saturated output; a second, unclamped render of the same draws counts the pixels the clamp changed
// (clippedPixels). Coverage: alpha 1 where a textured fragment was written, 0 elsewhere (hole cells have no triangles;
// neutral chunks are not drawn into a composite). Back-face culling is off for the render (a heightfield seen from
// straight above has nothing behind it; this keeps the mirrored projection of a texture target from culling it).
//
// ORIENTATION. The camera looks straight down Unity -Y with Unity +Z (north) up and +X (east) right; the readback's
// rows (Texture2D: bottom row first) are reversed into north-first. The self-test proves the orientation against the
// CPU compositor with an asymmetric texture and alpha map (all 8 symmetries).
//
// Every dump goes in bands of rows (at most BandPixels pixels a band), each written as it is read -- the tile composite,
// the chunk renders and the texel dump alike (StreamTexels) -- so memory stays bounded at any size (an 8-bit PNG copy is
// kept only up to 2048 px). Only ReadTexels, the self-test's reader of its own small textures, holds a mip whole.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEngine;
using UnityEngine.Experimental.Rendering;
using UnityEngine.Rendering;
using Wmv.Wow.World;

public static class WmvTerrainComposite
{
    public const int DefaultTileSize = 512;
    /// <summary>Pixels a band of a render reads back at most (8 MiB of half floats).</summary>
    public const int BandPixels = 1 << 20;
    /// <summary>The largest render (pixels per side): the design's 8192.</summary>
    public const int MaxSize = 8192;

    static readonly int EncodeId = Shader.PropertyToID("_WmvShaderEncode");
    static readonly int MatrixV = Shader.PropertyToID("unity_MatrixV"), MatrixP = Shader.PropertyToID("glstate_matrix_projection"),
                        MatrixVP = Shader.PropertyToID("unity_MatrixVP");

    /// <summary>
    /// Render what `draw` draws (it adds draw commands to the buffer), top-down orthographic over x0..x0+extentX (west to
    /// east) and zNorth-extentZ..zNorth, heights yMin..yMax, into width x height float RGBA, and hand the rows north-first
    /// to `rows` (first row, row count, floats: rows x width x 4). validation: the validation sampling global;
    /// unclamped: the composite is not saturated. _WmvShaderEncode is 0 for the render and the globals are put back; so is
    /// _WmvWaterGrab (WmvWaterGrabPass: no grab exists for a command-buffer render -- the water's High path is off unless
    /// `draw` itself sets it, as the self-test's synthetic grab does), and so is _WmvWLOn (WmvWorldLight, Stage 10: the outdoor
    /// light is the main camera's alone, never a fixture's; with WMV_LIGHT=off it is never touched).
    /// The target is R16G16B16A16_SFloat (the design's), or R32G32B32A32_SFloat with `float32` (the texel dump).
    /// `bandRows` (> 0) caps the rows of a band below what BandPixels allows (the self-test's banding check).
    /// </summary>
    public static void Render(Action<CommandBuffer> draw, float x0, float zNorth, float extentX, float extentZ, float yMin, float yMax,
                              int width, int height, bool validation, bool unclamped, Action<int, int, float[]> rows, bool float32 = false,
                              int bandRows = 0)
    {
        if (width <= 0 || height <= 0 || width > MaxSize || height > MaxSize)
            throw new ArgumentOutOfRangeException("width", "a render is 1.." + MaxSize + " pixels per side");
        int band = Math.Max(1, Math.Min(height, BandPixels / width));
        if (bandRows > 0)
            band = Math.Min(band, bandRows);
        float savedEncode = Shader.GetGlobalFloat(EncodeId);
        float savedValidate = Shader.GetGlobalFloat(WmvTerrainMaterials.ValidateId);
        float savedUnclamped = Shader.GetGlobalFloat(WmvTerrainMaterials.UnclampedId);
        float savedGrab = Shader.GetGlobalFloat(WmvWaterGrabPass.WaterGrabId);
        bool pinLight = !WmvWorldLight.HardOff;
        float savedWorldLight = Shader.GetGlobalFloat(WmvWorldLight.OnId);
        var desc = new RenderTextureDescriptor(width, band, float32 ? GraphicsFormat.R32G32B32A32_SFloat : GraphicsFormat.R16G16B16A16_SFloat, 0)
        {
            msaaSamples = 1, useMipMap = false,
        };
        var target = new RenderTexture(desc) { name = "WmvTerrain composite target" };
        target.Create();
        var readback = new Texture2D(width, band, float32 ? TextureFormat.RGBAFloat : TextureFormat.RGBAHalf, false, true)
        {
            name = "WmvTerrain composite readback",
        };
        RenderTexture previous = RenderTexture.active;
        var cmd = new CommandBuffer { name = "WmvTerrain composite" };
        try
        {
            float cx = x0 + extentX * 0.5f, cz = zNorth - extentZ * 0.5f, cy = yMax + 10f;
            float near = 1f, far = (yMax - yMin) + 20f;
            // Camera space as Unity keeps it (looking along -z): x = east, y = north, z = up.
            var view = new Matrix4x4();
            view.SetRow(0, new Vector4(1f, 0f, 0f, -cx));
            view.SetRow(1, new Vector4(0f, 0f, 1f, -cz));
            view.SetRow(2, new Vector4(0f, 1f, 0f, -cy));
            view.SetRow(3, new Vector4(0f, 0f, 0f, 1f));
            float pixelZ = extentZ / height;
            var floats = new float[width * band * 4];
            for (int first = 0; first < height; first += band)
            {
                int count = Math.Min(band, height - first);
                float top = zNorth - first * pixelZ - cz, bottom = top - count * pixelZ;
                Matrix4x4 ortho = Matrix4x4.Ortho(x0 - cx, x0 + extentX - cx, bottom, top, near, far);
                Matrix4x4 gpu = GL.GetGPUProjectionMatrix(ortho, true);
                cmd.Clear();
                cmd.SetRenderTarget(target);
                cmd.SetViewport(new Rect(0, 0, width, count));
                cmd.ClearRenderTarget(false, true, new Color(0f, 0f, 0f, 0f));
                cmd.SetGlobalFloat(EncodeId, 0f);
                cmd.SetGlobalFloat(WmvTerrainMaterials.ValidateId, validation ? 1f : 0f);
                cmd.SetGlobalFloat(WmvTerrainMaterials.UnclampedId, unclamped ? 1f : 0f);
                cmd.SetGlobalFloat(WmvWaterGrabPass.WaterGrabId, 0f);
                if (pinLight)
                    cmd.SetGlobalFloat(WmvWorldLight.OnId, 0f);
                cmd.SetViewProjectionMatrices(view, ortho);
                cmd.SetGlobalMatrix(MatrixV, view);
                cmd.SetGlobalMatrix(MatrixP, gpu);
                cmd.SetGlobalMatrix(MatrixVP, gpu * view);
                draw(cmd);
                cmd.SetGlobalFloat(EncodeId, savedEncode);
                cmd.SetGlobalFloat(WmvTerrainMaterials.ValidateId, savedValidate);
                cmd.SetGlobalFloat(WmvTerrainMaterials.UnclampedId, savedUnclamped);
                cmd.SetGlobalFloat(WmvWaterGrabPass.WaterGrabId, savedGrab);
                if (pinLight)
                    cmd.SetGlobalFloat(WmvWorldLight.OnId, savedWorldLight);
                Graphics.ExecuteCommandBuffer(cmd);
                RenderTexture.active = target;
                readback.ReadPixels(new Rect(0, 0, width, count), 0, 0, false);
                RenderTexture.active = previous;
                // Texture2D rows run bottom up: row count - 1 - i of the band is its north-first row i.
                if (float32)
                {
                    Unity.Collections.NativeArray<float> full = readback.GetRawTextureData<float>();
                    for (int i = 0; i < count; i++)
                    {
                        int src = (count - 1 - i) * width * 4, dst = i * width * 4;
                        for (int k = 0; k < width * 4; k++)
                            floats[dst + k] = full[src + k];
                    }
                }
                else
                {
                    Unity.Collections.NativeArray<ushort> half = readback.GetRawTextureData<ushort>();
                    for (int i = 0; i < count; i++)
                    {
                        int src = (count - 1 - i) * width * 4, dst = i * width * 4;
                        for (int k = 0; k < width * 4; k++)
                            floats[dst + k] = Mathf.HalfToFloat(half[src + k]);
                    }
                }
                if (count == band)
                    rows(first, count, floats);
                else
                {
                    var part = new float[width * count * 4];
                    Array.Copy(floats, part, part.Length);
                    rows(first, count, part);
                }
            }
        }
        finally
        {
            RenderTexture.active = previous;
            Shader.SetGlobalFloat(EncodeId, savedEncode);
            Shader.SetGlobalFloat(WmvTerrainMaterials.ValidateId, savedValidate);
            Shader.SetGlobalFloat(WmvTerrainMaterials.UnclampedId, savedUnclamped);
            Shader.SetGlobalFloat(WmvWaterGrabPass.WaterGrabId, savedGrab);
            if (pinLight)
                Shader.SetGlobalFloat(WmvWorldLight.OnId, savedWorldLight);
            cmd.Release();
            UnityEngine.Object.DestroyImmediate(readback);
            target.Release();
            UnityEngine.Object.DestroyImmediate(target);
        }
    }

    /// <summary>As Render, into one array (north-first rows), for sizes the caller can hold.</summary>
    public static float[] RenderToArray(Action<CommandBuffer> draw, float x0, float zNorth, float extentX, float extentZ, float yMin, float yMax,
                                        int width, int height, bool validation, bool unclamped, bool float32 = false)
    {
        var all = new float[width * height * 4];
        Render(draw, x0, zNorth, extentX, extentZ, yMin, yMax, width, height, validation, unclamped,
               (first, count, f) => Array.Copy(f, 0, all, first * width * 4, count * width * 4), float32);
        return all;
    }

    /// <summary>One drawn material's values a render pins, and what they were.</summary>
    sealed class Pinned
    {
        public Material M;
        public float Cull, LinearOut, Hatch, View;
    }

    /// <summary>The draws of a tile's chunks drawn by Wmv/TerrainTextured (all of them, or chunk `only`). For the render
    /// every drawn material has culling off and _WmvLinearOut 0 (the dumps hold authored values in every display mode) --
    /// and, unless `keepView` (the self-test's plain-weights and state-colour renders), _WmvHatch 0 and _WmvTexturedView
    /// textured, the authored composite; the returned action puts every value back.</summary>
    public static Action<CommandBuffer> ChunkDraws(WmvRuntimeTerrainTile tile, int only, out Action restore, bool keepView = false)
    {
        var draws = new List<Renderer>();
        var pinned = new List<Pinned>();
        for (int n = 0; n < AdtParser.ChunksPerTile; n++)
        {
            if ((only >= 0 && n != only) || !tile.ChunkTextured[n] || tile.ChunkObjects[n] == null)
                continue;
            Renderer r = tile.ChunkObjects[n].GetComponent<Renderer>();
            Material m = tile.ChunkMaterials[n];
            if (r == null || m == null)
                continue;
            draws.Add(r);
            bool listed = false;
            foreach (Pinned p in pinned)
                listed |= p.M == m;
            if (listed)
                continue;
            pinned.Add(new Pinned
            {
                M = m, Cull = m.GetFloat(WmvTerrainMaterials.CullId), LinearOut = m.GetFloat(WmvTerrainMaterials.LinearOutId),
                Hatch = m.GetFloat(WmvTerrainMaterials.HatchId), View = m.GetFloat(WmvTerrainMaterials.ViewId),
            });
            m.SetFloat(WmvTerrainMaterials.CullId, (float)CullMode.Off);
            m.SetFloat(WmvTerrainMaterials.LinearOutId, 0f);
            if (!keepView)
            {
                m.SetFloat(WmvTerrainMaterials.HatchId, 0f);
                m.SetFloat(WmvTerrainMaterials.ViewId, WmvTerrainMaterials.ViewTextured);
            }
        }
        restore = () =>
        {
            foreach (Pinned p in pinned)
                if (p.M != null)
                {
                    p.M.SetFloat(WmvTerrainMaterials.CullId, p.Cull);
                    p.M.SetFloat(WmvTerrainMaterials.LinearOutId, p.LinearOut);
                    p.M.SetFloat(WmvTerrainMaterials.HatchId, p.Hatch);
                    p.M.SetFloat(WmvTerrainMaterials.ViewId, p.View);
                }
        };
        return cmd =>
        {
            foreach (Renderer r in draws)
                cmd.DrawRenderer(r, r.sharedMaterial, 0, 0);
        };
    }

    /// <summary>The square chunk n covers, from its mesh: the north-west vertex (MCVT 0) and the south-east one (MCVT 144).</summary>
    public static bool ChunkRegion(TerrainTileMesh mesh, int n, out float x0, out float zNorth, out float extentX, out float extentZ)
    {
        x0 = zNorth = extentX = extentZ = 0f;
        TerrainChunkMesh c = mesh != null && n >= 0 && n < mesh.Chunks.Length ? mesh.Chunks[n] : null;
        if (c == null || c.Positions == null || c.Positions.Length < 3 * 145)
            return false;
        x0 = c.Positions[0];
        zNorth = c.Positions[2];
        extentX = c.Positions[3 * 144] - x0;
        extentZ = zNorth - c.Positions[3 * 144 + 2];
        return extentX > 0f && extentZ > 0f;
    }

    /// <summary>
    /// The design's validation size of a chunk: N = max over every texture it binds of (texture width or height x the
    /// layer's repeats, a float: 0.5 for MTXP scale nibble 4) -- its layers' diffuse textures with an uploaded texture (a
    /// missing layer is the checker) and, on an MPHD 0x80 map, their height textures -- 64 when none has one. Null with
    /// the reason when N is not a whole power of two within 8192 ("validation grid on texel boundary"). (A layer's UV phase
    /// is 0 or half a repeat, a whole number of texels of any texture of 2 or more texels, so the pixel centres stay off
    /// the texel boundaries.)
    /// </summary>
    public static int? ValidationSize(TerrainChunkMaterial c, Func<uint, Texture2D> texture, out string why)
    {
        why = null;
        float n = 0f;
        for (int k = 0; k < c.Layers.Length && k < TerrainMaterialBuilder.MaxDrawnLayers; k++)
        {
            TerrainMaterialLayer l = c.Layers[k];
            Texture2D t = l.Missing ? null : texture(l.TextureFileDataID);
            if (t != null)
                n = Math.Max(n, Math.Max(t.width, t.height) * l.RepeatsPerChunk);
            Texture2D h = c.UseHeightTextures && l.HeightTextureFileDataID != 0 ? texture(l.HeightTextureFileDataID) : null;
            if (h != null)
                n = Math.Max(n, Math.Max(h.width, h.height) * l.RepeatsPerChunk);
        }
        if (n == 0f)
            n = 64f;
        int whole = n >= 1f && n <= MaxSize && n == (float)Math.Floor(n) ? (int)n : 0;
        if (whole == 0 || (whole & (whole - 1)) != 0)
        {
            why = "validation grid on texel boundary (" + n.ToString("R", System.Globalization.CultureInfo.InvariantCulture) + " texels per chunk)";
            return null;
        }
        return whole;
    }

    // ---------------------------------------------------------------- the dumps

    /// <summary>A dump file's full path: `name` (a bare file name) in the player's folder.</summary>
    public static string PlayerFile(string name)
    {
        return Path.GetFullPath(Path.Combine(Application.dataPath, "../" + name));
    }

    /// <summary>The largest dump that also gets an 8-bit PNG copy (the copy is held whole).</summary>
    public const int MaxPngSize = 2048;

    /// <summary>Why a tile drawn in `view` gives no composite dump, or null for the textured view (the authored composite).</summary>
    public static string DumpRefusal(WmvTerrainView view)
    {
        if (view == WmvTerrainView.Textured)
            return null;
        return "view " + WmvTerrainDebug.ViewNames[(int)view] + " is not the authored composite (use -wmvTerrainView=textured, or no view)";
    }

    /// <summary>
    /// The tile composite at size x size, streamed: north-first bands of float RGBA written to `raw` as they are read
    /// (little-endian float32), and copied into `keepForPng` when given (size x size x 4). Validation sampling is written
    /// unclamped; production is the shader's saturated output and a second, unclamped render counts the pixels the clamp
    /// changed. `bandRows` as Render's. The tile's root is active for the render and the drawn materials pinned
    /// (ChunkDraws), both put back.
    /// </summary>
    public static void RenderTile(WmvRuntimeTerrainTile tile, int size, bool validation, Stream raw, float[] keepForPng, out int clipped,
                                  out int covered, int bandRows = 0)
    {
        float t = (float)TerrainCoordinates.TileSize;
        float yMin = tile.HasBounds ? tile.Bounds.min.y : 0f, yMax = tile.HasBounds ? tile.Bounds.max.y : 0f;
        Action restore;
        Action<CommandBuffer> draws = ChunkDraws(tile, -1, out restore);
        bool wasActive = tile.Root != null && tile.Root.activeSelf;
        if (tile.Root != null) tile.Root.SetActive(true);
        int c = 0, v = 0;
        try
        {
            Render(draws, 0f, 0f, t, t, yMin, yMax, size, size, validation, validation, (first, count, rows) =>
            {
                c += Clipped(rows);
                v += Covered(rows);
                if (raw != null)
                    WriteFloats(raw, rows);
                if (keepForPng != null)
                    Array.Copy(rows, 0, keepForPng, first * size * 4, count * size * 4);
            }, false, bandRows);
            if (!validation)
            {
                // The production render above is the shader's saturated output; the clamp is counted on an unclamped one.
                c = 0;
                Render(draws, 0f, 0f, t, t, yMin, yMax, size, size, false, true, (first, count, rows) => c += Clipped(rows), false, bandRows);
            }
        }
        finally
        {
            restore();
            if (tile.Root != null) tile.Root.SetActive(wasActive);
        }
        clipped = c;
        covered = v;
    }

    /// <summary>-wmvTerrainCompositeDump: the tile composite at `size`, streamed in bands to raw, then json (and png up to
    /// MaxPngSize). Refused, and logged, outside the textured view.</summary>
    public static void WriteTileComposite(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content,
                                          WmvIpcClient.TerrainTileCommand cmd, int rootFileDataID, string name, int size, bool validation,
                                          string texelDump, Action<string> log)
    {
        string refused = DumpRefusal(tile.View);
        if (refused != null)
        {
            if (log != null)
                log("composite dump " + name + ": refused -- " + refused);
            return;
        }
        var clock = System.Diagnostics.Stopwatch.StartNew();
        string raw = name + ".raw", png = size <= MaxPngSize ? name + ".png" : null;
        float[] keepForPng = png != null ? new float[size * size * 4] : null;
        int clipped, covered;
        using (var f = new FileStream(PlayerFile(raw), FileMode.Create, FileAccess.Write))
            RenderTile(tile, size, validation, f, keepForPng, out clipped, out covered);
        if (png != null)
            WritePng(PlayerFile(png), keepForPng, size, size);
        var side = Sidecar(tile, content, cmd, rootFileDataID, "tile", -1, size, size / 16, validation ? "validation" : "production", raw, png,
                           !validation, clipped, texelDump);
        File.WriteAllText(PlayerFile(name + ".json"), side, new UTF8Encoding(false));
        if (log != null)
            log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                "composite dump {0}: tile {1}_{2} at {3} px, {4} sampling{5}, {6} pixel(s) left 0..1 before the clamp, {7} covered; written in {8:F1} ms",
                PlayerFile(name + ".json"), tile.TileX, tile.TileY, size, validation ? "validation" : "production",
                validation ? " (unclamped)" : " (the shader's saturated output)", clipped, covered, clock.Elapsed.TotalMilliseconds));
    }

    /// <summary>-wmvTerrainCompositeChunk: each listed chunk alone at its validation size, as raw and json (and png up to
    /// 2048). Refused, and logged, outside the textured view; a chunk not drawable (so not drawn textured), or whose size
    /// is on a texel boundary, is logged and skipped.</summary>
    public static void WriteChunkComposites(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content,
                                            WmvIpcClient.TerrainTileCommand cmd, int rootFileDataID, string name, IList<int> chunks,
                                            bool validation, string texelDump, Action<string> log)
    {
        string refused = DumpRefusal(tile.View);
        if (refused != null)
        {
            if (log != null)
                log("chunk renders " + name + "_c*: refused -- " + refused);
            return;
        }
        TerrainTileMaterial mat = content != null ? content.Material : null;
        foreach (int n in chunks)
        {
            var clock = System.Diagnostics.Stopwatch.StartNew();
            if (mat == null || mat.Chunks.Length != AdtParser.ChunksPerTile || !mat.Chunks[n].Drawable || !tile.ChunkTextured[n] ||
                tile.ChunkObjects[n] == null)
            {
                if (log != null)
                    log("chunk render " + n + ": not drawn textured (" + (mat != null && mat.Chunks.Length == AdtParser.ChunksPerTile
                        ? TerrainTileMaterial.StateName(mat.Chunks[n].State) + (mat.Chunks[n].Reason != null ? ": " + mat.Chunks[n].Reason : "")
                        : "no material") + ") -- not rendered");
                continue;
            }
            string why;
            int? size = ValidationSize(mat.Chunks[n], id =>
            {
                Texture2D tex;
                return tile.Textures.TryGetValue(id, out tex) ? tex : null;
            }, out why);
            float x0, zNorth, ex, ez;
            if (size == null || !ChunkRegion(mesh, n, out x0, out zNorth, out ex, out ez))
            {
                if (log != null)
                    log("chunk render " + n + ": " + (why ?? "the chunk's mesh gives no square") + " -- not rendered");
                continue;
            }
            int s = size.Value;
            TerrainChunkMesh cm = mesh.Chunks[n];
            float yMin = cm.BoundsMin != null ? cm.BoundsMin[1] : 0f, yMax = cm.BoundsMax != null ? cm.BoundsMax[1] : 0f;
            Action restore;
            Action<CommandBuffer> draws = ChunkDraws(tile, n, out restore);
            bool wasActive = tile.Root != null && tile.Root.activeSelf;
            if (tile.Root != null) tile.Root.SetActive(true);
            string baseName = name + "_c" + n, raw = baseName + ".raw";
            int clipped = 0, covered = 0;
            float[] keepForPng = s <= MaxPngSize ? new float[s * s * 4] : null;
            try
            {
                using (var f = new FileStream(PlayerFile(raw), FileMode.Create, FileAccess.Write))
                    Render(draws, x0, zNorth, ex, ez, yMin, yMax, s, s, validation, validation, (first, count, rows) =>
                    {
                        clipped += Clipped(rows);
                        covered += Covered(rows);
                        WriteFloats(f, rows);
                        if (keepForPng != null)
                            Array.Copy(rows, 0, keepForPng, first * s * 4, count * s * 4);
                    });
                if (!validation)
                {
                    // The production render above is the shader's saturated output; the clamp is counted on an unclamped one.
                    clipped = 0;
                    Render(draws, x0, zNorth, ex, ez, yMin, yMax, s, s, false, true, (first, count, rows) => clipped += Clipped(rows));
                }
            }
            finally
            {
                restore();
                if (tile.Root != null) tile.Root.SetActive(wasActive);
            }
            string png = null;
            if (keepForPng != null)
            {
                png = baseName + ".png";
                WritePng(PlayerFile(png), keepForPng, s, s);
            }
            string side = Sidecar(tile, content, cmd, rootFileDataID, "chunk", n, s, s, validation ? "validation" : "production", raw, png,
                                  !validation, clipped, texelDump);
            File.WriteAllText(PlayerFile(baseName + ".json"), side, new UTF8Encoding(false));
            if (log != null)
                log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                    "chunk render {0}: MCNK {1} ({2}) alone at {3} px, {4} sampling, {5} covered, {6} clipped; written in {7:F1} ms",
                    PlayerFile(baseName + ".json"), n, TerrainTileMaterial.StateName(mat.Chunks[n].State), s,
                    validation ? "validation" : "production", covered, clipped, clock.Elapsed.TotalMilliseconds));
        }
    }

    /// <summary>A flat square over the region (0, 0)..(C, -C) with UV0 (0, 0) at its north-west corner and (8, 8) at its
    /// south-east one -- a chunk's UV layout, for the texel copy. The caller destroys it.</summary>
    public static Mesh TexelQuad(float c)
    {
        var m = new Mesh { name = "WmvTerrain texel quad" };
        m.SetVertices(new[] { new Vector3(0f, 0f, 0f), new Vector3(c, 0f, 0f), new Vector3(0f, 0f, -c), new Vector3(c, 0f, -c) });
        m.SetNormals(new[] { Vector3.up, Vector3.up, Vector3.up, Vector3.up });
        m.SetUVs(0, new[] { new Vector2(0f, 0f), new Vector2(8f, 0f), new Vector2(0f, 8f), new Vector2(8f, 8f) });
        m.SetIndices(new[] { 0, 2, 1, 1, 2, 3 }, MeshTopology.Triangles, 0);
        return m;
    }

    /// <summary>Mip `mip` of texture `t` as the GPU decodes it (Load, the texel-copy view, into a float32 target, so
    /// exactly), handed over in bands as Render reads them: (first row, row count, rows x width x 4 floats), rows as
    /// stored (row 0 = v 0), never the whole mip at once. `bandRows` (> 0) caps a band's rows (the self-test's check).</summary>
    public static void StreamTexels(Texture2D t, int mip, Action<int, int, float[]> rows, int bandRows = 0)
    {
        int w = Math.Max(1, t.width >> mip), h = Math.Max(1, t.height >> mip);
        Shader s = WmvTerrainMaterials.ResolveShader(null);
        if (s == null)
            throw new InvalidOperationException("Resources/" + WmvTerrainMaterials.ShaderResource + ".shader is missing from this player build");
        const float C = 10f;
        Mesh quad = TexelQuad(C);
        Material copy = WmvTerrainMaterials.TexelCopyMaterial(s, t, mip);
        copy.SetFloat(WmvTerrainMaterials.CullId, (float)CullMode.Off);
        try
        {
            Render(cmd => cmd.DrawMesh(quad, Matrix4x4.identity, copy, 0, 0), 0f, 0f, C, C, -1f, 1f, w, h, true, true, rows, true, bandRows);
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(copy);
            UnityEngine.Object.DestroyImmediate(quad);
        }
    }

    /// <summary>As StreamTexels, into one array: w x h float RGBA, rows as stored -- for the self-test's small textures
    /// only (the texel dump streams).</summary>
    public static float[] ReadTexels(Texture2D t, int mip, out int w, out int h)
    {
        int width = Math.Max(1, t.width >> mip), height = Math.Max(1, t.height >> mip);
        w = width;
        h = height;
        var all = new float[width * height * 4];
        StreamTexels(t, mip, (first, count, f) => Array.Copy(f, 0, all, first * width * 4, count * width * 4));
        return all;
    }

    /// <summary>The texel dump's raw file of texture `id` (one per id, whatever its roles).</summary>
    public static string TexelRawName(string name, uint id)
    {
        return name + "." + id.ToString(System.Globalization.CultureInfo.InvariantCulture) + ".raw";
    }

    /// <summary>The texel dump's json: the client and its build, then every texture the tile uploaded (by FileDataID),
    /// one entry per role it has in the tile ("diffuse", "height"; an id the material does not list is "diffuse"; a file
    /// without an alpha channel never has the height role), each with the md5 of the file the player received, its mip 0
    /// size, upload format and raw file.</summary>
    public static string TexelDumpJson(WmvRuntimeTerrainTile tile, WmvTerrainTileContent content, WmvIpcClient.TerrainTileCommand cmd, string name)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"format\":\"wmv-terrain-texels\",\"version\":1,\"source\":\"player\",\"client\":\"").Append(Json(Client(cmd)))
          .Append("\",\"build\":\"").Append(Json(BuildOf(content, cmd))).Append("\",\"textures\":[");
        int written = 0;
        var ids = new List<uint>(tile.Textures.Keys);
        ids.Sort();
        foreach (uint id in ids)
        {
            Texture2D t = tile.Textures[id];
            WmvTerrainTexture info = content != null ? content.Usable(id) : null;
            List<string> roles = content != null ? content.RolesOf(id) : new List<string>();
            if (info != null && !info.AvailableAsHeight)
                roles.Remove(WmvTerrainTileContent.RoleHeight);     // no alpha channel: never read as a height texture
            if (roles.Count == 0)
                roles.Add(WmvTerrainTileContent.RoleDiffuse);
            foreach (string role in roles)
                sb.Append(written++ > 0 ? "," : "").Append("{\"fileDataID\":").Append(id.ToString(inv))
                  .Append(",\"role\":\"").Append(role).Append('"')
                  .Append(",\"md5\":\"").Append(Json(info != null ? info.Md5 : "")).Append('"')
                  .Append(",\"width\":").Append(t.width).Append(",\"height\":").Append(t.height)
                  .Append(",\"format\":\"").Append(Json(t.graphicsFormat.ToString())).Append('"')
                  .Append(",\"encoding\":\"").Append(Json(info != null ? info.Encoding : "")).Append('"')
                  .Append(",\"mips\":").Append(t.mipmapCount)
                  .Append(",\"raw\":\"").Append(Json(TexelRawName(name, id)))
                  .Append("\",\"sampleFormat\":\"float32le\",\"channels\":\"RGBA\",\"rowOrder\":\"stored\"}");
        }
        sb.Append("]}");
        return sb.ToString();
    }

    /// <summary>-wmvTerrainTexelDump: every texture the tile uploaded (diffuse and height), mip 0 as the GPU decodes it, each
    /// streamed to its raw file in bands as they are read (StreamTexels).</summary>
    public static void WriteTexelDump(WmvRuntimeTerrainTile tile, WmvTerrainTileContent content, WmvIpcClient.TerrainTileCommand cmd,
                                      string name, Action<string> log, int bandRows = 0)
    {
        var clock = System.Diagnostics.Stopwatch.StartNew();
        var ids = new List<uint>(tile.Textures.Keys);
        ids.Sort();
        foreach (uint id in ids)
            using (var f = new FileStream(PlayerFile(TexelRawName(name, id)), FileMode.Create, FileAccess.Write))
                StreamTexels(tile.Textures[id], 0, (first, count, rows) => WriteFloats(f, rows), bandRows);
        File.WriteAllText(PlayerFile(name + ".json"), TexelDumpJson(tile, content, cmd, name), new UTF8Encoding(false));
        if (log != null)
            log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                              "texel dump {0}: {1} texture(s), mip 0 read by Load, build {2}; written in {3:F1} ms", PlayerFile(name + ".json"),
                              ids.Count, BuildOf(content, cmd), clock.Elapsed.TotalMilliseconds));
    }

    // ---------------------------------------------------------------- helpers

    /// <summary>The audit data's product folder of the loaded client (the host's product code).</summary>
    static string Client(WmvIpcClient.TerrainTileCommand cmd) { return cmd != null && !string.IsNullOrEmpty(cmd.Product) ? cmd.Product : "unknown"; }

    /// <summary>The client build the load came from (the content's, else the command's), "" when unknown.</summary>
    static string BuildOf(WmvTerrainTileContent content, WmvIpcClient.TerrainTileCommand cmd)
    {
        if (content != null && !string.IsNullOrEmpty(content.Build))
            return content.Build;
        return cmd != null && cmd.Build != null ? cmd.Build : "";
    }

    /// <summary>Pixels where any of R, G, B is outside 0..1.</summary>
    public static int Clipped(float[] px)
    {
        int n = 0;
        for (int i = 0; i + 3 < px.Length; i += 4)
            if (px[i] < 0f || px[i] > 1f || px[i + 1] < 0f || px[i + 1] > 1f || px[i + 2] < 0f || px[i + 2] > 1f)
                n++;
        return n;
    }

    /// <summary>Pixels a fragment was written to (alpha 1).</summary>
    public static int Covered(float[] px)
    {
        int n = 0;
        for (int i = 3; i < px.Length; i += 4)
            if (px[i] == 1f)
                n++;
        return n;
    }

    static void WriteFloats(Stream f, float[] px)
    {
        var bytes = new byte[px.Length * 4];
        Buffer.BlockCopy(px, 0, bytes, 0, bytes.Length);
        if (!BitConverter.IsLittleEndian)
            for (int i = 0; i < bytes.Length; i += 4)
            {
                Array.Reverse(bytes, i, 4);
            }
        f.Write(bytes, 0, bytes.Length);
    }

    /// <summary>An 8-bit RGB PNG of north-first float RGBA (clamped, rounded), north at the top.</summary>
    static void WritePng(string path, float[] px, int width, int height)
    {
        var tex = new Texture2D(width, height, TextureFormat.RGB24, false, true);
        try
        {
            var rgb = new byte[width * height * 3];
            for (int y = 0; y < height; y++)
            {
                int row = height - 1 - y;     // Texture2D rows run bottom up; a PNG's first row is the top
                for (int x = 0; x < width; x++)
                {
                    int s = 4 * (y * width + x), d = 3 * (row * width + x);
                    for (int c = 0; c < 3; c++)
                        rgb[d + c] = (byte)Mathf.RoundToInt(Mathf.Clamp01(px[s + c]) * 255f);
                }
            }
            tex.SetPixelData(rgb, 0);
            tex.Apply(false);
            File.WriteAllBytes(path, tex.EncodeToPNG());
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(tex);
        }
    }

    /// <summary>The composite dump's sidecar (the format of compare_terrain_material.py): the client, its build and the md5s
    /// of the WDT, root and tex0 the player parsed, the tile and its file ids, the render's kind, size and sampling, the
    /// clamp, the view, and the material's states and drawn chunks' layers.</summary>
    public static string Sidecar(WmvRuntimeTerrainTile tile, WmvTerrainTileContent content, WmvIpcClient.TerrainTileCommand cmd, int rootFileDataID,
                                 string kind, int chunk, int size, int pixelsPerChunk, string sampling, string raw, string png, bool saturated,
                                 int clipped, string texelDump)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        TerrainTileMaterial mat = content != null ? content.Material : null;
        var sb = new StringBuilder();
        sb.Append("{\"format\":\"wmv-terrain-composite\",\"version\":1,\"source\":\"player\"")
          .Append(",\"client\":\"").Append(Json(Client(cmd))).Append('"')
          .Append(",\"build\":\"").Append(Json(BuildOf(content, cmd))).Append('"')
          .Append(",\"tile\":[").Append(tile.TileX).Append(',').Append(tile.TileY).Append(']')
          .Append(",\"wdtFileDataID\":").Append(cmd != null ? cmd.WdtFileDataID : 0)
          .Append(",\"rootFileDataID\":").Append(rootFileDataID)
          .Append(",\"tex0FileDataID\":").Append(content != null ? content.Tex0FileDataID : 0)
          .Append(",\"wdtMd5\":\"").Append(Json(content != null ? content.WdtMd5 : "")).Append('"')
          .Append(",\"rootMd5\":\"").Append(Json(content != null ? content.RootMd5 : "")).Append('"')
          .Append(",\"tex0Md5\":\"").Append(Json(content != null ? content.Tex0Md5 : "")).Append('"')
          .Append(",\"mapTextureFileDataID\":").Append(content != null ? content.MapTextureFileDataID : 0)
          .Append(",\"mphdFlags\":").Append((content != null ? content.MphdFlags : 0u).ToString(inv))
          .Append(",\"kind\":\"").Append(kind).Append('"');
        if (chunk >= 0)
            sb.Append(",\"chunk\":").Append(chunk);
        sb.Append(",\"width\":").Append(size).Append(",\"height\":").Append(size)
          .Append(",\"pixelsPerChunk\":").Append(pixelsPerChunk)
          .Append(",\"sampling\":\"").Append(sampling).Append('"')
          .Append(",\"raw\":\"").Append(Json(raw)).Append('"');
        if (png != null)
            sb.Append(",\"png\":\"").Append(Json(png)).Append('"');
        sb.Append(",\"sampleFormat\":\"float32le\",\"channels\":\"RGBA\",\"rowOrder\":\"north-first\",\"colourDomain\":\"authored\"")
          .Append(",\"saturated\":").Append(saturated ? "true" : "false")
          .Append(",\"clippedPixels\":").Append(clipped)
          .Append(",\"view\":\"").Append(WmvTerrainDebug.ViewNames[(int)tile.View]).Append('"');
        if (texelDump != null)
            sb.Append(",\"texelDump\":\"").Append(Json(texelDump + ".json")).Append('"');
        if (mat != null)
        {
            sb.Append(",\"material\":\"").Append(TerrainTileMaterial.TileStateName(mat.State)).Append('"').Append(",\"states\":{");
            int i = 0;
            foreach (KeyValuePair<string, int> kv in mat.CounterPairs())
                if (kv.Key != "shadowMapIgnored" && kv.Key != "vertexColorMismatch")
                    sb.Append(i++ > 0 ? "," : "").Append('"').Append(kv.Key).Append("\":").Append(kv.Value);
            sb.Append('}');
            if (mat.Chunks.Length == AdtParser.ChunksPerTile)
            {
                sb.Append(",\"chunks\":[");
                int written = 0;
                for (int n = 0; n < AdtParser.ChunksPerTile; n++)
                {
                    if (chunk >= 0 && n != chunk)
                        continue;
                    TerrainChunkMaterial c = mat.Chunks[n];
                    sb.Append(written++ > 0 ? "," : "").Append("{\"n\":").Append(n).Append(",\"state\":\"").Append(c.State.ToString()).Append('"');
                    if (c.Drawable || c.PlainDiagnostic)
                    {
                        sb.Append(",\"layers\":[");
                        for (int k = 0; k < c.Layers.Length && k < TerrainMaterialBuilder.MaxDrawnLayers; k++)
                            sb.Append(k > 0 ? "," : "").Append("{\"fileDataID\":").Append(c.Layers[k].TextureFileDataID.ToString(inv))
                              .Append(",\"repeats\":").Append(c.Layers[k].RepeatsPerChunk.ToString("R", inv)).Append('}');
                        sb.Append(']');
                    }
                    sb.Append('}');
                }
                sb.Append(']');
            }
        }
        sb.Append('}');
        return sb.ToString();
    }

    static string Json(string s)
    {
        if (s == null) return "";
        var sb = new StringBuilder(s.Length);
        foreach (char c in s)
        {
            if (c == '"' || c == '\\') sb.Append('\\').Append(c);
            else if (c < 0x20 || c > 0x7e) sb.Append("\\u").Append(((int)c).ToString("x4"));
            else sb.Append(c);
        }
        return sb.ToString();
    }
}
