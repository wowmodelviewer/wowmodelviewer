// WowParserTests.cs
//
// Unit tests for the runtime parsing layer (M2, skin, BLP, coordinate conversion).
//
// Deliberately framework-free: the parsers are plain C# with no UnityEngine dependency, so
// these run anywhere a C# compiler exists -- `WowParserTests.RunAll()` returns the failure
// count and prints one line per case. That keeps them runnable in CI or from a console harness
// without a Unity install, and they can be wrapped in [Test] methods for the Unity Test
// Framework later without changing the assertions.
//
// Every fixture is a hand-built byte array. NO extracted WoW asset is committed here: the
// fixtures encode only format structure (headers, offsets, counts), not game content.

using System;
using System.Collections.Generic;
using Wmv.Wow;

namespace Wmv.Wow.Tests
{
    public static class WowParserTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        // ---------------------------------------------------------------- tiny assert kit

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static void Throws<T>(Action action, string name) where T : Exception
        {
            try
            {
                action();
                log.Add("  FAIL  " + name + " (expected " + typeof(T).Name + ", nothing was thrown)");
                failures++;
            }
            catch (T) { log.Add("  PASS  " + name); }
            catch (Exception e)
            {
                log.Add("  FAIL  " + name + " (expected " + typeof(T).Name + ", got " + e.GetType().Name + ")");
                failures++;
            }
        }

        static void Near(float actual, float expected, string name, float eps = 1e-4f)
        {
            Check(Math.Abs(actual - expected) <= eps, name + " (expected " + expected + ", got " + actual + ")");
        }

        // ---------------------------------------------------------------- fixture builders

