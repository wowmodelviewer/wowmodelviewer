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

        static byte[] WrapChunked(byte[] md20Payload, int[] sfid, int[] txid, int skid = 0,
                                  int afidAnimId = -1)
        {
            int total = 8 + md20Payload.Length + (sfid != null ? 8 + sfid.Length * 4 : 0)
                                               + (txid != null ? 8 + txid.Length * 4 : 0)
                                               + (skid != 0 ? 12 : 0)
                                               + (afidAnimId >= 0 ? 16 : 0);
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
            if (afidAnimId >= 0)
            {
                // One entry: animId(2) subAnimId(2) fileId(4).
                PutMagic(b, o, "AFID"); PutU32(b, o + 4, 8); o += 8;
                PutU16(b, o, (ushort)afidAnimId); PutU16(b, o + 2, 0);
                PutU32(b, o + 4, 999999); o += 8;
            }
            return b;
        }

        /// <summary>One int16 of a packed quaternion. 1.0 encodes as -1, 0.0 as 32767.</summary>
        static void PutQuatComponent(byte[] b, int o, float value)
        {
            int v = value >= 0.999f ? -1 : (int)Math.Round(value * 32767f) + 32767;
            PutU16(b, o, unchecked((ushort)v));
        }

        /// <summary>
        /// A model with a real sequence table, two global sequences, and bone tracks: bone 0 holds
        /// still, bone 1 has an ordinary rotation track on the idle sequence, and bone 2 has a
        /// translation track bound to global sequence 1.
        /// </summary>
        static byte[] BuildAnimatedM2(short[] animIds, bool standTrack, int afidAnimId = -1,
                                      int noKeysInM2 = -1)
        {
            int nSeq = animIds.Length;
            byte[] b = BuildM2Payload(3, boneCount: 3);
            int boneOffset = 0x100 + 3 * 48 + 16 + 4 + 2;

            // Which sequence the parser will pick, so the fixture can put its keys there.
            int idle = 0;
            for (int i = 0; i < nSeq; i++)
                if (animIds[i] == 0) { idle = i; break; }

            int baseLen = b.Length;
            int oGlobals = baseLen;                      // 2 uint32
            int oSeqs = oGlobals + 8;                    // nSeq * 64
            int oRotTimeHdr = oSeqs + nSeq * 64;         // nSeq * 8
            int oRotKeyHdr = oRotTimeHdr + nSeq * 8;     // nSeq * 8
            int oRotTimes = oRotKeyHdr + nSeq * 8;       // 2 * 4
            int oRotKeys = oRotTimes + 8;                // 2 * 8
            int oTransTimeHdr = oRotKeys + 16;           // 1 * 8
            int oTransKeyHdr = oTransTimeHdr + 8;        // 1 * 8
            int oTransTimes = oTransKeyHdr + 8;          // 2 * 4
            int oTransKeys = oTransTimes + 8;            // 2 * 12
            int total = oTransKeys + 24;
            Array.Resize(ref b, total);

            PutU32(b, oGlobals, 1000); PutU32(b, oGlobals + 4, 500);
            PutU32(b, 0x14, 2); PutU32(b, 0x18, (uint)oGlobals);

            for (int i = 0; i < nSeq; i++)
            {
                int o = oSeqs + i * 64;
                PutU16(b, o, unchecked((ushort)animIds[i]));   // animId
                PutU16(b, o + 2, 0);                            // subAnimId
                PutU32(b, o + 4, 1000);                         // length
                // 0x20 says the keyframes are in this file; clearing it is how a sequence says
                // they are somewhere else.
                PutU32(b, o + 12, (i == noKeysInM2) ? 0u : 0x20u);
            }
            PutU32(b, 0x1C, (uint)nSeq); PutU32(b, 0x20, (uint)oSeqs);

            // bone 1: rotation, linear, ordinary track with keys on the idle sequence only
            int rot = boneOffset + 1 * 88 + 36;
            PutU16(b, rot, 1);                                  // interpolation: linear
            PutU16(b, rot + 2, 0xFFFF);                         // globalSequence: -1
            PutU32(b, rot + 4, (uint)nSeq); PutU32(b, rot + 8, (uint)oRotTimeHdr);
            PutU32(b, rot + 12, (uint)nSeq); PutU32(b, rot + 16, (uint)oRotKeyHdr);
            if (standTrack)
            {
                PutU32(b, oRotTimeHdr + idle * 8, 2);
                PutU32(b, oRotTimeHdr + idle * 8 + 4, (uint)oRotTimes);
                PutU32(b, oRotKeyHdr + idle * 8, 2);
                PutU32(b, oRotKeyHdr + idle * 8 + 4, (uint)oRotKeys);
            }
            PutU32(b, oRotTimes, 0); PutU32(b, oRotTimes + 4, 400);
            for (int k = 0; k < 2; k++)
            {
                int o = oRotKeys + k * 8;
                PutQuatComponent(b, o, 0f); PutQuatComponent(b, o + 2, 0f);
                PutQuatComponent(b, o + 4, 0f); PutQuatComponent(b, o + 6, 1f);
            }

            // bone 2: translation bound to global sequence 1, keys at entry 0
            int tr = boneOffset + 2 * 88 + 16;
            PutU16(b, tr, 1);
            PutU16(b, tr + 2, 1);                               // globalSequence 1
            PutU32(b, tr + 4, 1); PutU32(b, tr + 8, (uint)oTransTimeHdr);
            PutU32(b, tr + 12, 1); PutU32(b, tr + 16, (uint)oTransKeyHdr);
            PutU32(b, oTransTimeHdr, 2); PutU32(b, oTransTimeHdr + 4, (uint)oTransTimes);
            PutU32(b, oTransKeyHdr, 2); PutU32(b, oTransKeyHdr + 4, (uint)oTransKeys);
            PutU32(b, oTransTimes, 0); PutU32(b, oTransTimes + 4, 250);
            PutF32(b, oTransKeys + 12, 1f);                     // second key moves 1 unit on X

            return WrapChunked(b, null, null, 0, afidAnimId);
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
            log.Add("Animation");
            AnimationTests();

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

        /// <summary>
        /// The rotation conversion, verified the long way round.
        ///
        /// A quaternion formula that mixes up a sign is not something you can eyeball, so this
        /// builds the rotation matrix in WoW space, conjugates it by the axis map (R_unity =
        /// M * R_wow * M^-1, the definition of "the same rotation seen in the other basis"), and
        /// checks the converted quaternion produces that same matrix. If ConvertRotation ever
        /// drifts, this fails with an actual number rather than a model that looks wrong.
        /// </summary>
        static void RotationConversionTests()
        {
            // A handful of rotations about assorted axes, none of them axis-aligned by accident.
            float[][] axes =
            {
                new[] { 0f, 0f, 1f }, new[] { 1f, 0f, 0f }, new[] { 0f, 1f, 0f },
                new[] { 0.267f, 0.535f, 0.802f }, new[] { -0.577f, 0.577f, -0.577f },
            };
            float[] angles = { 0.3f, 1.1f, -0.7f, 2.4f };
            int checkedCases = 0;
            float worst = 0f;

            foreach (float[] axis in axes)
            {
                foreach (float angle in angles)
                {
                    float s = (float)Math.Sin(angle / 2), cw = (float)Math.Cos(angle / 2);
                    var q = new WowQuat(axis[0] * s, axis[1] * s, axis[2] * s, cw);

                    float[,] rWow = MatrixFromQuat(q.X, q.Y, q.Z, q.W);
                    float[,] expected = Conjugate(rWow);

                    float ux, uy, uz, uw;
                    WowCoordinateConverter.ConvertRotation(q, out ux, out uy, out uz, out uw);
                    float[,] actual = MatrixFromQuat(ux, uy, uz, uw);

                    for (int r = 0; r < 3; r++)
                        for (int cc = 0; cc < 3; cc++)
                            worst = Math.Max(worst, Math.Abs(expected[r, cc] - actual[r, cc]));
                    checkedCases++;
                }
            }
            Check(worst < 1e-4f, "coords: rotation conversion matches the matrix route (worst " +
                                 worst.ToString("E2") + " over " + checkedCases + " cases)");

            // A unit quaternion must stay one: the map is a permutation with signs, nothing more.
            float qx, qy, qz, qw;
            WowCoordinateConverter.ConvertRotation(new WowQuat(0.5f, 0.5f, 0.5f, 0.5f),
                                                   out qx, out qy, out qz, out qw);
            Near((float)Math.Sqrt(qx * qx + qy * qy + qz * qz + qw * qw), 1f,
                 "coords: rotation stays unit length");

            // Identity in, identity out -- the rest pose depends on it.
            WowCoordinateConverter.ConvertRotation(WowQuat.Identity, out qx, out qy, out qz, out qw);
            Check(Math.Abs(qx) < 1e-6f && Math.Abs(qy) < 1e-6f && Math.Abs(qz) < 1e-6f &&
                  Math.Abs(qw - 1f) < 1e-6f, "coords: identity rotation is unchanged");

            // Scale permutes with the axes and is never negated.
            float sx, sy, sz;
            WowCoordinateConverter.ConvertScale(new WowVec3(2f, 3f, 4f), out sx, out sy, out sz);
            Check(sx == 3f && sy == 4f && sz == 2f, "coords: scale follows the axis permutation");
        }

        /// <summary>Rotation matrix from a quaternion, columns in x/y/z order.</summary>
        static float[,] MatrixFromQuat(float x, float y, float z, float w)
        {
            return new float[3, 3]
            {
                { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w) },
                { 2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                { 2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y) },
            };
        }

        /// <summary>M * m * M^-1 for the WoW->Unity axis map (x,y,z) -> (-y, z, x).</summary>
        static float[,] Conjugate(float[,] m)
        {
            // M as a matrix, and its inverse (which is its transpose: it is orthogonal).
            float[,] M = { { 0, -1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } };
            float[,] Mt = { { 0, 0, 1 }, { -1, 0, 0 }, { 0, 1, 0 } };
            return Multiply(Multiply(M, m), Mt);
        }

        static float[,] Multiply(float[,] a, float[,] b)
        {
            var r = new float[3, 3];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    for (int k = 0; k < 3; k++)
                        r[i, j] += a[i, k] * b[k, j];
            return r;
        }

        static void AnimationTests()
        {
            RotationConversionTests();

            // Sequence table and the idle rule: the FIRST sequence whose animId is 0, not
            // sequence 0. The fixture puts "Stand" third on purpose.
            byte[] file = BuildAnimatedM2(new[] { (short)5, (short)4, (short)0, (short)1 },
                                          standTrack: true);
            M2ParsedModel m = M2Parser.Parse(file);
            Check(m.Sequences.Length == 4, "anim: sequence table read");
            Check(m.Sequences[0].AnimId == 5 && m.Sequences[0].Length == 1000, "anim: sequence fields read");
            Check(m.AnimatedSequence == 2, "anim: idle is the first Stand sequence, not sequence 0");
            Check(m.AnimationSkipReason == null, "anim: nothing skipped");
            Check(m.GlobalSequences.Length == 2 && m.GlobalSequences[1] == 500,
                  "anim: global sequences read");

            // The track for that sequence, and only that sequence.
            M2Track<WowQuat> rot = m.Bones[1].Rotation;
            Check(rot.HasData, "anim: bone rotation track read for the idle");
            Check(rot.Times.Length == 2 && rot.Times[1] == 400, "anim: keyframe times read");
            Check(rot.Interpolation == M2Interpolation.Linear, "anim: interpolation type read");
            Check(!rot.IsGlobal, "anim: ordinary track is not global");
            Check(Math.Abs(rot.Values[0].W - 1f) < 1e-3f, "anim: packed quaternion unpacked");
            Check(m.Bones[1].IsAnimated && !m.Bones[0].IsAnimated,
                  "anim: only the bone with keys is animated");

            // A track bound to a global sequence reads entry 0 whatever is playing.
            M2Track<WowVec3> glob = m.Bones[2].Translation;
            Check(glob.IsGlobal && glob.GlobalSequence == 1, "anim: global sequence id read");
            Check(glob.HasData && glob.Times.Length == 2, "anim: global track reads entry 0");

            // No Stand sequence -> fall back to sequence 0.
            M2ParsedModel noStand = M2Parser.Parse(BuildAnimatedM2(new[] { (short)5, (short)4 },
                                                                   standTrack: false));
            Check(noStand.AnimatedSequence == 0, "anim: no Stand sequence falls back to sequence 0");

            // A model with no sequences at all is not animated, and says why.
            M2ParsedModel none = M2Parser.Parse(BuildM2Payload(3, boneCount: 2));
            Check(none.AnimatedSequence == -1 && none.AnimationSkipReason != null,
                  "anim: a model with no sequences reports why it is not animated");

            // An idle whose keyframes live in a separate .anim file is refused rather than read
            // out of the wrong buffer. Real data marks such a sequence by CLEARING 0x20 as well as
            // naming the file, and the fixture matches that.
            byte[] external = BuildAnimatedM2(new[] { (short)5, (short)0 }, standTrack: true,
                                              afidAnimId: 0, noKeysInM2: 1);
            M2ParsedModel ext = M2Parser.Parse(external);
            Check(ext.AnimatedSequence == -1, "anim: an idle with an AFID entry is skipped");
            Check(ext.AnimationSkipReason != null && ext.AnimationSkipReason.Contains(".anim"),
                  "anim: the skip reason names the .anim file");

            SelectedSequenceTests();
        }

        /// <summary>
        /// Following the app's animation selection: the requested sequence is parsed instead of the
        /// idle, and anything that cannot be played falls back to the idle rather than to nothing.
        /// </summary>
        static void SelectedSequenceTests()
        {
            // Asking for a sequence gets that sequence, not the idle.
            byte[] file = BuildAnimatedM2(new[] { (short)5, (short)4, (short)0, (short)1 },
                                          standTrack: true);
            Check(M2Parser.Parse(file).AnimatedSequence == 2, "select: no request still means the idle");
            M2ParsedModel picked = M2Parser.Parse(file, 1);
            Check(picked.AnimatedSequence == 1, "select: the requested sequence is the one parsed");
            Check(picked.AnimationSkipReason == null, "select: nothing skipped for a playable request");
            Check(picked.Sequences[picked.AnimatedSequence].AnimId == 4, "select: the right entry");

            // The keys move with the request: the fixture puts its rotation track on the idle only,
            // so another sequence parses that bone with no track at all.
            Check(!picked.Bones[1].Rotation.HasData,
                  "select: a sequence with no keys for a bone reads an empty track");
            Check(M2Parser.Parse(file, 2).Bones[1].Rotation.HasData,
                  "select: the sequence that does have keys still reads them");

            // A track bound to a global sequence is unaffected by which animation is selected --
            // it always reads entry 0.
            Check(M2Parser.Parse(file, 1).Bones[2].Translation.HasData &&
                  M2Parser.Parse(file, 3).Bones[2].Translation.HasData,
                  "select: a global-sequence track is read whichever sequence is chosen");

            // Out of range falls back to the idle, and says so.
            M2ParsedModel far = M2Parser.Parse(file, 99);
            Check(far.AnimatedSequence == 2, "select: an out-of-range request falls back to the idle");
            Check(far.AnimationSkipReason != null && far.AnimationSkipReason.Contains("99"),
                  "select: the fallback reason names the sequence that was asked for");

            // So does a request whose keyframes are in an .anim file.
            byte[] external = BuildAnimatedM2(new[] { (short)5, (short)0 }, standTrack: true,
                                              afidAnimId: 5, noKeysInM2: 0);
            M2ParsedModel ext = M2Parser.Parse(external, 0);
            Check(ext.AnimatedSequence == 1, "select: a request needing an .anim file falls back to the idle");
            Check(ext.AnimationSkipReason != null && ext.AnimationSkipReason.Contains(".anim"),
                  "select: that fallback reason names the .anim file");

            // And so does one whose keys are simply not in this file, with no .anim named for it.
            // This is the case that matters: its track headers ARE here and its offsets land in
            // range, so only the 0x20 flag distinguishes it from a playable sequence.
            byte[] notHere = BuildAnimatedM2(new[] { (short)5, (short)0 }, standTrack: true, noKeysInM2: 0);
            M2ParsedModel nh = M2Parser.Parse(notHere, 0);
            Check(nh.AnimatedSequence == 1, "select: a sequence without the 0x20 flag falls back to the idle");
            Check(nh.AnimationSkipReason != null && nh.AnimationSkipReason.Contains("0x20"),
                  "select: that fallback reason names the missing flag");
            Check(M2Parser.Parse(BuildAnimatedM2(new[] { (short)5, (short)0 }, standTrack: true,
                                                 noKeysInM2: 1)).AnimatedSequence == -1,
                  "select: an idle without the 0x20 flag is not played at all");
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
