// TerrainLight.cs
//
// THE TERRAIN'S OUTDOOR LIGHT (Stage 10 step 5, docs/world-viewer/10-light-sky-fog.md 2.7, 4.0, 4.5, 6.10, 6.15): the file's
// chunk normals (MCNR) and the client's terrain light, in pure C# (no UnityEngine). WmvTerrainBuilder uploads the decoded
// normals as a vertex stream of their own (TEXCOORD1), so the geometry normals, the composite and every mesh dump stay as
// they were; Resources/WmvTerrainTextured.shader's world-lit branch evaluates the light; the lifecycle self-test compares the
// GPU with Shade; Tests/SkyTerrainLightTests.cs checks the decode on synthetic bytes and, with WMV_MCNR_TILES, against the
// geometry normals of the four Southfury tiles.
//
//   MCNR         145 x 3 signed bytes per chunk in MCVT order, / 127 (INFERRED (public); the vectors it gives on the four
//                Southfury tiles are 0.979 to 1.000 long, median 0.994, p1 0.989 -- MEASURED over all 148,480 -- and the light
//                uses them at that length, so the scale reaches the colour, about 0.6 % at the median); the bytes (b0, b1,
//                b2) are the game frame's (X north, Y west, Z up), so Unity's are (-b1, b2, b0) / 127 (WorldToUnity.Direction)
//                -- the earlier survey's fit, mean 1.95 deg from the geometry normals and continuous across tile seams within
//                0.64 deg (MEASURED; the runner proves it again). The 13 bytes after the 435 are not read. A chunk without
//                MCNR takes its geometry normals (TerrainChunkMesh.Normals) and the builder says so.
//   the light    the client's terrain program (Retail 2977247, the HEMISPHERE variant: the shape PROVEN_BYTES, the variant
//                and the column mapping MEASURED on the Stage 09 PTR shots in step 1):
//                  albedo = the layer composite x MCCV x 2 (MCCV as UNorm bytes / 255, the client's factor; 0.5 without MCCV,
//                           INFERRED), NOT saturated before the light
//                  H      = N.U >= 0 ? lerp(Horizon, Ambient, N.U) : lerp(Horizon, Ground, -N.U)
//                           (HorizonAmbientColor or GroundAmbientColor 0 -> AmbientColor: INFERRED (public))
//                  A      = H x (0.7 + 0.4 x (0.5 + 0.5 x N.L))       (N.L not saturated: PROVEN_BYTES, MEASURED 0.710 /
//                           0.907 / 1.104 at N.L -1 / 0 / 1)
//                  D      = Direct x max(N.L, 0) x attenuation (1) x shadow (1)
//                  colour = sqrt((albedo x (A + D))^2 + albedo^2 x local (0)) + specular (0) + second light (0)
//                N = the MCNR normal as the vertices carry it, interpolated and NOT normalised (the client's bytes: 2977247
//                p_1 dots v3 as it comes, and the vertex program 2977850 passes it on without normalising), U = world up, L =
//                towards the light (WorldLightModel's table, MEASURED within 2.5 deg at three times). Held neutral: the
//                attenuation (1: consistent with the measured scale 1), the shadow (1: no cast shadows in Stage 10, plan
//                6.15 -- the dawn and dusk shots show the game's on about 90 % of the light-facing
//                terrain), the local lights (0) and the second directional light (0: no residual needs them), the specular
//                (0: its scale is UNKNOWN; its colour is DirectColor's hue, MEASURED). The shadow factor is an input of its
//                own, applied where the client applies it, so a shadow stage feeds it without a change here.
//                The FLAT variant (-wmvTerrainVariant=flat, for the record only: the measurement rejects it, fit 0.10-0.12
//                against 0.023): colour = albedo x sat(local (0) + Direct x max(N.L, 0) + Ambient) x (0.7 + 0.3 x shadow (1)).
// Thread safety: no mutable static state.

using System;

namespace Wmv.Wow.World
{
    /// <summary>The chunk normals of the file (see the file header).</summary>
    public static class TerrainMcnr
    {
        /// <summary>The bytes' scale: a component is byte / 127 (INFERRED (public); the axis fit MEASURED). The light uses the
        /// decoded vector at its own length (median 0.994 on the Southfury tiles, MEASURED), so the scale reaches the colour.</summary>
        public const double Scale = 127.0;
        public const string ScaleGrade = "INFERRED (public)";
        public const string AxisGrade = "MEASURED";
        /// <summary>Bytes per chunk the decode reads: 145 triplets.</summary>
        public const int Bytes = 3 * TerrainVertexLayout.VertexCount;

