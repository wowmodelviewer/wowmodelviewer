// M2ShaderTable.cs
//
// Which combiner a material uses, and where each of its texture units takes its coordinates from.
//
// A modern M2 material does not describe its own blending arithmetic. It carries a shader id, and
// that id plus the number of textures the batch loads selects a Blizzard "combiner shader" by
// name -- one name for the pixel side (how the textures are combined) and one for the vertex side
// (which UV set, or the environment sphere map, feeds each unit). Without this step a
// multi-texture material looks like a single-texture one and every unit past the first is
// silently dropped.
//
// The tables below are the same ones the legacy OpenGL renderer resolves in
// Source/games/wow/WoWModel.cpp, kept name-for-name so the two renderers classify a material
// identically. Resolving a name costs nothing and changes no rendering on its own; what a
// renderer then does with the result is its own business (see WmvModelBuilder, which implements
// the subset this milestone needs and says so in the log when it meets one it does not).
//
// Deliberately free of UnityEngine references, like the rest of this folder.

using System.Collections.Generic;

namespace Wmv.Wow
{
    public static class M2ShaderTable
    {
        /// <summary>Set in a shader id when the low bits index SHADER_ARRAY directly.</summary>
        public const int ExplicitShaderBit = 0x8000;

        struct ShaderEntry
        {
            public readonly string PS, VS;
            public ShaderEntry(string ps, string vs) { PS = ps; VS = vs; }
        }

        // index == (shaderId & 0x7FFF) when the 0x8000 "explicit shader" bit is set
        static readonly ShaderEntry[] ShaderArray =
        {
            new ShaderEntry("Combiners_Opaque_Mod2xNA_Alpha",           "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_AddAlpha",                "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_AddAlpha_Alpha",          "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_Mod2xNA_Alpha_Add",       "Diffuse_T1_Env_T1"),
            new ShaderEntry("Combiners_Mod_AddAlpha",                   "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_AddAlpha",                "Diffuse_T1_T1"),
            new ShaderEntry("Combiners_Mod_AddAlpha",                   "Diffuse_T1_T1"),
            new ShaderEntry("Combiners_Mod_AddAlpha_Alpha",             "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_Alpha_Alpha",             "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_Mod2xNA_Alpha_3s",        "Diffuse_T1_Env_T1"),
            new ShaderEntry("Combiners_Opaque_AddAlpha_Wgt",            "Diffuse_T1_T1"),
            new ShaderEntry("Combiners_Mod_Add_Alpha",                  "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_ModNA_Alpha",             "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Mod_AddAlpha_Wgt",               "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Mod_AddAlpha_Wgt",               "Diffuse_T1_T1"),
            new ShaderEntry("Combiners_Opaque_AddAlpha_Wgt",            "Diffuse_T1_T2"),
            new ShaderEntry("Combiners_Opaque_Mod_Add_Wgt",             "Diffuse_T1_Env"),
            new ShaderEntry("Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T1"),
            new ShaderEntry("Combiners_Mod_Dual_Crossfade",             "Diffuse_T1"),
            new ShaderEntry("Combiners_Mod_Depth",                      "Diffuse_EdgeFade_T1"),
            new ShaderEntry("Combiners_Opaque_Mod2xNA_Alpha_Alpha",     "Diffuse_T1_Env_T2"),
            new ShaderEntry("Combiners_Mod_Mod",                        "Diffuse_EdgeFade_T1_T2"),
            new ShaderEntry("Combiners_Mod_Masked_Dual_Crossfade",      "Diffuse_T1_T2"),
            new ShaderEntry("Combiners_Opaque_Alpha",                   "Diffuse_T1_T1"),
            new ShaderEntry("Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T2"),
            new ShaderEntry("Combiners_Mod_Depth",                      "Diffuse_EdgeFade_Env"),
            new ShaderEntry("Guild",                                    "Diffuse_T1_T2_T1"),
            new ShaderEntry("Guild_NoBorder",                           "Diffuse_T1_T2"),
            new ShaderEntry("Guild_Opaque",                             "Diffuse_T1_T2_T1"),
            new ShaderEntry("Illum",                                    "Diffuse_T1_T1"),
            new ShaderEntry("Combiners_Mod_Mod_Mod_Const",              "Diffuse_T1_T2_T3"),
            new ShaderEntry("Combiners_Mod_Mod_Mod_Const",              "Color_T1_T2_T3"),
            new ShaderEntry("Combiners_Opaque",                         "Diffuse_T1"),
            new ShaderEntry("Combiners_Mod_Mod2x",                      "Diffuse_EdgeFade_T1_T2"),
            new ShaderEntry("Combiners_Mod",                            "Diffuse_EdgeFade_T1"),
            new ShaderEntry("Combiners_Mod_Mod_Depth",                  "Diffuse_EdgeFade_T1_T2"),
        };

