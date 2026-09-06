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
                                ushort batchTextureCount = 1, ushort submeshId = 0,
                                ushort submeshLevel = 0, ushort submeshIndexStart = 0,
                                byte batchFlags = 0, ushort batchTransformCombo = 0)
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
            PutU16(b, subOffset + 2, submeshLevel);          // level: the high half of indexStart
            PutU16(b, subOffset + 4, 0);                     // vertexStart
            PutU16(b, subOffset + 6, (ushort)vertexCount);   // vertexCount
            PutU16(b, subOffset + 8, submeshIndexStart);     // indexStart: the low half
            PutU16(b, subOffset + 10, submeshIndexCount == 0xFFFF ? (ushort)triangles.Length : submeshIndexCount);

            b[batchOffset] = batchFlags;                     // flags (0x10 = STATIC)
            PutU16(b, batchOffset + 4, 0);                   // submeshIndex
            PutU16(b, batchOffset + 8, batchColorIndex);     // colorIndex
            PutU16(b, batchOffset + 14, batchTextureCount);  // textureCount
            PutU16(b, batchOffset + 16, 0);                  // textureComboIndex
            PutU16(b, batchOffset + 18, 0xFFFF);             // textureCoordComboIndex
            PutU16(b, batchOffset + 20, 0);                  // textureWeightComboIndex
            PutU16(b, batchOffset + 22, batchTransformCombo); // textureTransformComboIndex
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
            WideIndexStartTests();
            log.Add("BlpDecoder");
            BlpTests();
            log.Add("WowCoordinateConverter");
            CoordinateTests();
            log.Add("M2ShaderTable");
            ShaderTableTests();
            log.Add("Visibility tracks");
            VisibilityTests();
            MaterialTrackTests();
            MaterialSequenceTests();
            CrossSequenceGateTests();
            WeightLookupTests();
            MaterialLifecycleTests();
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

            // Changing the animation goes through ReadAnimationInto, which re-reads the bone
            // tracks and NOTHING else. It has to land on exactly what a full parse of the same
            // sequence would have produced -- that equivalence is the whole basis for using the
            // cheap path -- and it has to leave the rest of the model untouched, because the skin
            // path keeps reading it.
            M2ParsedModel live = M2Parser.Parse(file, 2);
            M2TextureDef[] texturesBefore = live.Textures;
            M2Vertex[] verticesBefore = live.Vertices;
            byte[] payloadBefore = live.Md21Payload;

            M2Parser.ReadAnimationInto(file, 1, live);
            M2ParsedModel full = M2Parser.Parse(file, 1);
            Check(live.AnimatedSequence == full.AnimatedSequence,
                  "readInto: resolves the same sequence a full parse does");
            Check(live.Bones.Length == full.Bones.Length,
                  "readInto: reads the same number of bones");
            Check(live.Bones[1].Rotation.HasData == full.Bones[1].Rotation.HasData &&
                  live.Bones[2].Translation.HasData == full.Bones[2].Translation.HasData,
                  "readInto: the tracks it reads match the full parse");
            Check(ReferenceEquals(live.Textures, texturesBefore) &&
                  ReferenceEquals(live.Vertices, verticesBefore),
                  "readInto: leaves the mesh and texture data alone");
            Check(ReferenceEquals(live.Md21Payload, payloadBefore),
                  "readInto: reuses the payload rather than slicing a new one");

            // Same fallbacks as the full parse, reported the same way.
            M2Parser.ReadAnimationInto(file, 99, live);
            Check(live.AnimatedSequence == 2 && live.AnimationSkipReason != null &&
                  live.AnimationSkipReason.Contains("99"),
                  "readInto: an out-of-range request falls back to the idle and says so");
            M2Parser.ReadAnimationInto(file, 2, live);
            Check(live.AnimatedSequence == 2 && live.AnimationSkipReason == null,
                  "readInto: a playable request clears the previous skip reason");

            // EXTERNAL .anim FILES. A sequence without the 0x20 flag keeps its track HEADERS in
            // the .m2 -- counts and offsets, per sequence, exactly where an in-file sequence keeps
            // them -- but those offsets address a .anim file's bytes. So the same headers are read
            // either way, and only the buffer the entries come out of changes. Agronn's
            // SitGroundDown is the model case: it plays in the legacy viewport and used to fall
            // back to the idle here.
            byte[] withAfid = BuildAnimatedM2(new[] { (short)5, (short)0 }, standTrack: true,
                                              afidAnimId: 5, noKeysInM2: 0);
            M2ParsedModel afidModel = M2Parser.Parse(withAfid, 1);
            Check(afidModel.AnimFileIds.Length >= 1, "afid: the chunk is read into the model");
            Check(M2Parser.ExternalAnimFileId(afidModel, 0) != 0,
                  "afid: an external sequence reports the file that holds its keys");
            Check(M2Parser.ExternalAnimFileId(afidModel, 1) == 0,
                  "afid: an in-file sequence reports no external file");

            // Without the bytes it still falls back, and says why -- now naming the fetch rather
            // than claiming the milestone cannot do it.
            M2ParsedModel notFetched = M2Parser.Parse(withAfid, 0);
            Check(notFetched.AnimatedSequence == 1 && notFetched.AnimationSkipReason != null &&
                  notFetched.AnimationSkipReason.Contains(".anim"),
                  "afid: without the bytes the request still falls back to the idle");
            Check(notFetched.RequiredAnimFileId != 0 ||
                  M2Parser.ExternalAnimFileId(notFetched, 0) != 0,
                  "afid: the file that would be needed is reported so it can be fetched");

            // WITH the bytes it plays. The fixture's .anim carries the same keyframe bytes the
            // .m2 would have, so the sequence resolves to itself rather than the idle.
            byte[] animBytes = BuildAnimFileFor(withAfid);
            M2ParsedModel fetched = M2Parser.Parse(withAfid, 0, animBytes);
            Check(fetched.AnimatedSequence == 0,
                  "afid: with the .anim bytes the requested sequence plays");
            Check(fetched.AnimationSkipReason == null,
                  "afid: and nothing is reported as skipped");

            // ReadAnimationInto takes the same bytes, so switching to an external animation costs
            // no more than switching to an in-file one.
            M2ParsedModel live2 = M2Parser.Parse(withAfid, 1);
            M2Parser.ReadAnimationInto(withAfid, 0, live2, animBytes);
            Check(live2.AnimatedSequence == 0,
                  "afid: ReadAnimationInto plays an external sequence too");

            // Bytes fetched for a sequence that then falls back must NOT be read as though they
            // belonged to the idle -- that would pose the model from the wrong file.
            M2ParsedModel wrongWay = M2Parser.Parse(withAfid, 99, animBytes);
            Check(wrongWay.AnimatedSequence == 1,
                  "afid: an out-of-range request still falls back to the in-file idle");

            // A skeleton-file model has no bone array in THIS file to re-read, so the request is
            // refused rather than answered from whatever the header's bone array happens to hold.
            M2ParsedModel skel = M2Parser.Parse(file, 2);
            skel.SkeletonFileDataID = 4242;
            M2Parser.ReadAnimationInto(file, 0, skel);
            Check(skel.AnimatedSequence == -1 && skel.AnimationSkipReason != null &&
                  skel.AnimationSkipReason.Contains("skeleton file"),
                  "readInto: a skeleton-file model stays unanimated");
        }

        /// <summary>
        /// A stand-in .anim file for the fixture: a copy of the .m2's own bytes.
        ///
        /// That is not a shortcut, it is the shape of the thing. A real .anim holds only keyframe
        /// ENTRIES, addressed by the offsets in the .m2's track headers; copying the .m2 gives a
        /// buffer where those same offsets resolve to the same keys, which is exactly what the
        /// reader has to cope with. If the reader wrongly used the .m2 for the entries the test
        /// would still pass -- so the tests above also check the cases where the two must differ.
        /// </summary>
        static byte[] BuildAnimFileFor(byte[] m2)
        {
            var copy = new byte[m2.Length];
            System.Buffer.BlockCopy(m2, 0, copy, 0, m2.Length);
            return copy;
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

        /// <summary>
        /// A skin whose triangle array runs past 65535. The submesh's first index does not fit in
        /// the field that holds it, so the format puts the overflow in the Level word; reading the
        /// stored half alone lands on a different submesh's triangles and every bounds check still
        /// passes, because a wrapped start is a small in-range number. That is the whole bug, and
        /// this is the case that proves it either way.
        /// </summary>
        static void WideIndexStartTests()
        {
            const int total = 65541;            // 21847 triangles: past the 16-bit ceiling
            var tris = new ushort[total];
            for (int i = 0; i < total; i++)
                tris[i] = (ushort)(i % 3);
            // The triangle the submesh should draw, and a decoy at the wrapped start.
            tris[65538] = 0; tris[65539] = 1; tris[65540] = 2;
            tris[2] = 2; tris[3] = 2; tris[4] = 2;

            // level 1, stored start 2  ->  whole start 65538
            M2ParsedSkin skin = M2SkinParser.Parse(
                BuildSkin(3, tris, 3, 0xFFFF, 0xFFFF, 1, 0, 1, 2));
            M2Submesh s = skin.Submeshes[0];
            Check(s.Level == 1, "wide index: the level word is read");
            Check(s.RawIndexStart == 2, "wide index: the stored half is kept as stored");
            Check(s.IndexStart == 65538, "wide index: the start is expanded past 16 bits");

            int[] drawn = M2SkinParser.BuildTriangles(skin, s, 3);
            Check(drawn.Length == 3 && drawn[0] == 0 && drawn[1] == 1 && drawn[2] == 2,
                  "wide index: triangles come from the expanded start, not the wrapped one");

            Check(skin.IndexSurvey.NonZeroLevel == 1, "wide index: survey counts the level word");
            Check(skin.IndexSurvey.MaxIndexStart == 65538, "wide index: survey records the start");
            Check(skin.IndexSurvey.ExercisesWideStarts, "wide index: survey flags the model");
            // This fixture has one submesh starting at 65538, so the legacy viewport's running-sum
            // reading (which would say 0) disagrees -- exactly what the cross-check is for.
            Check(!skin.IndexSurvey.CumulativeAgrees && skin.IndexSurvey.DisagreeAt == 0,
                  "wide index: the running-sum cross-check notices the difference");

            // An ordinary skin: the two readings agree and nothing is flagged.
            var small = new ushort[] { 0, 1, 2, 0, 1, 2 };
            M2ParsedSkin plain = M2SkinParser.Parse(BuildSkin(3, small));
            Check(plain.Submeshes[0].IndexStart == 0, "wide index: an ordinary start is unchanged");
            Check(plain.IndexSurvey.NonZeroLevel == 0 && !plain.IndexSurvey.ExercisesWideStarts,
                  "wide index: an ordinary skin is not flagged");
            Check(plain.IndexSurvey.CumulativeAgrees && plain.IndexSurvey.DisagreeAt == -1,
                  "wide index: the two readings agree on an ordinary skin");
            Check(plain.IndexSurvey.GeosetZero == 1, "wide index: survey counts geoset 0 submeshes");
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


        // ---------------------------------------------------------------- material animation

        /// <summary>
        /// A growable M2 payload with a key pool, so a fixture can carry REAL tracks -- several
        /// keys, several sequences, global sequences -- without hand-computing every offset.
        /// </summary>
        sealed class M2Fixture
        {
            public byte[] B;
            public int Pool;
            public M2Fixture(int fixedSize) { B = new byte[fixedSize + 4096]; Pool = fixedSize; }

            int Take(int n)
            {
                int o = Pool;
                Pool += n;
                if (Pool > B.Length) Array.Resize(ref B, Pool * 2);
                return o;
            }

            /// <summary>
            /// Write one M2Track at trackOffset. times[s]/values[s] are sequence s's keys; a null
            /// entry is a sequence with no keys. values[s] is already encoded (fixed16, vec3, quat).
            /// </summary>
            public void Track(int trackOffset, ushort interpolation, short globalSeq, uint[][] times, byte[][] values)
            {
                int nSeq = times.Length;
                PutU16(B, trackOffset, interpolation);
                PutU16(B, trackOffset + 2, unchecked((ushort)globalSeq));
                int th = Take(nSeq * 8), vh = Take(nSeq * 8);
                PutU32(B, trackOffset + 4, (uint)nSeq); PutU32(B, trackOffset + 8, (uint)th);
                PutU32(B, trackOffset + 12, (uint)nSeq); PutU32(B, trackOffset + 16, (uint)vh);
                for (int s = 0; s < nSeq; s++)
                {
                    if (times[s] == null)
                    {
                        PutU32(B, th + s * 8, 0); PutU32(B, th + s * 8 + 4, 0);
                        PutU32(B, vh + s * 8, 0); PutU32(B, vh + s * 8 + 4, 0);
                        continue;
                    }
                    int to = Take(times[s].Length * 4);
                    for (int k = 0; k < times[s].Length; k++) PutU32(B, to + k * 4, times[s][k]);
                    int vo = Take(values[s].Length);
                    Buffer.BlockCopy(values[s], 0, B, vo, values[s].Length);
                    PutU32(B, th + s * 8, (uint)times[s].Length); PutU32(B, th + s * 8 + 4, (uint)to);
                    PutU32(B, vh + s * 8, (uint)times[s].Length); PutU32(B, vh + s * 8 + 4, (uint)vo);
                }
            }

            public byte[] Done() { Array.Resize(ref B, Pool); return B; }
        }

        static uint[] T(params uint[] t) { return t; }

        /// <summary>fixed16 keys: 32767 is 1.0, as ShortToFloat reads them.</summary>
        static byte[] F16(params float[] v)
        {
            var b = new byte[v.Length * 2];
            for (int i = 0; i < v.Length; i++) PutU16(b, i * 2, unchecked((ushort)(short)Math.Round(v[i] * 32767f)));
            return b;
        }

        /// <summary>vec3 keys, three floats each.</summary>
        static byte[] V3(params float[] xyz)
        {
            var b = new byte[xyz.Length * 4];
            for (int i = 0; i < xyz.Length; i++) PutF32(b, i * 4, xyz[i]);
            return b;
        }

        /// <summary>
        /// An M2 with two sequences (Stand and one more), two global sequences (1000 ms, 500 ms),
        /// two colour entries, two texture weights, two texture transforms and a four-entry
        /// transform lookup that includes the 0xFFFF sentinel and an out-of-range value. The
        /// keys are chosen so that every "which sequence does this track follow" question has a
        /// different answer for entry 0 and entry 1.
        /// </summary>
        static byte[] BuildMaterialM2(bool badTransformOffset = false, bool badLookupOffset = false)
        {
            const int nSeq = 2;
            const int headerSize = 0x100;
            int oGlobals = headerSize;                       // 2 * 4
            int oSeqs = oGlobals + 8;                        // nSeq * 64
            int oVerts = oSeqs + nSeq * 64;                  // 1 * 48
            int oTex = oVerts + 48;                          // 1 * 16
            int oMat = oTex + 16;                            // 4
            int oTexLookup = oMat + 4;                       // 1 * 2
            int oColors = oTexLookup + 2;                    // 2 * 40
            int oWeights = oColors + 2 * 40;                 // 2 * 20
            int oWeightLookup = oWeights + 2 * 20;           // 2 * 2
            int oXf = oWeightLookup + 4;                     // 2 * 60
            int oXfLookup = oXf + 2 * 60;                    // 4 * 2
            int fixedEnd = oXfLookup + 8;

            var f = new M2Fixture(fixedEnd);
            byte[] b = f.B;
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x14, 2); PutU32(b, 0x18, (uint)oGlobals);
            PutU32(b, oGlobals, 1000); PutU32(b, oGlobals + 4, 500);
            PutU32(b, 0x1C, nSeq); PutU32(b, 0x20, (uint)oSeqs);
            for (int i = 0; i < nSeq; i++)
            {
                int o = oSeqs + i * 64;
                PutU16(b, o, (ushort)i);                     // animId 0 = Stand
                PutU32(b, o + 4, 1000);                      // length
                PutU32(b, o + 12, 0x20);                     // keys are in this file
            }
            PutU32(b, 0x2C, 0); PutU32(b, 0x30, (uint)headerSize);          // no bones
            PutU32(b, 0x3C, 1); PutU32(b, 0x40, (uint)oVerts);
            PutF32(b, oVerts + 28, 1f); PutF32(b, oVerts + 32, 0.25f); PutF32(b, oVerts + 36, 0.75f);
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, 1); PutU32(b, 0x54, (uint)oTex);
            PutU32(b, oTex, 11); PutU32(b, oTex + 4, 3);
            PutU32(b, 0x70, 1); PutU32(b, 0x74, (uint)oMat);
            PutU16(b, oMat + 2, 2);                          // blend: alpha
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)oTexLookup);
            PutU32(b, 0x48, 2); PutU32(b, 0x4C, (uint)oColors);
            PutU32(b, 0x58, 2); PutU32(b, 0x5C, (uint)oWeights);
            PutU32(b, 0x90, 2); PutU32(b, 0x94, (uint)oWeightLookup);
            PutU16(b, oWeightLookup, 0); PutU16(b, oWeightLookup + 2, 1);
            PutU32(b, 0x60, 2); PutU32(b, 0x64, badTransformOffset ? 0x7FFFFFF0u : (uint)oXf);
            PutU32(b, 0x98, 4); PutU32(b, 0x9C, badLookupOffset ? 0x7FFFFFF0u : (uint)oXfLookup);
            PutU16(b, oXfLookup, 0); PutU16(b, oXfLookup + 2, 0xFFFF);
            PutU16(b, oXfLookup + 4, 1); PutU16(b, oXfLookup + 6, 7);

            // colour 0: RGB red->blue on the idle, green on sequence 1; alpha 1->0 on the idle,
            //           0.5 on sequence 1
            f.Track(oColors, 1, -1,
                    new[] { T(0, 1000), T(0) },
                    new[] { V3(1, 0, 0, 0, 0, 1), V3(0, 1, 0) });
            f.Track(oColors + 20, 1, -1,
                    new[] { T(0, 500), T(0) },
                    new[] { F16(1f, 0f), F16(0.5f) });
            // colour 1: no RGB keys anywhere; alpha on global sequence 1 (keys at entry 0)
            f.Track(oColors + 40, 1, -1, new uint[][] { null, null }, new byte[][] { null, null });
            f.Track(oColors + 60, 1, 1,
                    new[] { T(0, 250), null },
                    new[] { F16(0.25f, 0.75f), null });
            // weight 0: 0->1 on the idle, 0.5 on sequence 1 (DIFFERS: the legacy rule never reads it)
            f.Track(oWeights, 1, -1,
                    new[] { T(0, 400), T(0) },
                    new[] { F16(0f, 1f), F16(0.5f) });
            // weight 1: identical single key on both sequences
            f.Track(oWeights + 20, 0, -1,
                    new[] { T(0), T(0) },
                    new[] { F16(1f), F16(1f) });
            // transform 0: translation follows the sequence, rotation one quaternion key,
            //              scale on global sequence 1
            f.Track(oXf, 1, -1,
                    new[] { T(0, 500), T(0, 1000) },
                    new[] { V3(0, 0, 0, 0.5f, 0.25f, 0), V3(0, 0, 0, 1, 0, 0) });
            f.Track(oXf + 20, 0, -1,
                    new[] { T(0), null },
                    new[] { V3(0, 0, 0.7071f, 0.7071f), null });      // four floats: x y z w
            f.Track(oXf + 40, 1, 1,
                    new[] { T(0, 250), null },
                    new[] { V3(1, 1, 1, 2, 2, 1), null });
            // transform 1: nothing
            f.Track(oXf + 60, 0, -1, new uint[][] { null, null }, new byte[][] { null, null });
            f.Track(oXf + 80, 0, -1, new uint[][] { null, null }, new byte[][] { null, null });
            f.Track(oXf + 100, 0, -1, new uint[][] { null, null }, new byte[][] { null, null });
            return f.Done();
        }

        // ------------------------------------------------------------------ sequence change

        /// <summary>
        /// Two-sequence model for the sequence-change tests. One colour entry, one weight, one
        /// texture transform, one opaque material, one batch reading all three. In one sequence
        /// the batch is animated -- a non-identity UV transform, an opacity below 1 that falls to
        /// 0 at 500 ms (so the gate shuts), a weight of 1 -- and in the other it has NO keys for
        /// any sequence-following track. keyedSequence says which one is which.
        /// </summary>
        static byte[] BuildSwitchM2(int keyedSequence)
        {
            const int nSeq = 2;
            const int headerSize = 0x100;
            int oGlobals = headerSize;                       // 1 * 4
            int oSeqs = oGlobals + 4;                        // nSeq * 64
            int oVerts = oSeqs + nSeq * 64;                  // 1 * 48
            int oTex = oVerts + 48;                          // 1 * 16
            int oMat = oTex + 16;                            // 4
            int oTexLookup = oMat + 4;                       // 1 * 2
            int oColors = oTexLookup + 2;                    // 1 * 40
            int oWeights = oColors + 40;                     // 1 * 20
            int oWeightLookup = oWeights + 20;               // 1 * 2
            int oXf = oWeightLookup + 2;                     // 1 * 60
            int oXfLookup = oXf + 60;                        // 1 * 2
            int fixedEnd = oXfLookup + 2;

            var f = new M2Fixture(fixedEnd);
            byte[] b = f.B;
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x14, 1); PutU32(b, 0x18, (uint)oGlobals);
            PutU32(b, oGlobals, 1000);
            PutU32(b, 0x1C, nSeq); PutU32(b, 0x20, (uint)oSeqs);
            for (int i = 0; i < nSeq; i++)
            {
                int o = oSeqs + i * 64;
                PutU16(b, o, (ushort)i);                     // animId 0 = Stand, 1 = the other
                PutU32(b, o + 4, 1000);
                PutU32(b, o + 12, 0x20);                     // keys are in this file
            }
            PutU32(b, 0x2C, 0); PutU32(b, 0x30, (uint)headerSize);
            PutU32(b, 0x3C, 1); PutU32(b, 0x40, (uint)oVerts);
            PutF32(b, oVerts + 28, 1f); PutF32(b, oVerts + 32, 0.25f); PutF32(b, oVerts + 36, 0.75f);
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, 1); PutU32(b, 0x54, (uint)oTex);
            PutU32(b, oTex, 11); PutU32(b, oTex + 4, 3);
            PutU32(b, 0x70, 1); PutU32(b, 0x74, (uint)oMat);
            PutU16(b, oMat + 2, 0);                          // blend: opaque
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)oTexLookup);
            PutU32(b, 0x48, 1); PutU32(b, 0x4C, (uint)oColors);
            PutU32(b, 0x58, 1); PutU32(b, 0x5C, (uint)oWeights);
            PutU32(b, 0x90, 1); PutU32(b, 0x94, (uint)oWeightLookup);
            PutU16(b, oWeightLookup, 0);
            PutU32(b, 0x60, 1); PutU32(b, 0x64, (uint)oXf);
            PutU32(b, 0x98, 1); PutU32(b, 0x9C, (uint)oXfLookup);
            PutU16(b, oXfLookup, 0);

            int k = keyedSequence, e = 1 - keyedSequence;
            var times = new uint[nSeq][]; var vals = new byte[nSeq][];
            // colour RGB: one key at animation 0 (index 0 is what the legacy reads, whatever plays)
            f.Track(oColors, 0, -1, new[] { T(0), T(0) }, new[] { V3(0.5f, 0.5f, 0.5f), V3(0.5f, 0.5f, 0.5f) });
            // colour alpha: keyed sequence 0.5 -> 0 at 500 ms; the other sequence: no keys
            times[k] = T(0, 500); vals[k] = F16(0.5f, 0f); times[e] = null; vals[e] = null;
            f.Track(oColors + 20, 1, -1, (uint[][])times.Clone(), (byte[][])vals.Clone());
            // weight: 1 at animation 0 (sequence-independent)
            f.Track(oWeights, 0, -1, new[] { T(0), T(0) }, new[] { F16(1f), F16(1f) });
            // transform: translation (0.25, 0.5) and scale (2, 2) in the keyed sequence only
            times = new uint[nSeq][]; vals = new byte[nSeq][];
            times[k] = T(0); vals[k] = V3(0.25f, 0.5f, 0f);
            f.Track(oXf, 0, -1, (uint[][])times.Clone(), (byte[][])vals.Clone());
            f.Track(oXf + 20, 0, -1, new uint[][] { null, null }, new byte[][] { null, null });
            times = new uint[nSeq][]; vals = new byte[nSeq][];
            times[k] = T(0); vals[k] = V3(2f, 2f, 1f);
            f.Track(oXf + 40, 0, -1, (uint[][])times.Clone(), (byte[][])vals.Clone());
            return f.Done();
        }

        static bool Near(float a, float b) { return Math.Abs(a - b) < 1e-4f; }

        static void MaterialSequenceTests()
        {
            foreach (int keyed in new[] { 0, 1 })
            {
                int other = 1 - keyed;
                string shape = keyed == 0 ? "keys in 0, none in 1" : "keys in 1, none in 0";
                byte[] file = BuildSwitchM2(keyed);
                M2ParsedModel m = M2Parser.Parse(file, keyed);
                Check(m.AnimatedSequence == keyed, "switch (" + shape + "): parsed at the keyed sequence");
                int zero = 0;
                var s = new M2MaterialState();
                var batch = new M2Batch();
                batch.ColorIndex = 0; batch.TextureWeightComboIndex = 0; batch.TextureTransformComboIndex = 0;
                int w = M2MaterialEval.ResolveWeightIndex(m, batch);
                int x0 = M2MaterialEval.ResolveTransformIndex(m, batch, 0);
                Check(w == 0 && x0 == 0, "switch (" + shape + "): indices resolve through the lookups");

                // --- the keyed sequence: animated
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, 0f, 0.0, ref s, ref zero);
                Check(s.HasColor && Near(s.R, 0.5f), "switch (" + shape + "): colour present in the keyed sequence");
                Check(s.AlphaFromTrack && Near(s.OcolW, 0.5f) && Near(s.EcolW, 0.5f), "switch (" + shape + "): opacity 0.5 from the track at t=0");
                Check(s.Drawn, "switch (" + shape + "): gate open at t=0");
                Check(s.Uv0Applied && Near(s.T0x, 0.25f) && Near(s.T0y, 0.5f) && Near(s.S0x, 2f) && Near(s.S0y, 2f),
                      "switch (" + shape + "): non-identity UV transform in the keyed sequence");
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, 600f, 0.0, ref s, ref zero);
                Check(Near(s.OcolW, 0f) && !s.Drawn, "switch (" + shape + "): gate shut at t=600 (alpha 0)");

                // --- switch to the other sequence: everything falls back to the legacy defaults
                M2Parser.ReadAnimationInto(file, other, m);
                Check(m.AnimatedSequence == other, "switch (" + shape + "): re-read at the other sequence");
                Check(!m.Colors[0].Opacity.HasData && !m.TextureTransforms[0].IsAnimated,
                      "switch (" + shape + "): the other sequence has no keys for alpha or transform");
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, 600f, 0.0, ref s, ref zero);
                Check(s.HasColor && Near(s.R, 0.5f), "switch (" + shape + "): colour still present (index 0)");
                Check(!s.AlphaFromTrack && Near(s.OcolW, 1f) && Near(s.EcolW, 1f),
                      "switch (" + shape + "): opacity back to the default 1 (legacy: ocol.w keeps 1 when the track has no keys)");
                Check(s.Drawn, "switch (" + shape + "): gate open again");
                Check(!s.Uv0Applied && Near(s.T0x, 0f) && Near(s.T0y, 0f) && Near(s.S0x, 1f) && Near(s.S0y, 1f),
                      "switch (" + shape + "): UV transform back to identity (legacy: component not applied without keys)");

                // --- and back: the animated values return
                M2Parser.ReadAnimationInto(file, keyed, m);
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, 0f, 0.0, ref s, ref zero);
                Check(s.AlphaFromTrack && Near(s.OcolW, 0.5f) && s.Uv0Applied && Near(s.T0x, 0.25f) && Near(s.S0x, 2f),
                      "switch (" + shape + "): animated values restored on the way back");
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, 600f, 0.0, ref s, ref zero);
                Check(!s.Drawn, "switch (" + shape + "): gate shuts again at t=600");
            }
            // The value the UNLIT opaque write derives its blend state from: OcolW below 1 means
            // blending in place, exactly 1 means One/Zero and _OpaqueAlpha 1 -- both come from the
            // same field, so a sequence whose alpha track is absent yields 1 and therefore the
            // opaque state. (This fixture's material is lit -- flags 0 -- and a lit opaque pass
            // ignores OcolW for blending; the animator applies it to unlit passes only. What is
            // checked here is the evaluator's value.)
            {
                byte[] file = BuildSwitchM2(0);
                M2ParsedModel m = M2Parser.Parse(file, 0);
                int zero = 0; var s = new M2MaterialState();
                M2MaterialEval.Evaluate(m, 0, 0, 0, -1, 0f, 0.0, ref s, ref zero);
                bool blendIn0 = s.OcolW < 1f;
                M2Parser.ReadAnimationInto(file, 1, m);
                M2MaterialEval.Evaluate(m, 0, 0, 0, -1, 0f, 0.0, ref s, ref zero);
                bool blendIn1 = s.OcolW < 1f;
                Check(blendIn0 && !blendIn1, "switch: evaluator ocol.w below 1 in sequence 0 (an unlit opaque batch would blend in place) and 1 again in sequence 1 (One/Zero)");
            }
        }

        // ------------------------------------------------------------------ cross-sequence gate

        /// <summary>
        /// One colour entry whose alpha is constant zero in sequence 0 and, in sequence 1, either
        /// keyed above zero, keyed at zero, absent, or stored outside the file (flag 0x20 clear).
        /// mode: 0 = keys above zero, 1 = keys all zero, 2 = no keys, 3 = external, 4 = global track.
        /// </summary>
        static byte[] BuildGateM2(int mode)
        {
            const int nSeq = 2;
            const int headerSize = 0x100;
            int oGlobals = headerSize;                       // 1 * 4
            int oSeqs = oGlobals + 4;
            int oVerts = oSeqs + nSeq * 64;
            int oTex = oVerts + 48;
            int oMat = oTex + 16;
            int oTexLookup = oMat + 4;
            int oColors = oTexLookup + 2;                    // 1 * 40
            int fixedEnd = oColors + 40;
            var f = new M2Fixture(fixedEnd);
            byte[] b = f.B;
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x14, 1); PutU32(b, 0x18, (uint)oGlobals);
            PutU32(b, oGlobals, 1000);
            PutU32(b, 0x1C, nSeq); PutU32(b, 0x20, (uint)oSeqs);
            for (int i = 0; i < nSeq; i++)
            {
                int o = oSeqs + i * 64;
                PutU16(b, o, (ushort)i);
                PutU32(b, o + 4, 1000);
                PutU32(b, o + 12, (i == 1 && mode == 3) ? 0u : 0x20u);   // sequence 1 external in mode 3
            }
            PutU32(b, 0x2C, 0); PutU32(b, 0x30, (uint)headerSize);
            PutU32(b, 0x3C, 1); PutU32(b, 0x40, (uint)oVerts);
            PutF32(b, oVerts + 28, 1f); PutF32(b, oVerts + 32, 0.25f); PutF32(b, oVerts + 36, 0.75f);
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, 1); PutU32(b, 0x54, (uint)oTex);
            PutU32(b, oTex, 11); PutU32(b, oTex + 4, 3);
            PutU32(b, 0x70, 1); PutU32(b, 0x74, (uint)oMat);
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)oTexLookup);
            PutU32(b, 0x48, 1); PutU32(b, 0x4C, (uint)oColors);
            f.Track(oColors, 0, -1, new[] { T(0), T(0) }, new[] { V3(1f, 1f, 1f), V3(1f, 1f, 1f) });
            if (mode == 4)
                f.Track(oColors + 20, 0, 0, new[] { T(0), null }, new[] { F16(0f), null });
            else
                f.Track(oColors + 20, 1, -1,
                        new[] { T(0), mode == 2 ? null : T(0, 500) },
                        new[] { F16(0f), mode == 2 ? null : (mode == 1 ? F16(0f, 0f) : F16(0f, 1f)) });
            return f.Done();
        }

        static void CrossSequenceGateTests()
        {
            string[] names = { "keys above zero", "keys all zero", "no keys", "external keys", "global track" };
            bool[] expectOpen = { true, false, true, true, false };
            for (int mode = 0; mode < 5; mode++)
            {
                M2ParsedModel m = M2Parser.Parse(BuildGateM2(mode), 0);
                M2ColorDef c = m.Colors[0];
                Check(c.Opacity.HasData && c.Opacity.Values.Length == 1 && c.Opacity.Values[0] == 0f,
                      "gate scan (" + names[mode] + "): constant-zero alpha in the idle");
                Check(c.OpacityMayOpenElsewhere == expectOpen[mode],
                      "gate scan (" + names[mode] + "): may open elsewhere == " + expectOpen[mode]);
                var batch = new M2Batch();
                batch.ColorIndex = 0; batch.TextureWeightComboIndex = 0xFFFF; batch.TextureTransformComboIndex = 0xFFFF;
                Check(M2MaterialEval.GateMayOpenElsewhere(m, batch) == expectOpen[mode],
                      "gate scan (" + names[mode] + "): the build keeps the batch == " + expectOpen[mode]);
                if (mode == 3)
                    Check(c.OpacityOtherUnknown == 1 && c.OpacityOtherVisible == 0, "gate scan (external): counted as unknown, not visible");
                if (mode == 0 || mode == 2)
                    Check(c.OpacityOtherVisible == 1 && c.OpacityOtherUnknown == 0, "gate scan (" + names[mode] + "): counted as visible");
            }
            // sequence-independent hiders win: no RGB keys at animation 0, or weight 0 at animation 0
            {
                M2ParsedModel m = M2Parser.Parse(BuildGateM2(0), 0);
                var noRgb = new M2Batch();
                noRgb.ColorIndex = 0; noRgb.TextureWeightComboIndex = 0xFFFF; noRgb.TextureTransformComboIndex = 0xFFFF;
                m.Colors[0].Color = new M2Track<WowVec3> { Times = new uint[0], Values = new WowVec3[0] };
                Check(!M2MaterialEval.GateMayOpenElsewhere(m, noRgb), "gate scan: no RGB keys at animation 0 -> hidden everywhere, not kept");
                M2ParsedModel m2 = M2Parser.Parse(BuildGateM2(0), 0);
                m2.TextureWeightLookup = new ushort[] { 0 };
                m2.TextureWeightTracks = new[] { new M2Track<float> { Times = new uint[] { 0 }, Values = new float[] { 0f }, GlobalSequence = -1 } };
                var w0 = new M2Batch();
                w0.ColorIndex = 0; w0.TextureWeightComboIndex = 0; w0.TextureTransformComboIndex = 0xFFFF;
                Check(!M2MaterialEval.GateMayOpenElsewhere(m2, w0), "gate scan: weight 0 at animation 0 -> hidden everywhere, not kept");
            }
        }

        // ------------------------------------------------------------------ weight lookup

        static void WeightLookupTests()
        {
            var tracks = new M2Track<float>[3];
            for (int i = 0; i < 3; i++)
                tracks[i] = new M2Track<float> { Times = new uint[] { 0 }, Values = new float[] { 1f }, GlobalSequence = -1 };
            var m = new M2ParsedModel();
            m.TextureWeightTracks = tracks;
            m.TextureWeightLookup = new ushort[] { 2, 0xFFFF, 7 };
            var batch = new M2Batch();
            batch.ColorIndex = 0xFFFF; batch.TextureTransformComboIndex = 0xFFFF;
            batch.TextureWeightComboIndex = 0;
            Check(M2MaterialEval.ResolveWeightIndex(m, batch) == 2, "weight lookup: valid entry -> its track");
            batch.TextureWeightComboIndex = 1;
            Check(M2MaterialEval.ResolveWeightIndex(m, batch) == -1, "weight lookup: 0xFFFF entry -> none");
            batch.TextureWeightComboIndex = 2;
            Check(M2MaterialEval.ResolveWeightIndex(m, batch) == -1, "weight lookup: entry past the tracks -> none");
            batch.TextureWeightComboIndex = 3;
            Check(M2MaterialEval.ResolveWeightIndex(m, batch) == -1, "weight lookup: combo past a NON-EMPTY lookup -> none (no direct index)");
            m.TextureWeightLookup = new ushort[0];
            batch.TextureWeightComboIndex = 0;
            Check(M2MaterialEval.ResolveWeightIndex(m, batch) == -1, "weight lookup: absent table -> none (no direct index)");

            // malformed / partial table through the parser: rejected as a whole, so nothing resolves
            byte[] file = BuildMaterialM2();
            byte[] past = (byte[])file.Clone();
            PutU32(past, 0x94, 0x7FFFFFF0u);                 // lookup offset outside the file
            M2ParsedModel mp = M2Parser.Parse(past, 0);
            Check(mp.TextureWeightLookup.Length == 0, "weight lookup: table outside the file -> rejected");
            batch.TextureWeightComboIndex = 0;
            Check(M2MaterialEval.ResolveWeightIndex(mp, batch) == -1, "weight lookup: rejected table -> none");
            byte[] partial = (byte[])file.Clone();
            PutU32(partial, 0x90, 4000);                     // claims 4000 entries: does not fit
            M2ParsedModel mq = M2Parser.Parse(partial, 0);
            Check(mq.TextureWeightLookup.Length == 0, "weight lookup: partial table (count past the file) -> rejected");
            Check(M2MaterialEval.ResolveWeightIndex(mq, batch) == -1, "weight lookup: partial table -> none");
            // the intact fixture still resolves both of its entries
            M2ParsedModel ok = M2Parser.Parse(file, 0);
            batch.TextureWeightComboIndex = 1;
            Check(M2MaterialEval.ResolveWeightIndex(ok, batch) == 1, "weight lookup: intact table -> entry 1 -> track 1");
        }

        // ------------------------------------------------------------------ lifecycle (evaluator side)

        /// <summary>
        /// The synthetic two-sequence transform model the runtime self-test drives, checked here at
        /// the evaluator level: no keys in one sequence (identity, nothing to evaluate), keys in the
        /// other (the transform applied), both shapes, and the skin resolving the transform.
        /// </summary>
        static void MaterialLifecycleTests()
        {
            M2ParsedSkin skin = M2SkinParser.Parse(M2Synthetic.TransformSwitchSkin());
            Check(skin.Batches.Length == 1 && !skin.Batches[0].HasColor && skin.Batches[0].TextureTransformComboIndex == 0,
                  "lifecycle: synthetic skin has one batch with a transform combo and no colour entry");
            foreach (int keyed in new[] { 1, 0 })
            {
                const int other = 1;                        // the switches go to sequence 1 and back
                foreach (bool skinned in new[] { false, true })
                {
                    string v = "lifecycle (" + (skinned ? "skinned" : "static") + ", keys in " + keyed + ")";
                    byte[] file = M2Synthetic.TransformSwitchModel(keyed, skinned);
                    M2ParsedModel m = M2Parser.Parse(file, 0);
                    Check(m.Sequences.Length == 2 && m.AnimatedSequence == 0, v + ": parsed at sequence 0");
                    Check(m.Bones.Length == (skinned ? 1 : 0), v + ": bone count as built");
                    Check(m.TextureTransforms.Length == 1 && M2MaterialEval.ResolveTransformIndex(m, skin.Batches[0], 0) == 0,
                          v + ": the batch resolves transform 0 through the lookup");
                    Check(M2MaterialEval.ResolveWeightIndex(m, skin.Batches[0]) == -1, v + ": no weight resolves");
                    int zero = 0; var s = new M2MaterialState();
                    // sequence 0
                    bool keysNow = keyed == 0;
                    Check(m.TextureTransforms[0].IsAnimated == keysNow, v + ": sequence 0 has keys == " + keysNow);
                    M2MaterialEval.Evaluate(m, -1, -1, 0, -1, 0f, 0.0, ref s, ref zero);
                    Check(s.Drawn && !s.HasColor, v + ": drawn, no colour");
                    Check(s.Uv0Applied == keysNow && Near(s.S0x, keysNow ? M2Synthetic.KeyedSx : 1f) && Near(s.T0x, keysNow ? M2Synthetic.KeyedTx : 0f),
                          v + ": sequence 0 UV " + (keysNow ? "applied" : "identity"));
                    // the other sequence
                    M2Parser.ReadAnimationInto(file, other, m);
                    bool keysThen = keyed == 1;
                    Check(m.AnimatedSequence == other && m.TextureTransforms[0].IsAnimated == keysThen, v + ": sequence 1 re-read, keys == " + keysThen);
                    M2MaterialEval.Evaluate(m, -1, -1, 0, -1, 0f, 0.0, ref s, ref zero);
                    Check(s.Uv0Applied == keysThen && Near(s.S0y, keysThen ? M2Synthetic.KeyedSy : 1f) && Near(s.T0y, keysThen ? M2Synthetic.KeyedTy : 0f),
                          v + ": sequence 1 UV " + (keysThen ? "applied" : "identity"));
                    // and back
                    M2Parser.ReadAnimationInto(file, 0, m);
                    M2MaterialEval.Evaluate(m, -1, -1, 0, -1, 0f, 0.0, ref s, ref zero);
                    Check(s.Uv0Applied == keysNow, v + ": back to sequence 0, UV " + (keysNow ? "applied" : "identity") + " again");
                }
            }
        }

        static void MaterialTrackTests()
        {
            byte[] file = BuildMaterialM2();
            M2ParsedModel m = M2Parser.Parse(file);
            Check(m.AnimatedSequence == 0, "mat: the idle resolved as the parsed sequence");

            // the lookup, kept as stored: the consumer decides what an out-of-range value means
            Check(m.TextureTransformLookup.Length == 4 && m.TextureTransformLookup[0] == 0 &&
                  m.TextureTransformLookup[1] == 0xFFFF && m.TextureTransformLookup[2] == 1 &&
                  m.TextureTransformLookup[3] == 7,
                  "mat: transform lookup decoded, sentinel and out-of-range values kept as stored");

            // transforms
            Check(m.TextureTransforms.Length == 2, "mat: two texture transforms parsed");
            M2TextureTransform x = m.TextureTransforms[0];
            Check(x.Translation.HasData && x.Translation.Times.Length == 2 && x.Translation.Times[1] == 500 &&
                  x.Translation.Interpolation == M2Interpolation.Linear && !x.Translation.IsGlobal,
                  "mat: translation track decoded (linear, two keys, ordinary sequence)");
            Near(x.Translation.Values[1].X, 0.5f, "mat: translation key value X");
            Near(x.Translation.Values[1].Y, 0.25f, "mat: translation key value Y");
            Check(x.Rotation.HasData && x.Rotation.Values.Length == 1 &&
                  x.Rotation.Interpolation == M2Interpolation.None,
                  "mat: rotation track decoded (one key, no interpolation)");
            Near(x.Rotation.Values[0].Z, 0.7071f, "mat: rotation key Z, read as four floats");
            Near(x.Rotation.Values[0].W, 0.7071f, "mat: rotation key W, read as four floats");
            Check(x.Scale.HasData && x.Scale.IsGlobal && x.Scale.GlobalSequence == 1 &&
                  x.Scale.Times.Length == 2 && x.Scale.Times[1] == 250,
                  "mat: scale track bound to global sequence 1, keys read at entry 0");
            Near(x.Scale.Values[1].X, 2f, "mat: scale key value");
            Check(x.IsAnimated && !m.TextureTransforms[1].IsAnimated,
                  "mat: an entry with no keys anywhere is not animated");
            Check(m.MaterialSurvey.TextureTransforms == 2 && m.MaterialSurvey.TransformsAnimated == 1 &&
                  m.MaterialSurvey.RotationTracksWithData == 1,
                  "mat: survey counts transforms, animated transforms and rotation tracks");

            // colours
            Check(m.Colors.Length == 2, "mat: two colour entries");
            Check(m.Colors[0].Color.HasData && m.Colors[0].Color.Values.Length == 2,
                  "mat: colour RGB track decoded");
            Near(m.Colors[0].Color.Values[0].X, 1f, "mat: colour RGB key 0 is red");
            Near(m.Colors[0].Color.Values[1].Z, 1f, "mat: colour RGB key 1 is blue");
            Check(m.Colors[0].Opacity.HasData && m.Colors[0].Opacity.Times[1] == 500,
                  "mat: colour alpha track decoded");
            Near(m.Colors[0].Opacity.Values[0], 1f, "mat: colour alpha key 0");
            Near(m.Colors[0].Opacity.Values[1], 0f, "mat: colour alpha key 1");
            Check(m.Colors[0].HasColorTrack, "mat: the animation-0 summary flag is still set");
            Near(m.Colors[0].Alpha, 1f, "mat: the animation-0 summary alpha is unchanged");
            Check(!m.Colors[1].Color.HasData, "mat: a colour entry with no RGB keys has no RGB track");
            Check(m.Colors[1].Opacity.HasData && m.Colors[1].Opacity.IsGlobal &&
                  m.Colors[1].Opacity.GlobalSequence == 1,
                  "mat: colour alpha on a global sequence keeps the sequence id");
            Near(m.Colors[1].Opacity.Values[1], 0.75f, "mat: global-sequence alpha key value");
            Check(m.MaterialSurvey.Colors == 2 && m.MaterialSurvey.ColorRgbTracks == 1 &&
                  m.MaterialSurvey.ColorOpacityTracks == 2, "mat: survey counts colour tracks");

            // weights
            Check(m.TextureWeightTracks.Length == 2 && m.TextureWeightTracks[0].Times.Length == 2 &&
                  m.TextureWeightTracks[0].Times[1] == 400, "mat: texture weight track decoded");
            Near(m.TextureWeightTracks[0].Values[1], 1f, "mat: weight key value");
            Near(m.TextureWeights[0], 0f, "mat: the animation-0 first-value summary is unchanged");
            Check(m.MaterialSurvey.WeightsAnimated == 1, "mat: survey counts animated weights");
            Check(m.MaterialSurvey.WeightPerSequenceChecked == 0,
                  "mat: with the idle parsed there is no other sequence to compare weights against");

            // which sequence each track follows, when sequence 1 is parsed
            M2ParsedModel m1 = M2Parser.Parse(file, 1);
            Check(m1.AnimatedSequence == 1, "mat: sequence 1 resolved");
            Check(m1.TextureTransforms[0].Translation.Times[1] == 1000, "mat: translation follows the parsed sequence");
            Near(m1.TextureTransforms[0].Translation.Values[1].X, 1f, "mat: translation keys are sequence 1's");
            Check(m1.Colors[0].Color.Values.Length == 2, "mat: colour RGB stays at animation 0 (legacy index 0)");
            Near(m1.Colors[0].Color.Values[0].X, 1f, "mat: colour RGB keys are still animation 0's");
            Near(m1.Colors[0].Opacity.Values[0], 0.5f, "mat: colour alpha follows the parsed sequence");
            Check(m1.TextureWeightTracks[0].Times[1] == 400, "mat: texture weight stays at animation 0 (legacy index 0)");
            Check(m1.MaterialSurvey.WeightPerSequenceChecked == 2 && m1.MaterialSurvey.WeightPerSequenceDiffers == 1,
                  "mat: survey counts the one weight whose sequence-1 keys differ from animation 0's");

            // a sequence change re-reads the tracks that follow the sequence, in place
            M2Parser.ReadAnimationInto(file, 1, m);
            Check(m.TextureTransforms[0].Translation.Times[1] == 1000, "mat: ReadAnimationInto re-reads the transforms");
            Near(m.Colors[0].Opacity.Values[0], 0.5f, "mat: ReadAnimationInto re-reads colour alpha");
            M2Parser.ReadAnimationInto(file, 0, m);
            Check(m.TextureTransforms[0].Translation.Times[1] == 500, "mat: ...and back again");

            // STATIC is bit 0x10 of the batch flags and nothing else
            var tri = new ushort[] { 0, 1, 2 };
            var s16 = M2SkinParser.Parse(BuildSkin(3, tri, batchFlags: 0x10, batchTransformCombo: 2));
            Check(s16.Batches[0].Static && s16.Batches[0].TextureTransformComboIndex == 2,
                  "mat: STATIC read from flag bit 0x10; transform combo index parsed");
            var s01 = M2SkinParser.Parse(BuildSkin(3, tri, batchFlags: 0x01));
            Check(!s01.Batches[0].Static, "mat: another flag bit is not STATIC");

            // malformed arrays are left empty and counted, never defaulted
            M2ParsedModel badXf = M2Parser.Parse(BuildMaterialM2(badTransformOffset: true));
            Check(badXf.TextureTransforms.Length == 0 && badXf.MaterialSurvey.Rejected == 1 &&
                  badXf.TextureTransformLookup.Length == 4,
                  "mat: a transform array past the payload is rejected; the lookup is still read");
            M2ParsedModel badL = M2Parser.Parse(BuildMaterialM2(badLookupOffset: true));
            Check(badL.TextureTransformLookup.Length == 0 && badL.MaterialSurvey.Rejected == 1 &&
                  badL.TextureTransforms.Length == 2,
                  "mat: a lookup past the payload is rejected; the transforms are still read");

            // a header with none of these arrays parses to empty arrays
            M2ParsedModel plain = M2Parser.Parse(BuildM2Payload(2));
            Check(plain.TextureTransforms.Length == 0 && plain.TextureTransformLookup.Length == 0 &&
                  plain.TextureWeightTracks.Length == 0, "mat: absent arrays parse as empty");
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