        /// <summary>MCVT entry k of a chunk's MCNR as a Unity vector: (-b1, b2, b0) / 127.</summary>
        public static void Unity(sbyte[] raw, int k, out float x, out float y, out float z)
        {
            x = (float)(-raw[3 * k + 1] / Scale);
            y = (float)(raw[3 * k + 2] / Scale);
            z = (float)(raw[3 * k] / Scale);
        }

        /// <summary>The chunk's light normals into `dst` (145 x 3, Unity): the MCNR decode when the chunk carries MCNR
        /// (true), else its geometry normals (false; zero when the chunk has none either).</summary>
        public static bool Fill(TerrainChunkMesh chunk, float[] dst)
        {
            if (dst == null || dst.Length < Bytes)
                throw new ArgumentException("needs room for 145 x 3 floats", "dst");
            sbyte[] raw = chunk != null ? chunk.McnrRaw : null;
            if (raw != null && raw.Length >= Bytes)
            {
                for (int k = 0; k < TerrainVertexLayout.VertexCount; k++)
                    Unity(raw, k, out dst[3 * k], out dst[3 * k + 1], out dst[3 * k + 2]);
                return true;
            }
            if (chunk != null && chunk.Normals != null && chunk.Normals.Length >= Bytes)
                Array.Copy(chunk.Normals, dst, Bytes);
            else
                Array.Clear(dst, 0, Bytes);
            return false;
        }
    }

    /// <summary>The client's terrain light (see the file header), the CPU twin of WmvTerrainTextured's world-lit branch.</summary>
    public static class TerrainOutdoorLight
    {
        /// <summary>The wrap A = H (WrapBase + WrapScale (WrapHalf + WrapHalf N.L)) (PROVEN_BYTES, 2977247; MEASURED).</summary>
        public const double WrapBase = 0.7, WrapScale = 0.4, WrapHalf = 0.5;
        public const string WrapGrade = "PROVEN_BYTES";
        /// <summary>The albedo's vertex-colour factor (the client's x 2 of a UNorm byte / 255; PROVEN_BYTES and MEASURED) and
        /// the vertex colour a chunk without MCCV takes (0.5: INFERRED).</summary>
        public const double VertexColourFactor = 2.0, NoVertexColour = 0.5;
        /// <summary>The flat variant's shadow mix 0.7 + 0.3 shadow (PROVEN_BYTES).</summary>
        public const double FlatShadowBase = 0.7, FlatShadowScale = 0.3;
        /// <summary>The terms held neutral in Stage 10 (see the file header).</summary>
        public const double Attenuation = 1.0, Shadow = 1.0, Local = 0.0, Specular = 0.0, SecondLight = 0.0;

        static double Sat(double x) { return x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x; }

        static bool Zero(double[] c) { return c[0] == 0.0 && c[1] == 0.0 && c[2] == 0.0; }

        /// <summary>The colour of a terrain pixel: albedo (r, g, b, not saturated), the normal n (Unity, used at its own length
        /// as the client does: the interpolated MCNR / 127 vector, not normalised), the light direction l (towards the light,
        /// Unity, unit), the colours (authored, r g b), the variant.</summary>
        public static void Shade(double[] albedo, double[] n, double[] l, double[] ambient, double[] horizon, double[] ground, double[] direct,
                                 bool flat, double[] rgb)
        {
            double nx = n[0], ny = n[1], nz = n[2];
            double nl = nx * l[0] + ny * l[1] + nz * l[2];
            double lit = Math.Max(nl, 0.0);
            if (flat)
            {
                for (int c = 0; c < 3; c++)
                    rgb[c] = albedo[c] * Sat(Local + direct[c] * lit + ambient[c]) * (FlatShadowBase + FlatShadowScale * Shadow);
                return;
            }
            double nu = ny;                                          // U = world up (Unity +y)
            double[] hz = Zero(horizon) ? ambient : horizon;
            double[] gr = Zero(ground) ? ambient : ground;
            double wrap = WrapBase + WrapScale * (WrapHalf + WrapHalf * nl);
            for (int c = 0; c < 3; c++)
            {
                double h = nu >= 0.0 ? hz[c] + (ambient[c] - hz[c]) * nu : hz[c] + (gr[c] - hz[c]) * (-nu);
                double a = h * wrap;
                double d = direct[c] * lit * Attenuation * Shadow;
                double x = albedo[c] * (a + d);
                rgb[c] = Math.Sqrt(x * x + albedo[c] * albedo[c] * Local) + Specular + SecondLight;
            }
        }
    }
}