        // pixel-shader name -> combiner id. The numbering matches the OpenGL renderer's GLSL
        // combiner so a variant implemented on one side can be read straight across to the other.
        static readonly Dictionary<string, int> PixelShaderIds = new Dictionary<string, int>
        {
            { "Combiners_Opaque", 0 }, { "Combiners_Mod", 1 }, { "Combiners_Opaque_Mod", 2 },
            { "Combiners_Opaque_Mod2x", 3 }, { "Combiners_Opaque_Mod2xNA", 4 }, { "Combiners_Opaque_Opaque", 5 },
            { "Combiners_Mod_Mod", 6 }, { "Combiners_Mod_Mod2x", 7 }, { "Combiners_Mod_Add", 8 },
            { "Combiners_Mod_Mod2xNA", 9 }, { "Combiners_Mod_AddNA", 10 }, { "Combiners_Mod_Opaque", 11 },
            { "Combiners_Opaque_Mod2xNA_Alpha", 12 }, { "Combiners_Opaque_AddAlpha", 13 },
            { "Combiners_Opaque_AddAlpha_Alpha", 14 }, { "Combiners_Opaque_Mod2xNA_Alpha_Add", 15 },
            { "Combiners_Mod_AddAlpha", 16 }, { "Combiners_Mod_AddAlpha_Alpha", 17 },
            { "Combiners_Opaque_Alpha_Alpha", 18 }, { "Combiners_Opaque_Mod2xNA_Alpha_3s", 19 },
            { "Combiners_Opaque_AddAlpha_Wgt", 20 }, { "Combiners_Mod_Add_Alpha", 21 },
            { "Combiners_Opaque_ModNA_Alpha", 22 }, { "Combiners_Mod_AddAlpha_Wgt", 23 },
            { "Combiners_Opaque_Mod_Add_Wgt", 24 }, { "Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", 25 },
            { "Combiners_Mod_Dual_Crossfade", 26 }, { "Combiners_Opaque_Mod2xNA_Alpha_Alpha", 27 },
            { "Combiners_Mod_Masked_Dual_Crossfade", 28 }, { "Combiners_Opaque_Alpha", 29 },
            { "Guild", 30 }, { "Guild_NoBorder", 31 }, { "Guild_Opaque", 32 },
            { "Combiners_Mod_Depth", 33 }, { "Illum", 34 }, { "Combiners_Mod_Mod_Mod_Const", 35 },
            { "Combiners_Mod_Mod_Depth", 36 },
        };