        static void PutU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24);
        }

        static void PutU16(byte[] b, int o, ushort v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); }

        static void PutF32(byte[] b, int o, float v) { Buffer.BlockCopy(BitConverter.GetBytes(v), 0, b, o, 4); }

        static void PutMagic(byte[] b, int o, string m)
        {
            for (int i = 0; i < 4; i++) b[o + i] = (byte)m[i];
        }

        /// <summary>One bone on disk, laid out the way the format does it.</summary>
        const int BoneStride = 88;

        static void PutBone(byte[] b, int o, short parent, float px, float py, float pz, uint flags = 0)
        {
            PutU32(b, o + 0, unchecked((uint)-1));   // keyBoneId
            PutU32(b, o + 4, flags);
            PutU16(b, o + 8, unchecked((ushort)parent));
            PutU16(b, o + 10, 0);                    // submeshId
            PutF32(b, o + 76, px); PutF32(b, o + 80, py); PutF32(b, o + 84, pz);
        }

        /// <summary>Minimal but structurally valid MD20 payload with `vertexCount` vertices, and
        /// optionally a bone array.</summary>
        static byte[] BuildM2Payload(int vertexCount, int textureCount = 1, uint textureType = 11,
                                     int boneCount = 0)
        {
            const int headerSize = 0x100;
            int vertsOffset = headerSize;
            int texOffset = vertsOffset + vertexCount * 48;
            int matOffset = texOffset + textureCount * 16;
            int lookupOffset = matOffset + 4;
            int boneOffset = lookupOffset + 2;
            int total = boneOffset + boneCount * BoneStride;

            var b = new byte[total];
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x08, 0); PutU32(b, 0x0C, 0);            // name
            PutU32(b, 0x10, 0);                                 // globalFlags
            PutU32(b, 0x2C, (uint)boneCount); PutU32(b, 0x30, (uint)boneOffset);
            PutU32(b, 0x3C, (uint)vertexCount); PutU32(b, 0x40, (uint)vertsOffset);
            PutU32(b, 0x44, 1);                                 // numSkinProfiles
            PutU32(b, 0x50, (uint)textureCount); PutU32(b, 0x54, (uint)texOffset);
            PutU32(b, 0x70, 1); PutU32(b, 0x74, (uint)matOffset);
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)lookupOffset);

            for (int i = 0; i < vertexCount; i++)
            {
                int o = vertsOffset + i * 48;
                PutF32(b, o + 0, i);            // position
                PutF32(b, o + 4, i * 2);
                PutF32(b, o + 8, i * 3);
                PutF32(b, o + 20, 0f);          // normal (after 4 weight + 4 index bytes)
                PutF32(b, o + 24, 0f);
                PutF32(b, o + 28, 1f);
                PutF32(b, o + 32, 0.25f);       // uv0
                PutF32(b, o + 36, 0.75f);
            }
            for (int i = 0; i < textureCount; i++)
            {
                PutU32(b, texOffset + i * 16 + 0, textureType);
                PutU32(b, texOffset + i * 16 + 4, 3);   // flags: wrap x|y
            }
            PutU16(b, matOffset, 0);        // material flags
            PutU16(b, matOffset + 2, 1);    // blend mode: alpha key
            PutU16(b, lookupOffset, 0);     // textureLookup[0] -> slot 0

            // A small chain: bone 0 at the origin, each later one a child of the one before it,
            // one unit further along WoW's +X (forward).
            for (int i = 0; i < boneCount; i++)
                PutBone(b, boneOffset + i * BoneStride, (short)(i - 1), i, 0f, 0f);
            return b;
        }

        static byte[] WrapChunked(byte[] md20Payload, int[] sfid, int[] txid, int skid = 0)
        {
            int total = 8 + md20Payload.Length + (sfid != null ? 8 + sfid.Length * 4 : 0)
                                               + (txid != null ? 8 + txid.Length * 4 : 0)
                                               + (skid != 0 ? 12 : 0);
            var b = new byte[total];
            int o = 0;
            PutMagic(b, o, "MD21"); PutU32(b, o + 4, (uint)md20Payload.Length); o += 8;
            Buffer.BlockCopy(md20Payload, 0, b, o, md20Payload.Length); o += md20Payload.Length;
            if (sfid != null)
            {
                PutMagic(b, o, "SFID"); PutU32(b, o + 4, (uint)(sfid.Length * 4)); o += 8;
                foreach (int id in sfid) { PutU32(b, o, (uint)id); o += 4; }
            }
            if (txid != null)
            {
                PutMagic(b, o, "TXID"); PutU32(b, o + 4, (uint)(txid.Length * 4)); o += 8;
                foreach (int id in txid) { PutU32(b, o, (uint)id); o += 4; }
            }
            if (skid != 0)
            {
                PutMagic(b, o, "SKID"); PutU32(b, o + 4, 4); o += 8;
                PutU32(b, o, (uint)skid); o += 4;
            }
            return b;
        }

        /// <summary>Skin with `vertexCount` lookup entries and one submesh covering `triangles`.</summary>
        static byte[] BuildSkin(int vertexCount, ushort[] triangles, ushort submeshIndexCount = 0xFFFF,
                                ushort lookupOverride = 0xFFFF, ushort batchColorIndex = 0xFFFF,
                                ushort batchTextureCount = 1, ushort submeshId = 0)
        {
            const int headerSize = 0x30;
            int vertOffset = headerSize;
            int triOffset = vertOffset + vertexCount * 2;
            int subOffset = triOffset + triangles.Length * 2;
            int batchOffset = subOffset + 48;
            int total = batchOffset + 24;

            var b = new byte[total];
            PutMagic(b, 0, "SKIN");
            PutU32(b, 0x04, (uint)vertexCount); PutU32(b, 0x08, (uint)vertOffset);
            PutU32(b, 0x0C, (uint)triangles.Length); PutU32(b, 0x10, (uint)triOffset);
            PutU32(b, 0x14, 0); PutU32(b, 0x18, 0);                 // bones
            PutU32(b, 0x1C, 1); PutU32(b, 0x20, (uint)subOffset);   // submeshes
            PutU32(b, 0x24, 1); PutU32(b, 0x28, (uint)batchOffset); // batches

            for (int i = 0; i < vertexCount; i++)
                PutU16(b, vertOffset + i * 2, lookupOverride == 0xFFFF ? (ushort)i : lookupOverride);
            for (int i = 0; i < triangles.Length; i++)
                PutU16(b, triOffset + i * 2, triangles[i]);

            PutU16(b, subOffset + 0, submeshId);             // id (geoset number)
            PutU16(b, subOffset + 2, 0);                     // level
            PutU16(b, subOffset + 4, 0);                     // vertexStart
            PutU16(b, subOffset + 6, (ushort)vertexCount);   // vertexCount
            PutU16(b, subOffset + 8, 0);                     // indexStart
            PutU16(b, subOffset + 10, submeshIndexCount == 0xFFFF ? (ushort)triangles.Length : submeshIndexCount);

            b[batchOffset] = 0;                              // flags
            PutU16(b, batchOffset + 4, 0);                   // submeshIndex
            PutU16(b, batchOffset + 8, batchColorIndex);     // colorIndex
            PutU16(b, batchOffset + 14, batchTextureCount);  // textureCount
            PutU16(b, batchOffset + 16, 0);                  // textureComboIndex
            PutU16(b, batchOffset + 18, 0xFFFF);             // textureCoordComboIndex
            PutU16(b, batchOffset + 20, 0);                  // textureWeightComboIndex
            PutU16(b, batchOffset + 22, 0);                  // textureTransformComboIndex
            return b;
        }

        /// <summary>BLP2 with one palettized mip level.</summary>
        static byte[] BuildBlpPalettized(int w, int h, byte alphaSize)
        {
            const int headerSize = 0x494;
            int pixels = w * h;
            int alphaBytes = alphaSize == 8 ? pixels : alphaSize == 4 ? (pixels + 1) / 2
                           : alphaSize == 1 ? (pixels + 7) / 8 : 0;
            var b = new byte[headerSize + pixels + alphaBytes];
            PutMagic(b, 0, "BLP2");
            PutU32(b, 0x04, 1);
            b[0x08] = 1;            // palettized
            b[0x09] = alphaSize;
            b[0x0A] = 0;
            b[0x0B] = 0;
            PutU32(b, 0x0C, (uint)w);
            PutU32(b, 0x10, (uint)h);
            PutU32(b, 0x14, headerSize);                       // mipOffsets[0]
            PutU32(b, 0x54, (uint)(pixels + alphaBytes));      // mipSizes[0]
            // palette entry 1 = pure red; palette entries are BGRA, so bytes are 00 00 FF 00
            PutU32(b, 0x94 + 4, 0x00FF0000u);
            for (int i = 0; i < pixels; i++) b[headerSize + i] = 1;
            for (int i = 0; i < alphaBytes; i++) b[headerSize + pixels + i] = 0xFF;
            return b;
        }

        // ---------------------------------------------------------------- the tests

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();

            log.Add("M2Parser");
            M2Tests();
            log.Add("M2SkinParser");
            SkinTests();
            log.Add("BlpDecoder");
            BlpTests();
            log.Add("WowCoordinateConverter");
            CoordinateTests();
            log.Add("M2ShaderTable");
            ShaderTableTests();
            log.Add("Visibility tracks");
            VisibilityTests();
            log.Add("Bones");
            BoneTests();

            log.Add(failures == 0 ? "ALL TESTS PASSED" : (failures + " TEST(S) FAILED"));
            if (output != null)
                foreach (var l in log) output(l);
            return failures;
        }

        public static string LastReport { get { return string.Join(Environment.NewLine, log.ToArray()); } }

        static void M2Tests()
        {
            // valid header + arrays, chunked exactly like retail
            byte[] file = WrapChunked(BuildM2Payload(3), new[] { 473370 }, new[] { 0 });
            M2ParsedModel m = M2Parser.Parse(file);
            Check(m.Version == 272, "M2: version read");
            Check(m.Vertices.Length == 3, "M2: vertex count");
            Check(m.SkinFileDataIDs.Length == 1 && m.SkinFileDataIDs[0] == 473370, "M2: SFID chunk read");
            Check(m.TextureFileDataIDs.Length == 1 && m.TextureFileDataIDs[0] == 0, "M2: TXID chunk read");
            Check(m.Textures.Length == 1 && m.Textures[0].IsReplaceable, "M2: replaceable texture detected");
            Check(m.Materials.Length == 1 && m.Materials[0].BlendMode == 1, "M2: material blend mode");
            Near(m.Vertices[1].Position.X, 1f, "M2: vertex position X");
            Near(m.Vertices[1].TexCoord0.Y, 0.75f, "M2: vertex uv V");
            Check(m.BoundsMax.Z == 6f && m.BoundsMin.X == 0f, "M2: bounds computed");

            // a bare (unchunked) MD20 is accepted too
            M2ParsedModel bare = M2Parser.Parse(BuildM2Payload(2));
            Check(bare.Vertices.Length == 2, "M2: bare MD20 accepted");

            // truncated header
            var truncated = new byte[64];
            PutMagic(truncated, 0, "MD20");
            Throws<WowParseException>(() => M2Parser.Parse(truncated), "M2: truncated header rejected");

            // wrong magic
            var wrongMagic = BuildM2Payload(1);
            PutMagic(wrongMagic, 0, "XXXX");
            Throws<WowParseException>(() => M2Parser.Parse(wrongMagic), "M2: wrong magic rejected");

            // vertex array offset points past the end
            var badOffset = BuildM2Payload(3);
            PutU32(badOffset, 0x40, (uint)(badOffset.Length + 1000));
            Throws<WowParseException>(() => M2Parser.Parse(badOffset), "M2: out-of-range array offset rejected");

            // implausible count (overflow guard)
            var badCount = BuildM2Payload(3);
            PutU32(badCount, 0x3C, 0x40000000u);
            Throws<WowParseException>(() => M2Parser.Parse(badCount), "M2: overflowing array count rejected");

            // chunk larger than the file
            var badChunk = WrapChunked(BuildM2Payload(1), null, null);
            PutU32(badChunk, 4, (uint)(badChunk.Length * 4));
            Throws<WowParseException>(() => M2Parser.Parse(badChunk), "M2: oversized chunk rejected");
        }

        static void BoneTests()
        {
            M2ParsedModel m = M2Parser.Parse(BuildM2Payload(3, boneCount: 4));
            Check(m.BoneCount == 4 && m.Bones.Length == 4, "bones: count read");
            Check(m.Bones[0].Parent == -1, "bones: first bone is a root");
            Check(m.Bones[1].Parent == 0 && m.Bones[3].Parent == 2, "bones: parent chain read");
            Near(m.Bones[2].Pivot.X, 2f, "bones: pivot read");
            Check(m.SkeletonFileDataID == 0, "bones: no SKID means no skeleton file");

            // A parent index outside the array cannot be followed; it becomes a root rather than
            // a read off the end of the array.
            var badParent = BuildM2Payload(3, boneCount: 3);
            PutU16(badParent, 0x100 + 3 * 48 + 16 + 4 + 2 + BoneStride + 8, 99);
            M2ParsedModel bp = M2Parser.Parse(badParent);
            Check(bp.Bones[1].Parent == -1, "bones: out-of-range parent becomes a root");

            // Same for a bone that claims to be its own parent.
            var selfParent = BuildM2Payload(3, boneCount: 3);
            PutU16(selfParent, 0x100 + 3 * 48 + 16 + 4 + 2 + BoneStride + 8, 1);
            Check(M2Parser.Parse(selfParent).Bones[1].Parent == -1, "bones: self-parent becomes a root");

            // ... and for a cycle, which would otherwise be an infinite parent walk and a
            // renderer asked to parent a transform inside its own descendants.
            var cycle = BuildM2Payload(3, boneCount: 3);
            int boneBase = 0x100 + 3 * 48 + 16 + 4 + 2;
            PutU16(cycle, boneBase + 8, 2);                 // 0 -> 2 -> 1 -> 0
            PutU16(cycle, boneBase + BoneStride + 8, 0);
            PutU16(cycle, boneBase + 2 * BoneStride + 8, 1);
            M2ParsedModel cy = M2Parser.Parse(cycle);
            // The guarantee is not that every bone in the loop becomes a root -- it is that no
            // parent chain runs forever, which is what the renderer and the depth walk need.
            bool allChainsTerminate = true;
            for (int i = 0; i < cy.Bones.Length; i++)
            {
                int p = cy.Bones[i].Parent, steps = 0;
                while (p >= 0 && steps++ <= cy.Bones.Length) p = cy.Bones[p].Parent;
                if (p >= 0) allChainsTerminate = false;
            }
            Check(allChainsTerminate, "bones: parent cycle broken");

            // A model whose bones live in a skeleton file must not hand back the header's array:
            // the vertices are not indexed against it.
            byte[] skid = WrapChunked(BuildM2Payload(3, boneCount: 4), null, null, 1234567);
            M2ParsedModel sk = M2Parser.Parse(skid);
            Check(sk.SkeletonFileDataID == 1234567, "bones: SKID chunk read");
            Check(sk.BoneCount == 4 && sk.Bones.Length == 0, "bones: SKID suppresses the header bone array");

            // The per-vertex influences are direct indices, kept verbatim.
            var weighted = BuildM2Payload(2, boneCount: 4);
            int v1 = 0x100 + 48;
            weighted[v1 + 12] = 200; weighted[v1 + 13] = 55;   // weights
            weighted[v1 + 16] = 3; weighted[v1 + 17] = 1;      // indices
            M2ParsedModel wm = M2Parser.Parse(weighted);
            Check(wm.Vertices[1].BoneWeight0 == 200 && wm.Vertices[1].BoneWeight1 == 55,
                  "bones: vertex weights read");
            Check(wm.Vertices[1].BoneIndex0 == 3 && wm.Vertices[1].BoneIndex1 == 1,
                  "bones: vertex bone indices read");

            // A bone array that runs past the end of the payload is a parse error, not a read
            // into whatever follows it.
            var truncatedBones = BuildM2Payload(3, boneCount: 2);
            PutU32(truncatedBones, 0x2C, 64);
            Throws<WowParseException>(() => M2Parser.Parse(truncatedBones),
                                      "bones: truncated bone array rejected");
        }

        static void SkinTests()
        {
            ushort[] tris = { 0, 1, 2, 2, 1, 0 };
            M2ParsedSkin skin = M2SkinParser.Parse(BuildSkin(3, tris));
            Check(skin.VertexLookup.Length == 3, "skin: vertex lookup count");
            Check(skin.TriangleCount == 2, "skin: triangle count");
            Check(skin.Submeshes.Length == 1 && skin.Submeshes[0].IndexCount == 6, "skin: submesh range");
            Check(skin.Batches.Length == 1 && skin.Batches[0].TextureCount == 1, "skin: batch parsed");

            int[] resolved = M2SkinParser.BuildTriangles(skin, skin.Submeshes[0], 3);
            Check(resolved.Length == 6 && resolved[0] == 0 && resolved[3] == 2, "skin: two-level index resolution");

            // triangle index outside the vertex lookup
            ushort[] badTris = { 0, 1, 9 };
            Throws<WowParseException>(() => M2SkinParser.Parse(BuildSkin(3, badTris)),
                                      "skin: triangle index beyond lookup rejected");

            // lookup entry pointing past the model's vertex array
            M2ParsedSkin badLookup = M2SkinParser.Parse(BuildSkin(3, tris, 0xFFFF, 250));
            Throws<WowParseException>(() => M2SkinParser.BuildTriangles(badLookup, badLookup.Submeshes[0], 3),
                                      "skin: out-of-range model vertex index rejected");

            // submesh claiming more indices than exist
            Throws<WowParseException>(() => M2SkinParser.Parse(BuildSkin(3, tris, 300)),
                                      "skin: invalid submesh range rejected");

            // index count not a multiple of 3
            ushort[] oddTris = { 0, 1 };
            Throws<WowParseException>(() => M2SkinParser.Parse(BuildSkin(3, oddTris)),
                                      "skin: non-multiple-of-3 index count rejected");

            // wrong magic
            byte[] wrong = BuildSkin(3, tris);
            PutMagic(wrong, 0, "NOPE");
            Throws<WowParseException>(() => M2SkinParser.Parse(wrong), "skin: wrong magic rejected");
        }

        static void BlpTests()
        {
            // the encoding chicken's skin actually uses (palettized + 8-bit alpha)
            BlpImage img = BlpDecoder.Decode(BuildBlpPalettized(4, 4, 8));
            Check(img.Width == 4 && img.Height == 4, "BLP: dimensions");
            Check(img.Rgba.Length == 4 * 4 * 4, "BLP: decoded byte size");
            Check(img.Rgba[0] == 255 && img.Rgba[1] == 0 && img.Rgba[2] == 0, "BLP: palette colour (BGRA->RGBA)");
            Check(img.Rgba[3] == 255, "BLP: 8-bit alpha applied");
            Check(img.Encoding == "palettized/a8", "BLP: encoding reported");

            BlpImage noAlpha = BlpDecoder.Decode(BuildBlpPalettized(4, 4, 0));
            Check(noAlpha.Rgba[3] == 255, "BLP: alphaSize 0 is opaque");

            BlpImage a1 = BlpDecoder.Decode(BuildBlpPalettized(8, 8, 1));
            Check(a1.Rgba[3] == 255, "BLP: 1-bit alpha applied");

            // malformed header
            Throws<WowParseException>(() => BlpDecoder.Decode(new byte[10]), "BLP: truncated header rejected");

            byte[] wrongMagic = BuildBlpPalettized(4, 4, 8);
            PutMagic(wrongMagic, 0, "BLP1");
            Throws<WowParseException>(() => BlpDecoder.Decode(wrongMagic), "BLP: wrong magic rejected");

            // unsupported colour encoding must say so explicitly
            byte[] unsupported = BuildBlpPalettized(4, 4, 8);
            unsupported[0x08] = 9;
            Throws<WowParseException>(() => BlpDecoder.Decode(unsupported), "BLP: unsupported encoding rejected");

            // mip level that is not present
            Throws<WowParseException>(() => BlpDecoder.Decode(BuildBlpPalettized(4, 4, 8), 3),
                                      "BLP: absent mip level rejected");

            // implausible dimensions
            byte[] huge = BuildBlpPalettized(4, 4, 8);
            PutU32(huge, 0x0C, 100000);
            Throws<WowParseException>(() => BlpDecoder.Decode(huge), "BLP: implausible dimensions rejected");
        }

        /// <summary>
        /// The shader table decides which combiner a material uses and where each texture unit
        /// takes its coordinates from. Getting this wrong is invisible -- the model still renders,
        /// just with every unit past the first silently dropped -- so it is worth pinning down.
        /// chicken2's own material (2 units, shaderId 0x8000) is the worked example.
        /// </summary>
        static void ShaderTableTests()
        {
            var r = M2ShaderTable.Resolve(2, 0x8000);
            Check(r.PixelShaderName == "Combiners_Opaque_Mod2xNA_Alpha", "shader: 0x8000 -> pixel shader name");
            Check(r.PixelShader == 12, "shader: 0x8000 -> combiner id 12");
            Check(r.VertexShaderName == "Diffuse_T1_Env", "shader: 0x8000 -> vertex shader name");
            Check(r.UvSource[0] == M2UvSource.TexCoord0, "shader: unit 0 samples uv set 0");
            Check(r.UvSource[1] == M2UvSource.Environment, "shader: unit 1 is an environment sphere map");

            // 0x8000 | 21 -> Combiners_Mod_Mod / Diffuse_EdgeFade_T1_T2: EdgeFade is a fade term,
            // not a texture unit, so unit 1 must still come out as uv set 1.
            var edge = M2ShaderTable.Resolve(2, 0x8000 | 21);
            Check(edge.VertexShaderName == "Diffuse_EdgeFade_T1_T2", "shader: edge-fade vertex shader name");
            Check(edge.UvSource[0] == M2UvSource.TexCoord0 && edge.UvSource[1] == M2UvSource.TexCoord1,
                  "shader: EdgeFade is skipped when assigning units");

            // an explicit id past the end of the table must degrade, not throw
            var over = M2ShaderTable.Resolve(2, 0x8000 | 999);
            Check(over.PixelShaderName == "Combiners_Opaque" && over.VertexShaderName == "Diffuse_T1",
                  "shader: out-of-range explicit id falls back");

            // without the explicit bit the id is decoded from its bit fields instead
            Check(M2ShaderTable.GetPixelShaderName(1, 0) == "Combiners_Opaque",
                  "shader: single texture, no flags -> opaque");
            Check(M2ShaderTable.GetPixelShaderName(1, 0x70) == "Combiners_Mod",
                  "shader: single texture, mod flag");
            Check(M2ShaderTable.GetVertexShaderName(2, 0x8) == "Diffuse_T1_Env",
                  "shader: two textures, env flag");
        }

        /// <summary>
        /// A model hides geometry it is not currently using by keying an animation track to zero
        /// rather than by leaving the geometry out. Reading those tracks is what stops the
        /// renderer drawing a hidden overlay on top of the detail it is meant to replace.
        /// </summary>
        static void VisibilityTests()
        {
            byte[] payload = BuildM2PayloadWithTracks(new[] { 0f, 1f }, 1f);
            M2ParsedModel m = M2Parser.Parse(payload);

            Check(m.Colors.Length == 2, "tracks: colours parsed");
            Check(m.Colors[0].HasColorTrack && m.Colors[1].HasColorTrack, "tracks: colour RGB tracks present");
            Near(m.Colors[0].Alpha, 0f, "tracks: colour 0 alpha is 0");
            Near(m.Colors[1].Alpha, 1f, "tracks: colour 1 alpha is 1");
            Check(m.TextureWeights.Length == 1, "tracks: texture weights parsed");
            Near(m.TextureWeights[0], 1f, "tracks: texture weight value");
            Check(m.TextureWeightLookup.Length == 1 && m.TextureWeightLookup[0] == 0,
                  "tracks: texture weight lookup parsed");

            // a header with no such arrays must leave them empty, not throw: that is what makes
            // "no tracks" mean "everything visible" rather than "everything hidden".
            M2ParsedModel plain = M2Parser.Parse(BuildM2Payload(2));
            Check(plain.Colors.Length == 0 && plain.TextureWeights.Length == 0,
                  "tracks: absent arrays parse as empty");

            // the batch fields those tracks are reached through
            var skin = M2SkinParser.Parse(BuildSkin(3, new ushort[] { 0, 1, 2 },
                                                    batchColorIndex: 0, batchTextureCount: 2));
            Check(skin.Batches[0].ColorIndex == 0 && skin.Batches[0].HasColor,
                  "tracks: batch colour index parsed");
            Check(skin.Batches[0].TextureWeightComboIndex == 0, "tracks: batch weight combo index parsed");

            // The geoset number is the low 15 bits: the legacy viewport masks the same way, and a
            // creature display's geoset set is expressed in those numbers. Without the mask the
            // two renderers would compare different values and never agree on what is visible.
            var masked = M2SkinParser.Parse(BuildSkin(3, new ushort[] { 0, 1, 2 }, submeshId: 0x8065));
            Check(masked.Submeshes[0].Id == 0x0065, "geoset: submesh id is masked to 15 bits");
            var unmasked = M2SkinParser.Parse(BuildSkin(3, new ushort[] { 0, 1, 2 }, submeshId: 101));
            Check(unmasked.Submeshes[0].Id == 101, "geoset: an id below the mask is unchanged");
            Check(skin.Batches[0].TextureCoordComboIndex == 0xFFFF, "tracks: batch coord combo index parsed");

            // a batch with no colour entry is never hidden by one
            var noColor = M2SkinParser.Parse(BuildSkin(3, new ushort[] { 0, 1, 2 }));
            Check(!noColor.Batches[0].HasColor, "tracks: 0xFFFF colour index means 'no colour entry'");

            // two texture units resolve to two different slots through the same combo run
            M2ParsedModel two = M2Parser.Parse(BuildM2PayloadWithTracks(new[] { 1f }, 1f, texLookup: new ushort[] { 0, 1 }));
            Check(two.TextureLookup.Length == 2 && two.TextureLookup[0] == 0 && two.TextureLookup[1] == 1,
                  "tracks: texture combo run resolves both units");
        }

        /// <summary>
        /// An M2 payload carrying colour and texture-weight tracks. Each track is
        /// {uint16 interpolation, uint16 globalSeq, M2Array timestamps, M2Array values}, where
        /// both arrays are arrays OF arrays -- one per animation.
        /// </summary>
        static byte[] BuildM2PayloadWithTracks(float[] colorAlphas, float weight, ushort[] texLookup = null)
        {
            if (texLookup == null) texLookup = new ushort[] { 0 };
            const int headerSize = 0x100;
            int vertsOffset = headerSize;
            int texOffset = vertsOffset + 1 * 48;
            int matOffset = texOffset + 1 * 16;
            int lookupOffset = matOffset + 4;
            int colorsOffset = lookupOffset + texLookup.Length * 2;
            int weightsOffset = colorsOffset + colorAlphas.Length * 40;
            int weightLookupOffset = weightsOffset + 20;
            // one nested-array header + one value per track, laid out after everything else
            int poolOffset = weightLookupOffset + 2;
            int total = poolOffset + (colorAlphas.Length * 2 + 1) * (8 + 2) + 64;

            var b = new byte[total];
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x3C, 1); PutU32(b, 0x40, (uint)vertsOffset);
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, 1); PutU32(b, 0x54, (uint)texOffset);
            PutU32(b, 0x70, 1); PutU32(b, 0x74, (uint)matOffset);
            PutU32(b, 0x80, (uint)texLookup.Length); PutU32(b, 0x84, (uint)lookupOffset);
            PutU32(b, 0x48, (uint)colorAlphas.Length); PutU32(b, 0x4C, (uint)colorsOffset);
            PutU32(b, 0x58, 1); PutU32(b, 0x5C, (uint)weightsOffset);
            PutU32(b, 0x90, 1); PutU32(b, 0x94, (uint)weightLookupOffset);

            PutU32(b, texOffset, 11);
            for (int i = 0; i < texLookup.Length; i++) PutU16(b, lookupOffset + i * 2, texLookup[i]);
            PutU16(b, weightLookupOffset, 0);

            int pool = poolOffset;
            // Writes one M2Track at trackOffset holding a single fixed16 value, and returns the
            // next free byte in the pool.
            Func<int, float, int> writeTrack = (trackOffset, value) =>
            {
                int inner = pool;                       // the value itself
                PutU16(b, inner, (ushort)(short)(value * 32767f));
                pool += 2;
                int outer = pool;                       // M2Array pointing at it
                PutU32(b, outer, 1); PutU32(b, outer + 4, (uint)inner);
                pool += 8;
                PutU32(b, trackOffset + 12, 1);         // values: one animation
                PutU32(b, trackOffset + 16, (uint)outer);
                return pool;
            };

            for (int i = 0; i < colorAlphas.Length; i++)
            {
                int rec = colorsOffset + i * 40;
                writeTrack(rec, 1f);                    // RGB track: only its presence is read
                writeTrack(rec + 20, colorAlphas[i]);   // alpha track
            }
            writeTrack(weightsOffset, weight);
            return b;
        }

        static void CoordinateTests()
        {
            // WoW (X forward, Y left, Z up) -> Unity (Z forward, X right, Y up)
            float x, y, z;
            WowCoordinateConverter.ConvertPosition(new WowVec3(1f, 2f, 3f), out x, out y, out z);
            Near(x, -2f, "coords: X = -wowY (right = -left)");
            Near(y, 3f, "coords: Y = wowZ (up)");
            Near(z, 1f, "coords: Z = wowX (forward)");

            WowCoordinateConverter.ConvertNormal(new WowVec3(0f, 0f, 1f), out x, out y, out z);
            Near(x, 0f, "coords: up normal X");
            Near(y, 1f, "coords: up normal stays up");
            Near(z, 0f, "coords: up normal Z");

            // conversion must preserve normal length (no scale leaking in)
            WowCoordinateConverter.ConvertNormal(new WowVec3(0.6f, 0f, 0.8f), out x, out y, out z);
            Near((float)Math.Sqrt(x * x + y * y + z * z), 1f, "coords: normals stay unit length");

            float u, v;
            WowCoordinateConverter.ConvertTexCoord(new WowVec2(0.25f, 0.75f), out u, out v);
            Near(u, 0.25f, "coords: U unchanged");
            Near(v, 0.25f, "coords: V flipped for Unity");

            // winding must reverse, because the axis map is orientation-reversing
            var tri = new[] { 0, 1, 2, 3, 4, 5 };
            WowCoordinateConverter.FlipWinding(tri);
            Check(tri[0] == 0 && tri[1] == 2 && tri[2] == 1, "coords: winding flipped (triangle 1)");
            Check(tri[3] == 3 && tri[4] == 5 && tri[5] == 4, "coords: winding flipped (triangle 2)");
        }
    }
}