        public static string GetPixelShaderName(int textureCount, int shaderId)
        {
            if ((shaderId & ExplicitShaderBit) != 0)
            {
                int id = shaderId & 0x7FFF;
                return (id >= ShaderArray.Length) ? "Combiners_Opaque" : ShaderArray[id].PS;
            }
            if (textureCount == 1)
                return ((shaderId & 0x70) != 0) ? "Combiners_Mod" : "Combiners_Opaque";
            if ((shaderId & 0x70) != 0)
            {
                switch (shaderId & 7)
                {
                    case 3: return "Combiners_Mod_Add";
                    case 4: return "Combiners_Mod_Mod2x";
                    case 6: return "Combiners_Mod_Mod2xNA";
                    case 7: return "Combiners_Mod_AddNA";
                    default: return "Combiners_Mod_Mod";
                }
            }
            switch (shaderId & 7)
            {
                case 0: return "Combiners_Opaque_Opaque";
                case 3:
                case 7: return "Combiners_Opaque_AddAlpha";
                case 4: return "Combiners_Opaque_Mod2x";
                case 6: return "Combiners_Opaque_Mod2xNA";
                default: return "Combiners_Opaque_Mod";
            }
        }

        public static string GetVertexShaderName(int textureCount, int shaderId)
        {
            if ((shaderId & ExplicitShaderBit) != 0)
            {
                int id = shaderId & 0x7FFF;
                return (id >= ShaderArray.Length) ? "Diffuse_T1" : ShaderArray[id].VS;
            }
            if (textureCount == 1)
            {
                if ((shaderId & 0x80) != 0) return "Diffuse_Env";
                return ((shaderId & 0x4000) != 0) ? "Diffuse_T2" : "Diffuse_T1";
            }
            if ((shaderId & 0x80) != 0)
                return ((shaderId & 0x8) != 0) ? "Diffuse_Env_Env" : "Diffuse_Env_T1";
            if ((shaderId & 0x8) != 0) return "Diffuse_T1_Env";
            return ((shaderId & 0x4000) != 0) ? "Diffuse_T1_T2" : "Diffuse_T1_T1";
        }

        public static int PixelShaderNameToId(string name)
        {
            int id;
            return PixelShaderIds.TryGetValue(name, out id) ? id : 0;
        }

        /// <summary>
        /// Per-unit UV routing, read off the vertex-shader name: Diffuse_T1_Env means unit 0 takes
        /// UV set 0 and unit 1 is an environment sphere map. "EdgeFade" is a fade term, not a unit,
        /// and is skipped. Units the name does not mention keep the defaults.
        /// </summary>
        public static M2UvSource[] UvSourceForVertexShaderName(string name)
        {
            var outSrc = new[] { M2UvSource.TexCoord0, M2UvSource.TexCoord1,
                                 M2UvSource.TexCoord2, M2UvSource.TexCoord2 };
            if (string.IsNullOrEmpty(name))
                return outSrc;

            string s = name;
            foreach (var prefix in new[] { "BW_Diffuse_", "Diffuse_", "Color_" })
            {
                if (s.StartsWith(prefix)) { s = s.Substring(prefix.Length); break; }
            }

            int unit = 0;
            foreach (var token in s.Split('_'))
            {
                if (unit >= 4) break;
                if (token.Length == 0 || token == "EdgeFade") continue;
                if (token == "T1") outSrc[unit++] = M2UvSource.TexCoord0;
                else if (token == "T2") outSrc[unit++] = M2UvSource.TexCoord1;
                else if (token == "T3") outSrc[unit++] = M2UvSource.TexCoord2;
                else if (token == "Env") outSrc[unit++] = M2UvSource.Environment;
            }
            return outSrc;
        }

        /// <summary>Everything a renderer needs to know about one batch's material combine.</summary>
        public struct Resolved
        {
            public string PixelShaderName, VertexShaderName;
            public int PixelShader;              // id shared with the OpenGL renderer's GLSL combiner
            public M2UvSource[] UvSource;        // one entry per texture unit
        }

        public static Resolved Resolve(int textureCount, int shaderId)
        {
            Resolved r;
            r.PixelShaderName = GetPixelShaderName(textureCount, shaderId);
            r.VertexShaderName = GetVertexShaderName(textureCount, shaderId);
            r.PixelShader = PixelShaderNameToId(r.PixelShaderName);
            r.UvSource = UvSourceForVertexShaderName(r.VertexShaderName);
            return r;
        }
    }
}
