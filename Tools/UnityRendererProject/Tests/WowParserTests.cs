// WowParserTests.cs
//
// Unit tests for the runtime parsing layer (M2, skin, BLP, WMO, coordinate conversion).
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
            log.Add("Emitters");
            EmitterTests();
            log.Add("Skeletons and character lookups");
            SkeletonTests();
            HeaderLookupTests();
            AliasSequenceTests();
            log.Add("WmoParser root");
            WmoRootTests();
            WmoMaterialTests();
            WmoMaterialSemanticsTests();
            WmoPreservedChunkTests();
            WmoMalformedRootTests();
            log.Add("WmoParser group");
            WmoGroupTests();
            WmoStreamTests();
            WmoBatchRuleTests();
            WmoEmptyGroupTests();
            WmoMalformedGroupTests();
            log.Add("WmoParser safety");
            WmoNoUnsafeReadTests();

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
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, -1, 0f, 0.0, ref s, ref zero);
                Check(s.HasColor && Near(s.R, 0.5f), "switch (" + shape + "): colour present in the keyed sequence");
                Check(s.AlphaFromTrack && Near(s.OcolW, 0.5f) && Near(s.EcolW, 0.5f), "switch (" + shape + "): opacity 0.5 from the track at t=0");
                Check(s.Drawn, "switch (" + shape + "): gate open at t=0");
                Check(s.Uv0Applied && Near(s.T0x, 0.25f) && Near(s.T0y, 0.5f) && Near(s.S0x, 2f) && Near(s.S0y, 2f),
                      "switch (" + shape + "): non-identity UV transform in the keyed sequence");
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, -1, 600f, 0.0, ref s, ref zero);
                Check(Near(s.OcolW, 0f) && !s.Drawn, "switch (" + shape + "): gate shut at t=600 (alpha 0)");

                // --- switch to the other sequence: everything falls back to the legacy defaults
                M2Parser.ReadAnimationInto(file, other, m);
                Check(m.AnimatedSequence == other, "switch (" + shape + "): re-read at the other sequence");
                Check(!m.Colors[0].Opacity.HasData && !m.TextureTransforms[0].IsAnimated,
                      "switch (" + shape + "): the other sequence has no keys for alpha or transform");
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, -1, 600f, 0.0, ref s, ref zero);
                Check(s.HasColor && Near(s.R, 0.5f), "switch (" + shape + "): colour still present (index 0)");
                Check(!s.AlphaFromTrack && Near(s.OcolW, 1f) && Near(s.EcolW, 1f),
                      "switch (" + shape + "): opacity back to the default 1 (legacy: ocol.w keeps 1 when the track has no keys)");
                Check(s.Drawn, "switch (" + shape + "): gate open again");
                Check(!s.Uv0Applied && Near(s.T0x, 0f) && Near(s.T0y, 0f) && Near(s.S0x, 1f) && Near(s.S0y, 1f),
                      "switch (" + shape + "): UV transform back to identity (legacy: component not applied without keys)");

                // --- and back: the animated values return
                M2Parser.ReadAnimationInto(file, keyed, m);
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, -1, 0f, 0.0, ref s, ref zero);
                Check(s.AlphaFromTrack && Near(s.OcolW, 0.5f) && s.Uv0Applied && Near(s.T0x, 0.25f) && Near(s.S0x, 2f),
                      "switch (" + shape + "): animated values restored on the way back");
                M2MaterialEval.Evaluate(m, 0, w, x0, -1, -1, 600f, 0.0, ref s, ref zero);
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
                M2MaterialEval.Evaluate(m, 0, 0, 0, -1, -1, 0f, 0.0, ref s, ref zero);
                bool blendIn0 = s.OcolW < 1f;
                M2Parser.ReadAnimationInto(file, 1, m);
                M2MaterialEval.Evaluate(m, 0, 0, 0, -1, -1, 0f, 0.0, ref s, ref zero);
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
                    M2MaterialEval.Evaluate(m, -1, -1, 0, -1, -1, 0f, 0.0, ref s, ref zero);
                    Check(s.Drawn && !s.HasColor, v + ": drawn, no colour");
                    Check(s.Uv0Applied == keysNow && Near(s.S0x, keysNow ? M2Synthetic.KeyedSx : 1f) && Near(s.T0x, keysNow ? M2Synthetic.KeyedTx : 0f),
                          v + ": sequence 0 UV " + (keysNow ? "applied" : "identity"));
                    // the other sequence
                    M2Parser.ReadAnimationInto(file, other, m);
                    bool keysThen = keyed == 1;
                    Check(m.AnimatedSequence == other && m.TextureTransforms[0].IsAnimated == keysThen, v + ": sequence 1 re-read, keys == " + keysThen);
                    M2MaterialEval.Evaluate(m, -1, -1, 0, -1, -1, 0f, 0.0, ref s, ref zero);
                    Check(s.Uv0Applied == keysThen && Near(s.S0y, keysThen ? M2Synthetic.KeyedSy : 1f) && Near(s.T0y, keysThen ? M2Synthetic.KeyedTy : 0f),
                          v + ": sequence 1 UV " + (keysThen ? "applied" : "identity"));
                    // and back
                    M2Parser.ReadAnimationInto(file, 0, m);
                    M2MaterialEval.Evaluate(m, -1, -1, 0, -1, -1, 0f, 0.0, ref s, ref zero);
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

        // ---------------------------------------------------------------- emitters

        const int ParticleStride = 492;
        const int RibbonStride = 176;

        /// <summary>
        /// An M2 payload with a FULL 0x138 header plus one particle emitter and one ribbon
        /// emitter, both with real nested tracks and real flat ramps.
        ///
        /// The header has to be the full length: the emitter arrays live at 0x120 and 0x128,
        /// past where the other fixtures stop, and a parser that read those offsets out of the
        /// vertex data would look like it worked.
        /// </summary>
        static byte[] BuildEmitterPayload(int particleFlags = 0, int rampStops = 3,
                                          bool ribbonTexturesAsUint16 = true)
        {
            const int headerSize = 0x138;
            const int boneCount = 2;
            const int texCount = 4;
            const int matCount = 2;

            int boneOffset = headerSize;
            int texOffset = boneOffset + boneCount * BoneStride;
            int matOffset = texOffset + texCount * 16;
            int seqOffset = matOffset + matCount * 4;
            // The nested track arrays: one per animation sequence (one sequence here).
            int nestedOffset = seqOffset + 64;
            int keyTimesOffset = nestedOffset + 8 * 8;      // eight {count,offset} pairs
            int keyValuesOffset = keyTimesOffset + 4 * 8;   // eight uint32 timestamps
            int rampTimesOffset = keyValuesOffset + 4 * 8;  // eight floats of key value
            int rampColorOffset = rampTimesOffset + 16 * 2; // uint16 normalised times
            int rampAlphaOffset = rampColorOffset + 16 * 12;
            int rampSizeOffset = rampAlphaOffset + 16 * 2;
            int ribbonTexOffset = rampSizeOffset + 16 * 8;
            int ribbonMatOffset = ribbonTexOffset + 8;
            int particleOffset = ribbonMatOffset + 8;
            int ribbonOffset = particleOffset + ParticleStride;
            int total = ribbonOffset + RibbonStride;

            var b = new byte[total];
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x1C, 1); PutU32(b, 0x20, (uint)seqOffset);          // one sequence
            PutU32(b, 0x2C, boneCount); PutU32(b, 0x30, (uint)boneOffset);
            PutU32(b, 0x3C, 0); PutU32(b, 0x40, 0);                        // no vertices
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, texCount); PutU32(b, 0x54, (uint)texOffset);
            PutU32(b, 0x70, matCount); PutU32(b, 0x74, (uint)matOffset);
            PutU32(b, 0x120, 1); PutU32(b, 0x124, (uint)ribbonOffset);     // ribbon emitters
            PutU32(b, 0x128, 1); PutU32(b, 0x12C, (uint)particleOffset);   // particle emitters

            for (int i = 0; i < boneCount; i++)
                PutBone(b, boneOffset + i * BoneStride, (short)(i - 1), i, 0f, 0f);
            for (int i = 0; i < texCount; i++)
            {
                PutU32(b, texOffset + i * 16 + 0, 0);   // type 0: a named texture
                PutU32(b, texOffset + i * 16 + 4, 0);
            }
            PutU16(b, matOffset, 0); PutU16(b, matOffset + 2, 2);          // material 0: blend 2
            PutU16(b, matOffset + 4, 0); PutU16(b, matOffset + 6, 7);      // material 1: blend 7
            PutU32(b, seqOffset + 4, 1000);                                 // sequence length
            PutU32(b, seqOffset + 0x10, 0x20);                              // primary sequence

            // One nested entry per track: {count, offset} into the times and values arrays.
            for (int i = 0; i < 8; i++)
            {
                PutU32(b, nestedOffset + i * 8 + 0, 1);
                PutU32(b, nestedOffset + i * 8 + 4, (uint)(keyTimesOffset + i * 4));
            }
            for (int i = 0; i < 8; i++)
                PutU32(b, keyTimesOffset + i * 4, 0);

            // The ramps. Times are uint16 over 32767; a deliberately UNEVEN middle stop, which is
            // the case the legacy's hardcoded 0.5 gets wrong.
            var stopTimes = new ushort[] { 0, 8192, 32767 };   // 0.0, 0.25, 1.0
            for (int i = 0; i < rampStops; i++)
                PutU16(b, rampTimesOffset + i * 2,
                       i < stopTimes.Length ? stopTimes[i] : (ushort)32767);
            for (int i = 0; i < rampStops; i++)
            {
                PutF32(b, rampColorOffset + i * 12 + 0, 255f);       // r -> 1.0
                PutF32(b, rampColorOffset + i * 12 + 4, i * 100f);
                PutF32(b, rampColorOffset + i * 12 + 8, 0f);
                PutU16(b, rampAlphaOffset + i * 2, (ushort)(i == 1 ? 32767 : 0));
                PutF32(b, rampSizeOffset + i * 8 + 0, 0.5f);         // x
                PutF32(b, rampSizeOffset + i * 8 + 4, 0.25f);        // y, deliberately different
            }

            // Ribbon texture and material index arrays -- uint16 each.
            PutU16(b, ribbonTexOffset + 0, 2);
            PutU16(b, ribbonTexOffset + 2, 3);
            PutU16(b, ribbonMatOffset + 0, 1);        // -> material 1, blend 7

            // ---- the particle emitter ----
            int p = particleOffset;
            PutU32(b, p + 0, unchecked((uint)-1));               // id
            PutU32(b, p + 4, (uint)particleFlags);
            PutF32(b, p + 8, 1f); PutF32(b, p + 12, 2f); PutF32(b, p + 16, 3f);   // pos
            PutU16(b, p + 20, 1);                                // bone 1
            PutU16(b, p + 22, 2);                                // texture slot 2
            b[p + 40] = 4;                                       // blend: additive on alpha
            b[p + 41] = 2;                                       // emitter type: sphere
            PutU16(b, p + 42, 12);                               // ParticleColorIndex
            PutU16(b, p + 46, 0);                                // tile rotation
            PutU16(b, p + 48, 2); PutU16(b, p + 50, 4);          // rows, cols
            // The ten float tracks. Only EmissionRate (index 6, at +176) is given keys.
            PutU16(b, p + 176, 1);                               // interpolation: linear
            PutU16(b, p + 178, unchecked((ushort)-1));           // no global sequence
            PutU32(b, p + 180, 1); PutU32(b, p + 184, (uint)nestedOffset);
            PutU32(b, p + 188, 1); PutU32(b, p + 192, (uint)(nestedOffset + 8));
            PutF32(b, keyValuesOffset + 4, 40f);                 // rate = 40
            for (int i = 0; i < 8; i++)
                PutU32(b, nestedOffset + i * 8 + 4,
                       (uint)(i == 1 ? keyValuesOffset + 4 : keyTimesOffset + i * 4));
            PutU32(b, nestedOffset + 0 * 8 + 4, (uint)keyTimesOffset);

            // EnabledIn, at +456: ONE byte per key.
            PutU16(b, p + 456, 0);
            PutU16(b, p + 458, unchecked((ushort)-1));
            PutU32(b, p + 460, 1); PutU32(b, p + 464, (uint)(nestedOffset + 16));
            PutU32(b, p + 468, 1); PutU32(b, p + 472, (uint)(nestedOffset + 24));

            // ModelParticleParams at +260: three FakeAnimationBlocks {nTimes,ofsTimes,nKeys,ofsKeys}
            int pp = p + 260;
            PutU32(b, pp + 0, (uint)rampStops); PutU32(b, pp + 4, (uint)rampTimesOffset);
            PutU32(b, pp + 8, (uint)rampStops); PutU32(b, pp + 12, (uint)rampColorOffset);
            PutU32(b, pp + 16, (uint)rampStops); PutU32(b, pp + 20, (uint)rampTimesOffset);
            PutU32(b, pp + 24, (uint)rampStops); PutU32(b, pp + 28, (uint)rampAlphaOffset);
            PutU32(b, pp + 32, (uint)rampStops); PutU32(b, pp + 36, (uint)rampTimesOffset);
            PutU32(b, pp + 40, (uint)rampStops); PutU32(b, pp + 44, (uint)rampSizeOffset);
            PutF32(b, pp + 100, 2f);      // scales.x
            PutF32(b, pp + 104, 2f);      // scales.y
            PutF32(b, pp + 108, 519f);    // the third float: NOT a scale
            PutF32(b, pp + 112, 0.5f);    // slowdown
            PutF32(b, pp + 124, 0.25f);   // sprite rotation

            // ---- the ribbon emitter ----
            int r = ribbonOffset;
            PutU32(b, r + 0, unchecked((uint)-1));
            PutU32(b, r + 4, 1);                                  // bone 1 (int32 here)
            PutF32(b, r + 8, 4f); PutF32(b, r + 12, 5f); PutF32(b, r + 16, 6f);
            PutU32(b, r + 20, 2); PutU32(b, r + 24, (uint)ribbonTexOffset);
            PutU32(b, r + 28, 1); PutU32(b, r + 32, (uint)ribbonMatOffset);
            PutF32(b, r + 116, 50f);       // edges per second
            PutF32(b, r + 120, 0.2f);      // edge lifetime
            PutF32(b, r + 124, 0f);        // emission angle
            return b;
        }

        static void EmitterTests()
        {
            byte[] file = WrapChunked(BuildEmitterPayload(), new[] { 1 }, new[] { 10, 11, 12, 13 });
            M2ParsedModel m = M2Parser.Parse(file);

            Check(m.ParticleEmitterCount == 1 && m.ParticleEmitters.Length == 1,
                  "emitters: particle array read at 0x128");
            Check(m.RibbonEmitterCount == 1 && m.RibbonEmitters.Length == 1,
                  "emitters: ribbon array read at 0x120");

            M2ParticleEmitterDef p = m.ParticleEmitters[0];
            Check(p.Bone == 1, "particle: bone index");
            Check(p.TextureId == 2, "particle: texture is a DIRECT slot index");
            Check(p.BlendMode == 4, "particle: blend mode");
            Check(p.EmitterType == 2 && p.Supported, "particle: sphere emitter is supported");
            Check(p.ParticleColorIndex == 12, "particle: ParticleColorIndex kept");
            Check(p.Rows == 2 && p.Cols == 4, "particle: flipbook rows/cols");
            Near(p.Position.X, 1f, "particle: position X");
            Near(p.Position.Z, 3f, "particle: position Z");
            Check(p.EmissionRate.HasData && p.EmissionRate.Values.Length == 1,
                  "particle: EmissionRate track read through the nested arrays");
            Near(p.EmissionRate.Values[0], 40f, "particle: EmissionRate value");

            // The ramp: variable length, with the authored times, NOT three stops at 0/0.5/1.
            Check(p.ColorKeys.Length == 3 && p.ColorTimes.Length == 3, "particle: colour ramp stops");
            Near(p.ColorTimes[1], 8192f / 32767f, "particle: ramp times are uint16 over 32767");
            Check(p.ColorTimes[1] < 0.3f, "particle: an uneven middle stop is NOT snapped to 0.5");
            Near(p.ColorKeys[0].X, 1f, "particle: colour stored 0..255 becomes 0..1");
            Near(p.AlphaKeys[1], 1f, "particle: alpha stored fixed16");

            // scales is per-AXIS, applied to every stop -- and its third float is not a scale.
            Near(p.ParticleScale.X, 2f, "particle: scales.x read");
            Near(p.ParticleScale.Y, 2f, "particle: scales.y read");
            Near(p.SizeKeys[0].X, 1f, "particle: size x scaled by scales.x (0.5 * 2)");
            Near(p.SizeKeys[0].Y, 0.5f, "particle: size y scaled by scales.y (0.25 * 2)");
            Check(Math.Abs(p.SizeKeys[2].X - 0.5f * 519f) > 1f,
                  "particle: the third scales float is NOT applied to the last stop");
            Near(p.Slowdown, 0.5f, "particle: slowdown");
            Near(p.SpriteRotation, 0.25f, "particle: sprite rotation");

            M2RibbonEmitterDef rb = m.RibbonEmitters[0];
            Check(rb.Bone == 1, "ribbon: bone index (int32)");
            Near(rb.Position.Y, 5f, "ribbon: position Y");
            Check(rb.TextureIds.Length == 2 && rb.TextureIds[0] == 2 && rb.TextureIds[1] == 3,
                  "ribbon: texture indices are UINT16, not int32");
            Check(rb.MaterialIndex == 1, "ribbon: material index read from its own array");
            Near(rb.EdgesPerSecond, 50f, "ribbon: res is edges per second");
            Near(rb.EdgeLifetimeSeconds, 0.2f, "ribbon: length is an edge lifetime in seconds");
            Check(rb.EmissionAngle == 0f,
                  "ribbon: the three floats are read separately (a struct cursor is by value)");

            // A two-stop ramp must stay two stops -- the legacy copies three regardless.
            M2ParsedModel two = M2Parser.Parse(
                WrapChunked(BuildEmitterPayload(rampStops: 2), new[] { 1 }, new[] { 10, 11, 12, 13 }));
            Check(two.ParticleEmitters[0].ColorKeys.Length == 2,
                  "particle: a two-stop ramp is not padded to three");

            // A spline emitter is PARSED -- the parser reports what the file holds -- but marks
            // itself unsupported, and the runtime is what declines to draw it (as the legacy
            // builds no emitter object for one either, particle.cpp:117-120).
            byte[] spline = BuildEmitterPayload();
            spline[FindParticleOffset(spline) + 41] = 3;
            M2ParsedModel sp = M2Parser.Parse(WrapChunked(spline, new[] { 1 }, new[] { 10, 11, 12, 13 }));
            Check(sp.ParticleEmitters.Length == 1, "particle: a spline emitter still parses");
            Check(!sp.ParticleEmitters[0].Supported,
                  "particle: a spline emitter reports itself unsupported");

            // An out-of-range bone is dropped rather than left to index off the end.
            byte[] badBone = BuildEmitterPayload();
            PutU16(badBone, FindParticleOffset(badBone) + 20, 900);
            M2ParsedModel bb = M2Parser.Parse(WrapChunked(badBone, new[] { 1 }, new[] { 10, 11, 12, 13 }));
            Check(bb.ParticleEmitters.Length == 0, "particle: an out-of-range bone is dropped");

            // Flags.
            M2ParticleEmitterDef f = M2Parser.Parse(
                WrapChunked(BuildEmitterPayload(0x10 | 0x1000 | 0x800000 | 0x20000),
                            new[] { 1 }, new[] { 10, 11, 12, 13 })).ParticleEmitters[0];
            Check(f.DoNotTrail, "particle: DONOTTRAIL flag");
            Check(f.DoNotBillboard, "particle: DONOTBILLBOARD flag");
            Check(f.Outward, "particle: OUTWARD flag");
            Check(f.CompressedGravity, "particle: compressed-gravity flag");

            // The synthetic fixture the runtime lifecycle test drives, checked here so a failure
            // in it is attributed to the fixture rather than to the runtime.
            for (int keyed = 0; keyed < 2; keyed++)
            {
                byte[] synth = M2Synthetic.EmitterModel(keyed);
                M2ParsedModel s0 = M2Parser.Parse(synth, 0);
                M2ParsedModel s1 = M2Parser.Parse(synth, 1);
                Check(s0.ParticleEmitters.Length == 1 && s0.RibbonEmitters.Length == 1,
                      "synthetic[" + keyed + "]: one particle and one ribbon emitter in sequence 0");
                Check(s1.ParticleEmitters.Length == 1 && s1.RibbonEmitters.Length == 1,
                      "synthetic[" + keyed + "]: same in sequence 1");
                // The float tracks are read at ANIMATION 0 whichever sequence is playing --
                // WMV's bZeroParticle default, see M2Parser. So the rate is the same in both,
                // and it is present exactly when animation 0 is the keyed one.
                Check(s0.ParticleEmitters[0].EmissionRate.HasData == (keyed == 0),
                      "synthetic[" + keyed + "]: rate read at animation 0, present == " + (keyed == 0));
                Check(s1.ParticleEmitters[0].EmissionRate.HasData ==
                      s0.ParticleEmitters[0].EmissionRate.HasData,
                      "synthetic[" + keyed + "]: the played sequence does not change it");
                Check(s0.ParticleEmitters[0].Lifespan.HasData && s1.ParticleEmitters[0].Lifespan.HasData,
                      "synthetic[" + keyed + "]: lifespan keys in BOTH sequences");
                Check(s0.RibbonEmitters[0].MaterialIndex == 1,
                      "synthetic[" + keyed + "]: ribbon material index");
            }

            // A header too short to carry the arrays yields no emitters and no exception.
            M2ParsedModel shortHeader = M2Parser.Parse(
                WrapChunked(BuildM2Payload(3), new[] { 473370 }, new[] { 0 }));
            Check(shortHeader.ParticleEmitters.Length == 0 && shortHeader.RibbonEmitters.Length == 0,
                  "emitters: a model without emitter arrays parses to none");
        }

        /// <summary>Where BuildEmitterPayload put the particle emitter, read back out of the
        /// header so the test does not restate the fixture's arithmetic.</summary>
        static int FindParticleOffset(byte[] payload)
        {
            return (int)(payload[0x12C] | (payload[0x12D] << 8) |
                         (payload[0x12E] << 16) | (payload[0x12F] << 24));
        }

        static M2BoneDef Bone(M2BoneDef[] bones, int i)
        {
            if (bones != null && i >= 0 && i < bones.Length)
                return bones[i];
            var none = new M2BoneDef();                  // absent: empty tracks, not null ones
            none.Translation.Times = new uint[0]; none.Translation.Values = new WowVec3[0];
            none.Rotation.Times = new uint[0]; none.Rotation.Values = new WowQuat[0];
            none.Scale.Times = new uint[0]; none.Scale.Values = new WowVec3[0];
            return none;
        }

        static M2AttachmentDef Att(M2ParsedModel model, int i)
        {
            return i >= 0 && i < model.Attachments.Length ? model.Attachments[i] : new M2AttachmentDef();
        }

        /// <summary>Key k of a track, or NaN (which fails every comparison) when it is absent, so a
        /// missing key is a FAIL line rather than an exception that stops the run.</summary>
        static WowVec3 TKey(M2Track<WowVec3> t, int k)
        {
            return t.Values != null && k < t.Values.Length ? t.Values[k] : new WowVec3(float.NaN, float.NaN, float.NaN);
        }

        static WowQuat RKey(M2Track<WowQuat> t, int k)
        {
            return t.Values != null && k < t.Values.Length ? t.Values[k] : new WowQuat(float.NaN, float.NaN, float.NaN, float.NaN);
        }

        static bool NoTracks(M2BoneDef[] bones)
        {
            foreach (M2BoneDef b in bones)
                if (b.IsAnimated) return false;
            return true;
        }

        /// <summary>
        /// Skeleton files, the way every retail playable race ships: the .m2 names a .skel with
        /// SKID, and the bones, sequences, lookups and attachments all come from there (or, with
        /// an SKPD, partly from a parent skeleton). See M2Synthetic's skeleton section for the
        /// fixture's values; tag 0 is a skeleton, tag 1 its parent.
        /// </summary>
        static void SkeletonTests()
        {
            const int skelId = 777001, parentId = 777002, skinId = 777003;
            byte[] m2 = M2Synthetic.SkeletonModel(skelId, skinId);
            byte[] skel = M2Synthetic.Skeleton(0);
            byte[] parentSkel = M2Synthetic.Skeleton(1);
            byte[] childSkel = M2Synthetic.Skeleton(0, parentId);
            byte[] anim = M2Synthetic.SkeletonAnimFile(0, true);
            float z2 = M2Synthetic.ExternalZ(2, 0);

            M2ParsedModel m = M2Parser.Parse(m2);
            Check(m.SkeletonFileDataID == skelId && m.SkinFileDataIDs.Length == 1 && m.SkinFileDataIDs[0] == skinId,
                  "skel: the synthetic model carries SKID and SFID");
            Check(m.Bones.Length == 0 && !m.SkeletonApplied && m.AnimationSkipReason != null,
                  "skel: Parse leaves an SKID model without bones until its skeleton is applied");
            Check(M2Parser.ReadSkeletonParentId(childSkel) == parentId && M2Parser.ReadSkeletonParentId(skel) == 0 &&
                  M2Parser.ReadSkeletonParentId(m2) == 0 && M2Parser.ReadSkeletonParentId(null) == 0,
                  "skel: SKPD names the parent, and anything without one reads 0");

            // ---- a skeleton with no parent ----
            M2Parser.ApplySkeleton(m, skel, null, -1);
            Check(m.SkeletonApplied && m.ParentSkeletonFileDataID == 0, "skel: applied, no parent");
            Check(m.Bones.Length == 3 && m.BoneCount == 3, "skel: bones come from SKB1");
            Check(Bone(m.Bones, 0).Parent == -1 && Bone(m.Bones, 1).Parent == 0 && Bone(m.Bones, 2).Parent == 1,
                  "skel: parent chain read");
            Near(Bone(m.Bones, 2).Pivot.X, 2f, "skel: pivot read");
            Check(Bone(m.Bones, 2).NameCrc == M2Synthetic.SkelBoneCrc(2, 0) && Bone(m.Bones, 0).NameCrc == M2Synthetic.SkelBoneCrc(0, 0),
                  "skel: bone name hash read from bone+12");
            Check(Bone(m.Bones, 2).KeyBoneId == 8 && Bone(m.Bones, 1).KeyBoneId == -1, "skel: keyBoneId read");
            Check(m.Sequences.Length == 2 && m.Sequences[1].AnimId == M2Synthetic.SkelExternalAnimId &&
                  m.Sequences[1].Length == 800 && !m.Sequences[1].PrimarySequence && m.Sequences[0].PrimarySequence,
                  "skel: sequences come from SKS1");
            Check(m.GlobalSequences.Length == 2 && m.GlobalSequences[0] == 1000 && m.GlobalSequences[1] == 2000,
                  "skel: global sequences come from SKS1");
            Check(m.AnimationLookup.Length == 6 && m.AnimationLookup[5] == 1 && m.AnimationLookup[0] == 0,
                  "skel: animation lookup read");
            Check(M2Parser.ExternalAnimFileId(m, 1) == M2Synthetic.SkelAnimFileIdBase,
                  "skel: AFID comes from the skeleton");
            Check(m.KeyBoneLookup.Length == 10 && m.KeyBoneLookup[8] == 2 && m.KeyBoneLookup[0] == 0,
                  "skel: key-bone lookup read");
            Check(m.KeyBoneLookup[9] == -1, "skel: a key-bone entry past the bones is sanitised to -1");
            Check(m.Attachments.Length == 2 && m.AttachmentLookup.Length == 12, "skel: attachments come from SKA1");
            Check(Att(m, 0).Id == 11 && Att(m, 0).Bone == 2,
                  "skel: attachment bone is the low 16 bits of the field");
            Check(m.AnimatedSequence == 0 && m.AnimationSkipReason == null && m.RequiredAnimFileId == 0,
                  "skel: the idle resolves and the skip reason is cleared");
            Check(Bone(m.Bones, 2).Translation.Times.Length == 2 && Bone(m.Bones, 2).Translation.Times[1] == 500,
                  "skel: idle translation keys read from SKB1");
            Near(TKey(Bone(m.Bones, 2).Translation, 1).X, 3f, "skel: idle translation value");
            Check(Bone(m.Bones, 1).Rotation.IsGlobal && Bone(m.Bones, 1).Rotation.GlobalSequence == 1 &&
                  Bone(m.Bones, 1).Rotation.Values.Length == 2, "skel: global-sequence rotation read");
            Near(RKey(Bone(m.Bones, 1).Rotation, 1).W, 0.7071f, "skel: packed quaternion from SKB1", 1e-3f);
            Check(m.TextureTransforms.Length == 1 && m.TextureTransforms[0].Translation.HasData,
                  "skel: the .m2's material tracks are read for the skeleton's sequence");

            // ---- the helpers ----
            Check(M2Parser.SequenceForAnimId(m, 5) == 1 && M2Parser.SequenceForAnimId(m, 0) == 0,
                  "helpers: SequenceForAnimId goes through the animation lookup");
            Check(M2Parser.SequenceForAnimId(m, 3) == -1 && M2Parser.SequenceForAnimId(m, 500) == -1 &&
                  M2Parser.SequenceForAnimId(m, -1) == -1, "helpers: an animation the model lacks is -1");
            short[] savedLookup = m.AnimationLookup;
            m.AnimationLookup = new short[0];
            Check(M2Parser.SequenceForAnimId(m, 5) == 1, "helpers: without a lookup the first matching AnimId answers");
            m.AnimationLookup = savedLookup;
            Check(M2Parser.BoneForKeyBone(m, 8) == 2 && M2Parser.BoneForKeyBone(m, 0) == 0,
                  "helpers: BoneForKeyBone maps a key bone");
            Check(M2Parser.BoneForKeyBone(m, 9) == -1 && M2Parser.BoneForKeyBone(m, 3) == -1 &&
                  M2Parser.BoneForKeyBone(m, 99) == -1 && M2Parser.BoneForKeyBone(m, -1) == -1 &&
                  M2Parser.BoneForKeyBone(null, 8) == -1, "helpers: BoneForKeyBone refuses anything not a real bone");
            M2AttachmentDef att;
            Check(M2Parser.AttachmentFor(m, 11, out att) && att.Bone == 2, "helpers: AttachmentFor resolves id 11");
            Near(att.Position.Z, 1.5f, "helpers: attachment position");
            Check(M2Parser.AttachmentFor(m, 5, out att) && att.Bone == 1 && Math.Abs(att.Position.Y + 0.25f) < 1e-4f,
                  "helpers: AttachmentFor resolves id 5");
            Check(!M2Parser.AttachmentFor(m, 1, out att) && !M2Parser.AttachmentFor(m, 400, out att) &&
                  !M2Parser.AttachmentFor(m, -3, out att), "helpers: AttachmentFor is false for an absent id");

            // ---- an external sequence ----
            M2Parser.ReadAnimationInto(m2, 1, m);
            Check(m.AnimatedSequence == 0 && m.AnimationSkipReason != null && m.AnimationSkipReason.Contains(".anim"),
                  "skel anim: without the .anim the external sequence falls back to the idle");
            M2Parser.ReadAnimationInto(m2, 1, m, anim);
            Check(m.AnimatedSequence == 1 && m.AnimationSkipReason == null &&
                  m.RequiredAnimFileId == M2Synthetic.SkelAnimFileIdBase,
                  "skel anim: ReadAnimationInto switches a skeleton model to its external sequence");
            Check(Bone(m.Bones, 2).Translation.Times.Length == 2 && Bone(m.Bones, 2).Translation.Times[1] == 400,
                  "skel anim: external key times read");
            Near(TKey(Bone(m.Bones, 2).Translation, 1).Z, z2, "skel anim: keys come from AFSB, not the AFM2 decoy");
            Check(Bone(m.Bones, 1).Rotation.Values.Length == 2,
                  "skel anim: a global track still reads entry 0 from the skeleton while an external sequence plays");
            Check(!m.TextureTransforms[0].Translation.HasData,
                  "skel anim: the .m2's material tracks follow the sequence change");
            M2Parser.ReadAnimationInto(m2, 0, m);
            Check(m.AnimatedSequence == 0 && m.TextureTransforms[0].Translation.HasData, "skel anim: and back to the idle");
            Near(TKey(Bone(m.Bones, 2).Translation, 1).X, 3f, "skel anim: idle keys again");

            // The .anim rule: AFSB when present, the sole chunk when there is one, else the file.
            byte[] raw = M2Synthetic.SkeletonAnimFile(0, false);
            M2ParsedModel u = M2Parser.Parse(m2);
            M2Parser.ApplySkeleton(u, skel, null, 1, raw);
            Check(u.AnimatedSequence == 1 && Bone(u.Bones, 2).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(u.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "anim file: an unchunked .anim is itself the keyframe buffer");
            M2ParsedModel single = M2Parser.Parse(m2);
            M2Parser.ApplySkeleton(single, skel, null, 1, M2Synthetic.SingleChunkAnimFile(0, "AFM2"));
            Check(single.AnimatedSequence == 1 && Bone(single.Bones, 2).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(single.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "anim file: a chunked .anim with one chunk reads that chunk");
            Check(ReferenceEquals(M2Parser.AnimKeyframeBuffer(raw), raw) && M2Parser.AnimKeyframeBuffer(null) == null,
                  "anim file: nothing to narrow returns the same bytes");
            byte[] narrowed = M2Parser.AnimKeyframeBuffer(anim);
            bool same = narrowed.Length == raw.Length;
            for (int i = 0; same && i < raw.Length; i++) same = narrowed[i] == raw[i];
            Check(same, "anim file: AFM2 + AFSB narrows to the AFSB payload");
            byte[] noAfsb = (byte[])anim.Clone();
            int second = 8 + (int)BitConverter.ToUInt32(anim, 4);
            PutMagic(noAfsb, second, "AFSA");
            Check(ReferenceEquals(M2Parser.AnimKeyframeBuffer(noAfsb), noAfsb),
                  "anim file: two chunks and no AFSB reads the whole file");

            // ---- override tracks ----
            M2BoneDef[] before = m.Bones;
            M2BoneDef[] other = M2Parser.ReadBoneTracksForSequence(m, 1, anim);
            Check(!ReferenceEquals(other, m.Bones) && ReferenceEquals(before, m.Bones) && m.AnimatedSequence == 0,
                  "override: ReadBoneTracksForSequence leaves the model alone");
            Near(TKey(Bone(m.Bones, 2).Translation, 1).X, 3f, "override: the model keeps its own keys");
            Check(other.Length == 3 && Bone(other, 2).Parent == 1 && Bone(other, 2).NameCrc == M2Synthetic.SkelBoneCrc(2, 0) &&
                  Bone(other, 2).KeyBoneId == 8 && Math.Abs(Bone(other, 2).Pivot.X - 2f) < 1e-4f,
                  "override: same hierarchy, pivots and name hashes");
            Check(Bone(other, 2).Translation.Times.Length == 2 && Bone(other, 2).Translation.Times[1] == 400 &&
                  Math.Abs(TKey(Bone(other, 2).Translation, 1).Z - z2) < 1e-4f,
                  "override: the other sequence's keys");
            Check(NoTracks(M2Parser.ReadBoneTracksForSequence(m, 1, null)),
                  "override: an external sequence without its bytes gives empty tracks");
            M2BoneDef[] bad = M2Parser.ReadBoneTracksForSequence(m, 99, null);
            Check(bad.Length == 3 && NoTracks(bad) && NoTracks(M2Parser.ReadBoneTracksForSequence(m, -1, null)),
                  "override: a bad sequence index gives empty tracks, not an exception");
            Check(M2Parser.ReadBoneTracksForSequence(null, 0, null).Length == 0, "override: no model, no bones");

            // ---- a skeleton with a parent ----
            M2ParsedModel p = M2Parser.Parse(m2);
            M2Parser.ApplySkeleton(p, childSkel, parentSkel, -1);
            Check(p.SkeletonApplied && p.ParentSkeletonFileDataID == parentId, "parent: the SKPD parent is recorded");
            Check(p.Bones.Length == 4 && Bone(p.Bones, 3).NameCrc == M2Synthetic.SkelBoneCrc(3, 1) &&
                  Math.Abs(Bone(p.Bones, 0).Pivot.Z - 1f) < 1e-4f, "parent: bones come from the parent's SKB1");
            Check(p.KeyBoneLookup.Length == 10 && p.KeyBoneLookup[8] == 3, "parent: key-bone lookup from the parent");
            Check(p.Sequences.Length == 2 && p.Sequences[1].Length == 801, "parent: sequences come from the parent");
            Check(p.AnimationLookup.Length == 7, "parent: animation lookup comes from the parent");
            Check(M2Parser.ExternalAnimFileId(p, 1) == M2Synthetic.SkelAnimFileIdBase + 1, "parent: AFID comes from the parent");
            Check(p.GlobalSequences.Length == 4 && p.GlobalSequences[0] == 1000 && p.GlobalSequences[1] == 2000 &&
                  p.GlobalSequences[2] == 1100 && p.GlobalSequences[3] == 2100,
                  "parent: global sequences are the child's followed by the parent's");
            Check(p.Attachments.Length == 2 && p.AttachmentLookup.Length == 12 &&
                  Math.Abs(Att(p, 0).Position.Z - 1.5f) < 1e-4f,
                  "parent: attachments come from the child only");
            Check(p.AnimatedSequence == 0 && Bone(p.Bones, 3).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(p.Bones, 3).Translation, 1).X - 5f) < 1e-4f,
                  "parent: idle keys read from the parent's SKB1");
            M2Parser.ReadAnimationInto(m2, 1, p, M2Synthetic.SkeletonAnimFile(1, true));
            Check(p.AnimatedSequence == 1 && Bone(p.Bones, 3).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(p.Bones, 3).Translation, 1).Z - M2Synthetic.ExternalZ(3, 1)) < 1e-4f,
                  "parent: an external sequence reads its keys against the parent's bone headers");

            // ---- optional and malformed chunks ----
            M2ParsedModel q = M2Parser.Parse(m2);
            M2Parser.ApplySkeleton(q, M2Synthetic.Skeleton(0, 0, false, true, false), null, -1);
            Check(q.SkeletonApplied && q.Attachments.Length == 0 && q.AttachmentLookup.Length == 0 && q.Bones.Length == 3,
                  "skel: no SKA1 (and no SKL1) means no attachments, not an error");
            M2ParsedModel mal = M2Parser.Parse(m2);
            Throws<WowParseException>(() => M2Parser.ApplySkeleton(mal, M2Synthetic.Skeleton(0, 0, true, false), null, -1),
                                      "skel: a skeleton without SKB1 is rejected");
            Check(!mal.SkeletonApplied && mal.Bones.Length == 0 && mal.Sequences.Length == 2 && mal.AnimationLookup.Length == 0,
                  "skel: a rejected skeleton leaves the model untouched");
            Throws<WowParseException>(() => M2Parser.ApplySkeleton(mal, m2, null, -1),
                                      "skel: bytes that are not a chunked skeleton are rejected");
            Throws<WowParseException>(() => M2Parser.ApplySkeleton(mal, null, null, -1),
                                      "skel: no skeleton bytes are rejected");
            Throws<WowParseException>(() => M2Parser.ApplySkeleton(mal, childSkel, M2Synthetic.Skeleton(1, 0, true, false), -1),
                                      "skel: a parent without SKB1 is rejected");

            // An SKID model whose skeleton was never applied keeps today's refusal.
            M2ParsedModel unapplied = M2Parser.Parse(m2);
            M2Parser.ReadAnimationInto(m2, 0, unapplied);
            Check(unapplied.AnimatedSequence == -1 && unapplied.Bones.Length == 0 &&
                  unapplied.AnimationSkipReason != null && unapplied.AnimationSkipReason.Contains("skeleton file"),
                  "skel: ReadAnimationInto still refuses an SKID model without its skeleton");
            Check(NoTracks(M2Parser.ReadBoneTracksForSequence(unapplied, 0, null)),
                  "override: an unapplied SKID model gives no tracks");
        }

        /// <summary>
        /// The same lookups on a model that keeps its skeleton in its own header, and the bone
        /// keyframe regression: bone tracks must take their keys from the .anim for a sequence
        /// stored there, as the legacy viewport does.
        /// </summary>
        static void HeaderLookupTests()
        {
            byte[] inFile = M2Synthetic.InFileSkeletonModel(473370);
            byte[] anim = M2Synthetic.SkeletonAnimFile(0, true);
            float z2 = M2Synthetic.ExternalZ(2, 0);

            M2ParsedModel h = M2Parser.Parse(inFile);
            Check(h.AnimationLookup.Length == 6 && h.AnimationLookup[5] == 1, "header: animation lookup read (0x24)");
            Check(h.KeyBoneLookup.Length == 10 && h.KeyBoneLookup[8] == 2 && h.KeyBoneLookup[9] == -1,
                  "header: key-bone lookup read (0x34) and sanitised");
            Check(h.Attachments.Length == 2 && h.AttachmentLookup.Length == 12,
                  "header: attachments (0xF0) and their lookup (0xF8) read");
            M2AttachmentDef att;
            Check(M2Parser.AttachmentFor(h, 11, out att) && att.Bone == 2 && Math.Abs(att.Position.Z - 1.5f) < 1e-4f,
                  "header: AttachmentFor on an in-file model");
            Check(M2Parser.SequenceForAnimId(h, 5) == 1 && M2Parser.BoneForKeyBone(h, 8) == 2,
                  "header: SequenceForAnimId and BoneForKeyBone on an in-file model");
            Check(Bone(h.Bones, 2).NameCrc == M2Synthetic.SkelBoneCrc(2, 0), "header: bone name hash read");
            Check(!h.SkeletonApplied, "header: an in-file skeleton is not an applied one");

            // THE REGRESSION. Sequence 1's bone keys exist only in the .anim.
            M2ParsedModel r = M2Parser.Parse(inFile, 1, anim);
            Check(r.AnimatedSequence == 1 && Bone(r.Bones, 2).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(r.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "regression: Parse reads bone keys from the chunked .anim's AFSB");
            Check(Bone(r.Bones, 1).Rotation.Values.Length == 2,
                  "regression: a global bone track still reads entry 0 from the .m2");
            M2Parser.ReadAnimationInto(inFile, 1, h, anim);
            Check(h.AnimatedSequence == 1 && Bone(h.Bones, 2).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(h.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "regression: ReadAnimationInto reads bone keys from the .anim too");
            M2ParsedModel rawModel = M2Parser.Parse(inFile, 1, M2Synthetic.SkeletonAnimFile(0, false));
            Check(Bone(rawModel.Bones, 2).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(rawModel.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "regression: an unchunked .anim works the same way");
            M2Parser.ReadAnimationInto(inFile, 0, h);
            M2BoneDef[] other = M2Parser.ReadBoneTracksForSequence(h, 1, anim);
            Check(h.AnimatedSequence == 0 && Bone(other, 2).Translation.Values.Length == 2 &&
                  Math.Abs(TKey(Bone(other, 2).Translation, 1).Z - z2) < 1e-4f &&
                  Math.Abs(TKey(Bone(h.Bones, 2).Translation, 1).X - 3f) < 1e-4f,
                  "override: ReadBoneTracksForSequence works on an in-file model");

            // A header that stops before the attachment entries, and lookups that do not fit:
            // empty arrays, never an exception.
            var shortHeader = new byte[0xF4];
            PutMagic(shortHeader, 0, "MD20");
            PutU32(shortHeader, 0x04, 272);
            PutU32(shortHeader, 0x24, 5); PutU32(shortHeader, 0x28, 0x1000);
            PutU32(shortHeader, 0x34, 3); PutU32(shortHeader, 0x38, 0xF0);
            PutU32(shortHeader, 0xF0, 2);
            M2ParsedModel sh = M2Parser.Parse(shortHeader);
            Check(sh.AnimationLookup.Length == 0 && sh.KeyBoneLookup.Length == 0 &&
                  sh.Attachments.Length == 0 && sh.AttachmentLookup.Length == 0,
                  "header: a short header yields empty lookup arrays");
        }

        /// <summary>The payload offset of the first chunk with this magic, or -1.</summary>
        static int ChunkPayload(byte[] file, string magic)
        {
            for (int o = 0; o + 8 <= file.Length; o += 8 + (int)BitConverter.ToUInt32(file, o + 4))
                if (file[o] == magic[0] && file[o + 1] == magic[1] && file[o + 2] == magic[2] && file[o + 3] == magic[3])
                    return o + 8;
            return -1;
        }

        /// <summary>Give sequence `seq` of the 64-byte table at `table` these flags and alias target.</summary>
        static void PutAlias(byte[] b, int table, int seq, uint flags, int target)
        {
            PutU32(b, table + seq * 64 + 12, flags);
            PutU16(b, table + seq * 64 + 62, unchecked((ushort)target));
        }

        /// <summary>
        /// Copy each bone's translation entry `from` over entry `to`, in both nested arrays, the way
        /// retail stores an alias's entries identical to its target's. Translation is the fixture's
        /// only per-sequence track (its rotation is on a global sequence). Nested offsets are
        /// relative to `payload`; `bones` is the absolute offset of the bone records.
        /// </summary>
        static void CopyTranslationEntry(byte[] b, int payload, int bones, int boneCount, int from, int to)
        {
            for (int i = 0; i < boneCount; i++)
            {
                int track = bones + i * BoneStride + 16;
                foreach (int nested in new[] { 4, 12 })
                {
                    int entries = payload + (int)BitConverter.ToUInt32(b, track + nested + 4);
                    Buffer.BlockCopy(b, entries + from * 8, b, entries + to * 8, 8);
                }
            }
        }

        /// <summary>A copy of a chunked file whose AFID chunk has one more entry, placed first.</summary>
        static byte[] WithAfidEntry(byte[] file, int animId, int subAnimId, int fileId)
        {
            int payload = ChunkPayload(file, "AFID");
            var b = new byte[file.Length + 8];
            Buffer.BlockCopy(file, 0, b, 0, payload);
            PutU16(b, payload, (ushort)animId);
            PutU16(b, payload + 2, (ushort)subAnimId);
            PutU32(b, payload + 4, (uint)fileId);
            Buffer.BlockCopy(file, payload, b, payload + 8, file.Length - payload);
            PutU32(b, payload - 4, BitConverter.ToUInt32(file, payload - 4) + 8);
            return b;
        }

        static M2Sequence Seq(int animId, uint flags, int aliasNext)
        {
            return new M2Sequence { AnimId = (short)animId, Length = 1000, Flags = flags, AliasNext = (short)aliasNext };
        }

        /// <summary>
        /// Alias sequences (flag 0x40 without 0x20): retail character skeletons ship several
        /// animations as an alias of another sequence -- humanfemale's 139 aliases 138, and 66
        /// aliases 67, which aliases 65 -- with no AFID entry of their own and track entries
        /// identical to the target's. They play from the target's keys: its .anim, or the file
        /// itself when the target is primary. A chain that loops or leaves the table plays nothing.
        /// </summary>
        static void AliasSequenceTests()
        {
            const int skelId = 777001, skinId = 777003;
            byte[] m2 = M2Synthetic.SkeletonModel(skelId, skinId);
            byte[] anim = M2Synthetic.SkeletonAnimFile(0, true);
            float z2 = M2Synthetic.ExternalZ(2, 0);
            int afidBase = M2Synthetic.SkelAnimFileIdBase;

            // ---- the resolution rule, on a table shaped like the retail ones ----
            var t = new M2ParsedModel();
            t.Sequences = new[]
            {
                Seq(0, 0x20, -1),      //  0 Stand, in the file
                Seq(123, 0, -1),       //  1 keys in .anim 1000760 (humanfemale 65)
                Seq(63, 0x40, 3),      //  2 alias -> 3 -> 1 (66)
                Seq(138, 0x40, 1),     //  3 alias -> 1 (67)
                Seq(60, 0, -1),        //  4 keys in .anim 1000789 (138)
                Seq(208, 0xC1, 4),     //  5 alias -> 4 (139)
                Seq(400, 0x40, 0),     //  6 alias of the in-file Stand
                Seq(401, 0x40, 7),     //  7 alias of itself
                Seq(402, 0x40, 9),     //  8 alias -> 9 -> 8
                Seq(403, 0x40, 8),     //  9
                Seq(404, 0x40, 50),    // 10 alias past the table
                Seq(405, 0x40, -1),    // 11 alias of a negative index
                Seq(406, 0, -1),       // 12 no keys anywhere, not an alias
                Seq(407, 0x40, 12),    // 13 alias -> 12, a dead end
                Seq(300, 0x40, 4),     // 14 alias with an AFID entry of its own
                Seq(409, 0x60, 4),     // 15 alias that is also primary
            };
            t.AnimFileIds = new[]
            {
                new AfidEntry { AnimId = 123, SubAnimId = 0, FileDataID = 1000760 },
                new AfidEntry { AnimId = 60, SubAnimId = 0, FileDataID = 1000789 },
                new AfidEntry { AnimId = 300, SubAnimId = 0, FileDataID = 555 },
            };
            Check(M2Parser.ExternalAnimFileId(t, 5) == 1000789,
                  "alias: an alias with no AFID entry names its target's .anim (139 -> 138)");
            Check(M2Parser.ExternalAnimFileId(t, 2) == 1000760 && M2Parser.ExternalAnimFileId(t, 3) == 1000760,
                  "alias: a chain is followed to the sequence with the keys (66 -> 67 -> 65)");
            Check(M2Parser.ExternalAnimFileId(t, 1) == 1000760 && M2Parser.ExternalAnimFileId(t, 4) == 1000789 &&
                  M2Parser.ExternalAnimFileId(t, 0) == 0, "alias: sequences that are not aliases are unchanged");
            Check(M2Parser.ExternalAnimFileId(t, 6) == 0, "alias: an alias of an in-file sequence needs no .anim");
            Check(M2Parser.ExternalAnimFileId(t, 7) == 0 && M2Parser.ExternalAnimFileId(t, 8) == 0 &&
                  M2Parser.ExternalAnimFileId(t, 9) == 0, "alias: a chain that loops names no file and does not hang");
            Check(M2Parser.ExternalAnimFileId(t, 10) == 0 && M2Parser.ExternalAnimFileId(t, 11) == 0,
                  "alias: a target outside the table names no file");
            Check(M2Parser.ExternalAnimFileId(t, 13) == 0, "alias: a chain ending on a sequence without keys names no file");
            Check(M2Parser.ExternalAnimFileId(t, 14) == 555, "alias: an alias with its own AFID entry keeps its own file");
            Check(M2Parser.ExternalAnimFileId(t, 15) == 0, "alias: an alias that is primary keeps its own in-file keys");

            // ---- alias of an external sequence, through a skeleton (the humanfemale case) ----
            byte[] skel = M2Synthetic.Skeleton(0);
            int sks1 = ChunkPayload(skel, "SKS1"), skb1 = ChunkPayload(skel, "SKB1");
            int skelSeqs = sks1 + (int)BitConverter.ToUInt32(skel, sks1 + 12);
            int skelBones = skb1 + (int)BitConverter.ToUInt32(skel, skb1 + 4);
            PutAlias(skel, skelSeqs, 0, 0xC1, 1);
            CopyTranslationEntry(skel, skb1, skelBones, 3, 1, 0);
            M2ParsedModel a = M2Parser.Parse(m2);
            M2Parser.ApplySkeleton(a, skel, null, -1);
            Check(a.Sequences[0].Alias && a.Sequences[0].AliasNext == 1 && !a.Sequences[0].PrimarySequence &&
                  !a.Sequences[1].Alias, "alias: the 0x40 flag and the alias target are read from the sequence entry");
            Check(M2Parser.ExternalAnimFileId(a, 0) == afidBase,
                  "alias: through a skeleton, the alias names its target's .anim");
            Check(a.AnimatedSequence == -1 && a.AnimationSkipReason != null &&
                  a.AnimationSkipReason.Contains("alias of sequence 1"),
                  "alias: without the target's .anim the alias is not played, and the reason names the target");
            M2Parser.ReadAnimationInto(m2, 0, a, anim);
            Check(a.AnimatedSequence == 0 && a.AnimationSkipReason == null && a.RequiredAnimFileId == afidBase,
                  "alias: with the target's .anim, ReadAnimationInto plays the alias itself");
            Check(Bone(a.Bones, 2).Translation.HasData && Bone(a.Bones, 2).Translation.Times.Length == 2 &&
                  Bone(a.Bones, 2).Translation.Times[1] == 400 &&
                  Math.Abs(TKey(Bone(a.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "alias: the alias's own entries are read against the target's .anim keys");
            M2BoneDef[] fist = M2Parser.ReadBoneTracksForSequence(a, 0, anim);
            Check(Math.Abs(TKey(Bone(fist, 2).Translation, 1).Z - z2) < 1e-4f &&
                  NoTracks(M2Parser.ReadBoneTracksForSequence(a, 0, null)),
                  "alias: ReadBoneTracksForSequence judges an alias by its target too");

            // The same table with a two-step chain in between: 0 -> 2 -> 1.
            a.Sequences = new[] { Seq(63, 0x40, 2), Seq(M2Synthetic.SkelExternalAnimId, 0, -1), Seq(138, 0x40, 1) };
            M2Parser.ReadAnimationInto(m2, 0, a);
            Check(a.AnimatedSequence == -1 && M2Parser.ExternalAnimFileId(a, 0) == afidBase,
                  "alias chain: without the .anim nothing plays, and the file named is the end of the chain");
            M2Parser.ReadAnimationInto(m2, 0, a, anim);
            Check(a.AnimatedSequence == 0 && a.RequiredAnimFileId == afidBase &&
                  Math.Abs(TKey(Bone(a.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "alias chain: the alias plays from the .anim at the end of its chain");

            // An AFID entry whose fileId is 0 names no file. Retail creatures give many aliases
            // one (ladyalexstrasa2's sequence 53 aliases 52, which names 575107), and the host's
            // readAFIDSFromFile drops it. Kept, it would make the alias look like it holds its
            // own keys, so its target's .anim would never be named.
            byte[] zeroSkel = WithAfidEntry(skel, BitConverter.ToUInt16(skel, skelSeqs),
                                            BitConverter.ToUInt16(skel, skelSeqs + 2), 0);
            M2ParsedModel z = M2Parser.Parse(m2);
            M2Parser.ApplySkeleton(z, zeroSkel, null, 0, anim);
            Check(z.AnimFileIds.Length == 1 && z.AnimFileIds[0].FileDataID == afidBase,
                  "afid: an entry whose fileId is 0 is dropped, as the host drops it");
            Check(M2Parser.ExternalAnimFileId(z, 0) == afidBase && z.AnimatedSequence == 0 &&
                  Math.Abs(TKey(Bone(z.Bones, 2).Translation, 1).Z - z2) < 1e-4f,
                  "alias: an alias with a file-less AFID entry of its own still plays its target's .anim");

            // ---- alias of an in-file sequence ----
            byte[] inFile = M2Synthetic.InFileSkeletonModel(473370);
            int md21 = ChunkPayload(inFile, "MD21"), afid = ChunkPayload(inFile, "AFID");
            int seqs = md21 + (int)BitConverter.ToUInt32(inFile, md21 + 0x20);
            int bones = md21 + (int)BitConverter.ToUInt32(inFile, md21 + 0x30);
            byte[] toStand = (byte[])inFile.Clone();
            PutAlias(toStand, seqs, 1, 0x40, 0);
            CopyTranslationEntry(toStand, md21, bones, 3, 0, 1);
            PutU16(toStand, afid, 99);                           // no AFID entry names the alias
            M2ParsedModel h = M2Parser.Parse(toStand, 1);
            Check(h.AnimatedSequence == 1 && h.AnimationSkipReason == null && h.RequiredAnimFileId == 0 &&
                  M2Parser.ExternalAnimFileId(h, 1) == 0,
                  "alias: an alias of an in-file sequence plays without any .anim");
            Check(Bone(h.Bones, 2).Translation.HasData && Bone(h.Bones, 2).Translation.Times[1] == 500 &&
                  Math.Abs(TKey(Bone(h.Bones, 2).Translation, 1).X - 3f) < 1e-4f,
                  "alias: and its keys are read from the .m2");
            Check(Math.Abs(TKey(Bone(M2Parser.ReadBoneTracksForSequence(h, 1, null), 2).Translation, 1).X - 3f) < 1e-4f,
                  "alias: ReadBoneTracksForSequence reads an in-file alias without bytes");

            // ---- broken chains fall back to the idle ----
            var broken = new[] { new[] { 1, 1 }, new[] { 1, 7 }, new[] { 1, -1 } };
            string[] what = { "an alias of itself", "a target past the table", "a negative target" };
            for (int i = 0; i < broken.Length; i++)
            {
                byte[] bad = (byte[])inFile.Clone();
                PutAlias(bad, seqs, broken[i][0], 0x40, broken[i][1]);
                PutU16(bad, afid, 99);
                M2ParsedModel bm = M2Parser.Parse(bad, 1, anim);
                M2ParsedModel bn = M2Parser.Parse(bad, 1);
                Check(bn.AnimatedSequence == 0 && bn.AnimationSkipReason != null &&
                      bn.AnimationSkipReason.Contains("never reaches") && M2Parser.ExternalAnimFileId(bn, 1) == 0 &&
                      NoTracks(M2Parser.ReadBoneTracksForSequence(bn, 1, null)),
                      "alias: " + what[i] + " is not played; the idle plays and the reason says why");
                Check(bm.AnimatedSequence == 1,
                      "alias: " + what[i] + " is still played from bytes the caller supplies, as before");
            }
            byte[] loop = (byte[])inFile.Clone();
            PutAlias(loop, seqs, 0, 0x40, 1);
            PutAlias(loop, seqs, 1, 0x40, 0);
            PutU16(loop, afid, 99);
            M2ParsedModel lm = M2Parser.Parse(loop, 1);
            Check(lm.AnimatedSequence == -1 && lm.AnimationSkipReason != null &&
                  lm.AnimationSkipReason.Contains("never reaches") &&
                  M2Parser.ExternalAnimFileId(lm, 0) == 0 && M2Parser.ExternalAnimFileId(lm, 1) == 0,
                  "alias: two sequences aliasing each other play nothing and do not hang");
        }

        // ================================================================ WMO (root + group)
        //
        // Fixtures come from WmoSynthetic: format structure only, no client content. The real
        // client files were checked separately against an independent reference parse.

        static void ThrowsWmo(Action action, string chunk, string name)
        {
            try
            {
                action();
                log.Add("  FAIL  " + name + " (expected WmoParseException in " + chunk + ", nothing was thrown)");
                failures++;
            }
            catch (WmoParseException e)
            {
                if (e.Chunk == chunk && e.Message.Contains(chunk)) log.Add("  PASS  " + name);
                else
                {
                    log.Add("  FAIL  " + name + " (expected chunk " + chunk + ", got " + e.Chunk + ": " + e.Message + ")");
                    failures++;
                }
            }
            catch (Exception e)
            {
                log.Add("  FAIL  " + name + " (expected WmoParseException, got " + e.GetType().Name + ": " + e.Message + ")");
                failures++;
            }
        }

        static WowVec3 WV(float x, float y, float z) { return new WowVec3(x, y, z); }

        static KeyValuePair<string, byte[]> Kv(string tag, byte[] payload) { return new KeyValuePair<string, byte[]>(tag, payload); }

        static bool SameU32(uint[] a, params uint[] b)
        {
            if (a == null || a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;
            return true;
        }

        static bool SameBytes(byte[] a, byte[] b, int bOffset = 0, int count = -1)
        {
            if (a == null || b == null) return false;
            if (count < 0) count = b.Length - bOffset;
            if (a.Length != count) return false;
            for (int i = 0; i < count; i++) if (a[i] != b[bOffset + i]) return false;
            return true;
        }

        static string Tags(WmoChunkInfo[] chunks)
        {
            var t = new string[chunks.Length];
            for (int i = 0; i < chunks.Length; i++) t[i] = chunks[i].Tag;
            return string.Join(",", t);
        }

        /// <summary>Replace the u32 size of the n-th top-level chunk with `size`.</summary>
        static byte[] WithChunkSize(byte[] file, string tag, uint size)
        {
            byte[] b = (byte[])file.Clone();
            int p = 0;
            while (p + 8 <= b.Length)
            {
                string t = new string(new[] { (char)b[p + 3], (char)b[p + 2], (char)b[p + 1], (char)b[p] });
                uint s = (uint)(b[p + 4] | (b[p + 5] << 8) | (b[p + 6] << 16) | (b[p + 7] << 24));
                if (t == tag) { PutU32(b, p + 4, size); return b; }
                p += 8 + (int)s;
            }
            throw new InvalidOperationException("fixture has no " + tag);
        }

        static WmoSynthetic.RootSpec TwoGroupRootSpec()
        {
            return new WmoSynthetic.RootSpec
            {
                Materials = new[]
                {
                    WmoSynthetic.Material(0x00, 0, 0, 1001, tail: new uint[] { 0x3D4CCCCD }),
                    WmoSynthetic.Material(0x44, 23, 1, 0, 2002, 2003, new uint[] { 3001, 3002, 2002, 3004, 3005, 3006 },
                                          sidnColor: 0xFF112233, diffColor: 0xFF959595, groundType: 10),
                    WmoSynthetic.Material(0x80, 5, 3, 1001, 2002),
                },
                GroupInfos = new[]
                {
                    WmoSynthetic.GroupInfo(0x8, WV(-1f, -2f, -3f), WV(4f, 5f, 6f), 1),
                    WmoSynthetic.GroupInfo(0x2000, WV(10f, 0f, 0f), WV(20f, 5f, 5f), 7),
                },
                // two groups x three LOD levels; zero entries only past LOD0, as on retail
                GroupFileDataIDs = new uint[] { 5001, 5002, 6001, 6002, 0, 7002 },
                LodCount = 3,
                Flags = 0x041F,
                BoundsMin = WV(-1f, -2f, -3f),
                BoundsMax = WV(20f, 5f, 6f),
                Ambient = 0xFF102030,
                WmoId = 77,
                GroupNames = new[] { "alpha", "beta" },   // MOGN offsets 1 and 7
            };
        }

        static void WmoRootTests()
        {
            byte[] file = WmoSynthetic.BuildRoot(TwoGroupRootSpec());
            WmoRoot r = WmoParser.ParseRoot(file, "synthetic root");

            // MVER
            Check(r.Version == 17, "wmo root MVER: version 17 read");
            ThrowsWmo(() => WmoParser.ParseRoot(WmoSynthetic.Concat(WmoSynthetic.Mver(16), file.SkipBytes(12)), "v16"),
                      "MVER", "wmo root MVER: another version is rejected by name");
            ThrowsWmo(() => WmoParser.ParseRoot(file.SkipBytes(12), "no mver"), "MVER",
                      "wmo root MVER: a file not starting with MVER is rejected");

            // MOHD
            Check(r.Header.MaterialCount == 3 && r.Header.GroupCount == 2 && r.Materials.Length == 3 && r.GroupCount == 2,
                  "wmo root MOHD: material and group counts");
            Check(r.Header.Flags == 0x041F, "wmo root MOHD: u16 flags at 0x3C");
            Check(r.Header.LodCountRaw == 3 && r.Header.EffectiveLodCount == 3 && r.LodCount == 3,
                  "wmo root MOHD: u16 LOD count at 0x3E");
            Check(r.Header.AmbientColor == 0xFF102030 && r.Header.WmoId == 77, "wmo root MOHD: ambient colour and WMO id");
            Check(r.Header.BoundsMin.X == -1f && r.Header.BoundsMin.Z == -3f && r.Header.BoundsMax.X == 20f &&
                  r.Header.BoundsMax.Z == 6f, "wmo root MOHD: bounds read unconverted");
            byte[] mohd = WmoSynthetic.Mohd(1, 2, 0x8, 0, WV(0f, 0f, 0f), WV(1f, 1f, 1f), 3, 4, 5, 6, 7, 8, 9);
            WmoRoot counts = WmoParser.ParseRoot(WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOHD", mohd),
                WmoSynthetic.Chunk("MOMT", WmoSynthetic.Material(0, 0, 0, 0)),
                WmoSynthetic.Chunk("MOGI", WmoSynthetic.Concat(WmoSynthetic.GroupInfo(0, WV(0f, 0f, 0f), WV(1f, 1f, 1f), -1),
                                                               WmoSynthetic.GroupInfo(0, WV(0f, 0f, 0f), WV(1f, 1f, 1f), -1))),
                WmoSynthetic.Chunk("GFID", WmoSynthetic.U32s(11, 12))), "counts");
            Check(counts.Header.PortalCount == 3 && counts.Header.LightCount == 4 && counts.Header.DoodadNameCount == 5 &&
                  counts.Header.DoodadDefCount == 6 && counts.Header.DoodadSetCount == 7 && counts.Header.AmbientColor == 8 &&
                  counts.Header.WmoId == 9 && counts.Header.Flags == 0x8, "wmo root MOHD: every u32 field in order");
            Check(counts.Header.LodCountRaw == 0 && counts.LodCount == 1 && SameU32(counts.Lod0GroupFileDataIDs, 11, 12),
                  "wmo root MOHD: a LOD count of 0 means one GFID block");
            Check(counts.Warnings.Length == 1 && counts.Warnings[0].StartsWith("MOGN"),
                  "wmo root MOGN: a missing name table is a warning, not a failure");

            // GFID
            Check(SameU32(r.GroupFileDataIDs, 5001, 5002, 6001, 6002, 0, 7002), "wmo root GFID: every entry kept, LOD blocks included");
            Check(SameU32(r.Lod0GroupFileDataIDs, 5001, 5002), "wmo root GFID: LOD0 slice is [0 .. nGroups-1]");
            Check(SameU32(r.GetGroupFileDataIDsForLod(1), 6001, 6002) && SameU32(r.GetGroupFileDataIDsForLod(2), 0, 7002),
                  "wmo root GFID: later LOD blocks addressable separately");
            Check(r.GetGroupFileDataIDsForLod(3).Length == 0 && r.GetGroupFileDataIDsForLod(-1).Length == 0,
                  "wmo root GFID: a block past the list is empty, not an unsafe read");
            Check(r.GetGroupFileDataIDsForLod(int.MaxValue).Length == 0,
                  "wmo root GFID: a huge LOD index is empty, not a wrapped copy offset");
            Check(r.Warnings.Length == 0, "wmo root: a well-formed root has no warnings");

            var shortGfid = TwoGroupRootSpec();
            shortGfid.GroupFileDataIDs = new uint[] { 5001 };
            ThrowsWmo(() => WmoParser.ParseRoot(WmoSynthetic.BuildRoot(shortGfid), "short gfid"), "GFID",
                      "wmo root GFID: fewer entries than groups is rejected");
            var oddGfid = TwoGroupRootSpec();
            oddGfid.LodCount = 0;
            oddGfid.GroupFileDataIDs = new uint[] { 5001, 5002, 6001 };
            WmoRoot odd = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(oddGfid), "odd gfid");
            Check(SameU32(odd.Lod0GroupFileDataIDs, 5001, 5002) && odd.Warnings.Length == 1 && odd.Warnings[0].StartsWith("GFID"),
                  "wmo root GFID: a count off the groups x LOD rule still resolves LOD0, with a warning");
            var zeroGfid = TwoGroupRootSpec();
            zeroGfid.GroupFileDataIDs = new uint[] { 5001, 0, 6001, 6002, 0, 7002 };
            WmoRoot zero = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(zeroGfid), "zero gfid");
            Check(zero.Lod0GroupFileDataIDs[1] == 0 && zero.Warnings.Length == 1 && zero.Warnings[0].Contains("group 1"),
                  "wmo root GFID: a zero full-detail entry is reported, left to the loader to count as missing");
            byte[] noGfid = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOHD", WmoSynthetic.Mohd(0, 1)),
                WmoSynthetic.Chunk("MOGI", WmoSynthetic.GroupInfo(0, WV(0f, 0f, 0f), WV(1f, 1f, 1f), 0)));
            ThrowsWmo(() => WmoParser.ParseRoot(noGfid, "no gfid"), "GFID",
                      "wmo root GFID: a root without GFID is rejected (no filename-based group lookup)");
            ThrowsWmo(() => WmoParser.ParseRoot(WithChunkSize(file, "GFID", 22), "gfid 22"), "root",
                      "wmo root GFID: a size that cuts into the next header is rejected");

            // MOGI / MOGN
            Check(r.GroupInfos.Length == 2 && r.GroupInfos[0].Flags == 0x8 && r.GroupInfos[1].Flags == 0x2000,
                  "wmo root MOGI: flags per group");
            Check(r.GroupInfos[1].BoundsMin.X == 10f && r.GroupInfos[1].BoundsMax.X == 20f && r.GroupInfos[0].BoundsMin.Y == -2f,
                  "wmo root MOGI: bounds per group");
            Check(r.GroupInfos[0].NameOffset == 1 && r.GroupInfos[1].NameOffset == 7, "wmo root MOGI: name offsets");
            Check(r.GetGroupName(1) == "alpha" && r.GetGroupName(7) == "beta" && r.GetGroupName(r.GroupInfos[1].NameOffset) == "beta",
                  "wmo root MOGN: names resolve by offset");
            Check(r.GetGroupName(-1) == null && r.GetGroupName(4096) == null, "wmo root MOGN: bad offsets give null, not a read");
            Check(r.GroupNames.Length == 2 && r.GroupNames[0].Offset == 1 && r.GroupNames[0].Name == "alpha" &&
                  r.GroupNames[1].Offset == 7 && r.GroupNames[1].Name == "beta", "wmo root MOGN: every name listed with its offset");

            Check(Tags(r.Chunks) == "MVER,MOHD,MOMT,MOGN,MOGI,GFID", "wmo root: every chunk listed in file order");
            Check(r.Chunks[1].HeaderOffset == 12 && r.Chunks[1].DataOffset == 20 && r.Chunks[1].Size == 64,
                  "wmo root: chunk offsets index the source bytes");
            Check(ReferenceEquals(r.Data, file), "wmo root: the model keeps the parsed bytes, not a copy");
        }

        static void WmoMaterialTests()
        {
            WmoSynthetic.RootSpec spec = TwoGroupRootSpec();
            byte[] file = WmoSynthetic.BuildRoot(spec);
            WmoRoot r = WmoParser.ParseRoot(file, "materials");

            WmoMaterial m0 = r.Materials[0], m1 = r.Materials[1], m2 = r.Materials[2];
            Check(m1.Raw != null && m1.Raw.Length == 64 && SameBytes(m1.Raw, spec.Materials[1]),
                  "wmo MOMT: the complete 64-byte record is kept");
            Check(m1.Index == 1 && m1.Flags == 0x44 && m1.Shader == 23 && m1.BlendMode == 1,
                  "wmo MOMT: flags u32@0x00, shader u32@0x04, blend u32@0x08");
            Check(m1.Texture1 == 0 && m1.Texture2 == 2002 && m1.Texture3 == 2003,
                  "wmo MOMT: texture FileDataIDs u32@0x0C, @0x18, @0x24");
            Check(m1.SidnColor == 0xFF112233 && m1.DiffColor == 0xFF959595 && m1.GroundType == 10 && m1.FrameSidnColor == 0,
                  "wmo MOMT: colours and ground type");
            Check(SameU32(m1.Tail, 3001, 3002, 2002, 3004, 3005, 3006), "wmo MOMT: every u32 of the tail 0x28..0x3C");
            Check(m1.Color3 == 3001 && m1.Flags2 == 3002 && SameU32(m1.RuntimeData, 2002, 3004, 3005, 3006),
                  "wmo MOMT: color3, flags2 and runtime fields are the same tail bytes");
            Check(m1.GetSlot(0) == 0 && m1.GetSlot(1) == 2002 && m1.GetSlot(2) == 2003 && m1.GetSlot(3) == 3001 &&
                  m1.GetSlot(8) == 3006 && m1.GetSlot(9) == 0 && m1.GetSlot(-1) == 0,
                  "wmo MOMT: nine texture slots addressable, out-of-range slots read as 0");
            Check(m1.TailHoldsTextures && SameU32(m1.GetTextureFileDataIDs(), 2002, 2003, 3001, 3002, 3004, 3005, 3006),
                  "wmo MOMT: shader 23 with slot 1 == 0 still exposes every later texture, deduplicated");
            Check(!m0.TailHoldsTextures && m0.Color3 == 0x3D4CCCCD && SameU32(m0.GetTextureFileDataIDs(), 1001),
                  "wmo MOMT: a shader-0 color3 value (0.05f) is kept but not taken for a texture");
            Check(m2.Flags == 0x80 && m2.BlendMode == 3 && SameU32(m2.GetTextureFileDataIDs(), 1001, 2002),
                  "wmo MOMT: blend values are kept as stored");
            Check(SameU32(r.GetAllTextureFileDataIDs(), 1001, 2002, 2003, 3001, 3002, 3004, 3005, 3006),
                  "wmo MOMT: textures deduplicated across the whole WMO");
            WmoMaterial found;
            Check(r.TryGetMaterial(2, out found) && found.Shader == 5 && !r.TryGetMaterial(3, out found) &&
                  !r.TryGetMaterial(-1, out found), "wmo MOMT: TryGetMaterial guards the index");

            var tailShaders = TwoGroupRootSpec();
            tailShaders.Materials = new[]
            {
                WmoSynthetic.Material(0, 22, 0, 1, 2, 3, new uint[] { 4, 5, 6 }),
                WmoSynthetic.Material(0, 24, 0, 1, 0, 0, new uint[] { 0, 0, 0, 0, 0, 9 }),
                WmoSynthetic.Material(0, 21, 0, 1, 2, 3, new uint[] { 0, 0, 7 }),
            };
            WmoRoot ts = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(tailShaders), "tail shaders");
            Check(SameU32(ts.Materials[0].GetTextureFileDataIDs(), 1, 2, 3, 4, 5, 6), "wmo MOMT: shader 22 tail slots are textures");
            Check(SameU32(ts.Materials[1].GetTextureFileDataIDs(), 1, 9), "wmo MOMT: an unknown shader id keeps tail texture ids");
            Check(SameU32(ts.Materials[2].GetTextureFileDataIDs(), 1, 2, 3) && ts.Materials[2].Tail[2] == 7,
                  "wmo MOMT: shaders 0..21 use slots 1-3 only, the tail still preserved");

            var mismatch = TwoGroupRootSpec();
            byte[] mm = WmoSynthetic.BuildRoot(mismatch);
            byte[] withFour = (byte[])mm.Clone();
            PutU32(withFour, 20, 4);   // MOHD material count 4, MOMT holds 3
            ThrowsWmo(() => WmoParser.ParseRoot(withFour, "mat count"), "MOMT", "wmo MOMT: a record count unlike MOHD is rejected");
            byte[] noMomt = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOHD", WmoSynthetic.Mohd(1, 0)));
            ThrowsWmo(() => WmoParser.ParseRoot(noMomt, "no momt"), "MOMT", "wmo MOMT: missing while MOHD declares materials");
            byte[] partial = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOHD", WmoSynthetic.Mohd(1, 0)),
                WmoSynthetic.Chunk("MOMT", new byte[63]));
            ThrowsWmo(() => WmoParser.ParseRoot(partial, "momt 63"), "MOMT", "wmo MOMT: a partial record is rejected");
        }

        // ---------------------------------------------------------------- WMO material mapping layer

        /// <summary>The plan for one synthetic MOMT record, parsed through a real root so the record takes the
        /// same path a retail one does.</summary>
        static WmoMaterialPlan PlanOf(byte[] record)
        {
            WmoSynthetic.RootSpec spec = TwoGroupRootSpec();
            spec.Materials = new[] { record };
            WmoRoot r = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(spec), "plan root");
            return WmoMaterialSemantics.Plan(r.Materials[0]);
        }

        static bool SameCodes(WmoMaterialPlan p, params string[] codes)
        {
            if (p.Codes.Length != codes.Length) return false;
            for (int i = 0; i < codes.Length; i++) if (p.Codes[i] != codes[i]) return false;
            return true;
        }

        static bool HasNote(WmoMaterialPlan p, string fragment)
        {
            foreach (string n in p.Notes) if (n.Contains(fragment)) return true;
            return false;
        }

        static void WmoMaterialSemanticsTests()
        {
            const uint T = 1001;
            float key = 128f / 255f;

            // ---- id 0, blend 0: the client's pixel case 0 -----------------------------------------
            WmoMaterialPlan p = PlanOf(WmoSynthetic.Material(0, 0, 0, T));
            Check(p.Resolution == WmoResolution.Resolved && p.Verdict == "resolved" && p.Codes.Length == 0 &&
                  p.Permutation == WmoPermutation.Diffuse && !p.Provisional,
                  "wmo plan: id 0 blend 0 is resolved, permutation diffuse");
            Check(p.Samplers.Length == 1 && p.Samplers[0].Register == 0 && p.Samplers[0].Slot == 0 &&
                  p.Samplers[0].FileDataID == T && p.Samplers[0].UvChannel == 0 && !p.Samplers[0].KeepAlpha,
                  "wmo plan: id 0 samples only +0x0C, on UV channel 0 (MOTV set 1), alpha dropped when opaque");
            Check(p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero && p.SrcAlpha == WmoBlendFactor.One &&
                  p.DstAlpha == WmoBlendFactor.Zero && p.ZWrite && !p.AlphaTest &&
                  p.RenderQueue == WmoMaterialSemantics.QueueGeometry && p.RenderType == "Opaque",
                  "wmo plan: blend 0 is One/Zero, depth write on, no test, Geometry queue");
            Check(!p.CullOff && !p.ClampU && !p.ClampV && !p.LightBypass && p.VertexColour == "none",
                  "wmo plan: no flags -> cull back, repeat addressing, lit, no vertex colour");

            // ---- blend 1: the 128/255 key on t0.a --------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 0, 1, T));
            Check(p.Resolution == WmoResolution.Resolved && p.AlphaTest && Math.Abs(p.Cutoff - key) < 1e-6f &&
                  Math.Abs(WmoMaterialSemantics.AlphaKeyThreshold - 0.501961f) < 1e-5f &&
                  p.RenderQueue == WmoMaterialSemantics.QueueAlphaTest && p.RenderType == "TransparentCutout" &&
                  p.ZWrite && p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero && p.Samplers[0].KeepAlpha,
                  "wmo plan: id 0 blend 1 keys at 128/255 on t0.a, blending off, depth write on, alpha kept");
            Check(HasNote(p, "U-B1"), "wmo plan: the blend-1 note names the non-blocking U-B1");

            // ---- id 16 is case 0 ---------------------------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 16, 0, T));
            Check(p.Resolution == WmoResolution.Resolved && p.Permutation == WmoPermutation.Diffuse &&
                  p.PermutationName.Contains("16") && p.Samplers.Length == 1 && p.Samplers[0].Slot == 0 &&
                  p.Samplers[0].UvChannel == 0, "wmo plan: id 16 is an alias of case 0");

            // ---- flags --------------------------------------------------------------------------------
            Check(PlanOf(WmoSynthetic.Material(0x04, 0, 0, T)).CullOff, "wmo plan: flag 0x04 turns culling off");
            p = PlanOf(WmoSynthetic.Material(0x40, 0, 0, T));
            Check(p.ClampU && !p.ClampV && p.Resolution == WmoResolution.Resolved, "wmo plan: flag 0x40 clamps U only");
            p = PlanOf(WmoSynthetic.Material(0x80, 16, 1, T));
            Check(!p.ClampU && p.ClampV, "wmo plan: flag 0x80 clamps V only");
            p = PlanOf(WmoSynthetic.Material(0x01, 0, 0, T));
            Check(p.LightBypass && p.Resolution == WmoResolution.Resolved && HasNote(p, "F_UNLIT"),
                  "wmo plan: flag 0x01 bypasses the preview light on id 0");
            Check(PlanOf(WmoSynthetic.Material(0x01, 16, 0, T)).LightBypass, "wmo plan: ... and on id 16");
            p = PlanOf(WmoSynthetic.Material(0x02, 0, 0, T));
            Check(p.Resolution == WmoResolution.Resolved && HasNote(p, "no fog"), "wmo plan: flag 0x02 has no effect without fog");

            // ---- resolved-partial: inputs the client reads with no established source ----------------
            p = PlanOf(WmoSynthetic.Material(0, 0, 0, 0));
            Check(p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback && p.Provisional && SameCodes(p, "U-23b") &&
                  p.Verdict == "unresolved: U-23b" && p.Permutation == WmoPermutation.Diffuse &&
                  p.PermutationName.StartsWith("PROVISIONAL diffuse fallback (client pixel case 0") &&
                  p.Samplers.Length == 1 && p.Samplers[0].FileDataID == 0 && !p.Samplers[0].Unread && p.Samplers[0].Role.Contains("MISSING"),
                  "wmo plan: id 0 with +0x0C empty is unresolved U-23b, a labelled fallback (the whole surface is an unbound register)");
            p = PlanOf(WmoSynthetic.Material(0x04, 16, 1, 0));
            Check(p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback && SameCodes(p, "U-23b") && p.AlphaTest && p.CullOff &&
                  p.PermutationName.Contains("case 16"), "wmo plan: ... id 16 with blend 1 too (keyed on the white default, never cut)");
            p = PlanOf(WmoSynthetic.Material(0x10, 0, 0, T));
            Check(p.Resolution == WmoResolution.ResolvedPartial && SameCodes(p, "U-F2") && HasNote(p, "F_SIDN"),
                  "wmo plan: F_SIDN on id 0 is resolved-partial U-F2");
            p = PlanOf(WmoSynthetic.Material(0x28, 0, 0, T, tail: new uint[] { 0x3D4CCCCD }));
            Check(p.Resolution == WmoResolution.ResolvedPartial && SameCodes(p, "U-F2") && HasNote(p, "0x3D4CCCCD") &&
                  HasNote(p, "F_EXTLIGHT") && HasNote(p, "F_WINDOW"),
                  "wmo plan: +0x28 = 0.05f and flags 0x08/0x20 give one U-F2, every cause in the notes");

            // ---- blend 2 and above: unresolved, provisional, no Src/Dst guessed ----------------------
            foreach (uint blend in new uint[] { 2, 3, 5, 6 })
            {
                p = PlanOf(WmoSynthetic.Material(0xC5, 0, blend, T));
                Check(p.Resolution == WmoResolution.Unresolved && p.Provisional &&
                      SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5") &&
                      p.AlphaTest && p.Samplers[0].KeepAlpha && p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero &&
                      p.ZWrite && p.CullOff && !p.ClampU && !p.ClampV && !p.LightBypass,
                      "wmo plan: id 0 blend " + blend + " is unresolved U-B2..U-B5, drawn by the provisional key, flags 0x40/0x80/0x01 not applied");
            }
            Check(SameCodes(PlanOf(WmoSynthetic.Material(0, 0, 14, T)), "U-B1", "U-B2", "U-B3", "U-B4", "U-B5"),
                  "wmo plan: a blend value past the documented table adds U-B1");

            // ---- every verdict is one of three words; an id-23 env map is never read as a diffuse ------------
            // Every staged id (0/16, 23, 13, 4, 7, 5) plans its established part; what is left open is a code.
            bool threeWords = true, envNeverDiffuse = true;
            foreach (uint sh in new uint[] { 0, 4, 5, 7, 13, 16, 23, 6, 3, 25 })
                foreach (uint bl in new uint[] { 0, 1, 2, 3 })
                    foreach (bool layers in new[] { true, false })
                    {
                        WmoMaterialPlan q = layers
                            ? PlanOf(WmoSynthetic.Material(0, sh, bl, T, 2002, 2003, new uint[] { 2004, 2005, 2006, 2007, 2008, 2009 }))
                            : PlanOf(WmoSynthetic.Material(0, sh, bl, T));
                        threeWords = threeWords && (q.Verdict.StartsWith("resolved") || q.Verdict.StartsWith("unresolved"));
                        if (sh == 23)
                            foreach (WmoSamplerBinding sb in q.Samplers)
                                envNeverDiffuse = envNeverDiffuse && (sb.Slot != 0 || sb.Unread);
                    }
            Check(threeWords, "wmo plan: every shader id / blend combination is resolved, resolved-partial or unresolved");
            Check(envNeverDiffuse, "wmo plan: no id-23 plan, whatever its blend or layers, binds its +0x0C env map to a register");
            // A still-unsupported id keeps the provisional key and every reason, the blend state's included.
            p = PlanOf(WmoSynthetic.Material(0, 6, 3, T, 2002));
            Check(p.Resolution == WmoResolution.Unresolved && p.Permutation == WmoPermutation.ProvisionalBaseline && p.AlphaTest &&
                  SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", WmoMaterialSemantics.OutOfPlan),
                  "wmo plan: an unsupported id with blend 3 is unresolved on the provisional key, blend and scope codes both logged");

            // ---- ids outside the plan -------------------------------------------------------------------
            Check(SameCodes(PlanOf(WmoSynthetic.Material(0, 6, 0, T, 2002)), WmoMaterialSemantics.OutOfPlan) &&
                  PlanOf(WmoSynthetic.Material(0, 6, 0, T, 2002)).Resolution == WmoResolution.Unresolved,
                  "wmo plan: id 6 is unresolved OUT-OF-PLAN");
            Check(SameCodes(PlanOf(WmoSynthetic.Material(0, 3, 0, T, 2002)), WmoMaterialSemantics.OutOfPlan, "U-G1", "U-E2"),
                  "wmo plan: env id 3 carries U-G1 and U-E2");
            Check(SameCodes(PlanOf(WmoSynthetic.Material(0, 25, 0, T)), WmoMaterialSemantics.OutOfPlan, "U-C1"),
                  "wmo plan: an id with no client case carries U-C1");

            // ---- decode set and diagnostic line ---------------------------------------------------------
            var set = new HashSet<uint>();
            WmoMaterialSemantics.CollectSampledTextures(PlanOf(WmoSynthetic.Material(0, 0, 0, T)), set);
            WmoMaterialSemantics.CollectSampledTextures(PlanOf(WmoSynthetic.Material(0x40, 16, 1, T)), set);
            WmoMaterialSemantics.CollectSampledTextures(PlanOf(WmoSynthetic.Material(0, 0, 2, T)), set);
            Check(set.Count == 1 && set.Contains(T), "wmo plan: one FileDataID sampled by three materials is decoded once");
            string d = WmoMaterialSemantics.Describe(PlanOf(WmoSynthetic.Material(0xC0, 0, 1, T)));
            Check(d.Contains("shader 0 blend 1 flags 0x000000C0") && d.Contains("+0x0C 1001") &&
                  d.Contains("t0 diffuse <- +0x0C 1001 @ UV0 (MOTV set 1), alpha read") && d.Contains("vertex colour none") &&
                  d.Contains("Src One Dst Zero SrcA One DstA Zero") && d.Contains("ZWrite on") && d.Contains("clip < 0.50196") &&
                  d.Contains("wrap U clamp V clamp") && d.Contains("queue 2450 TransparentCutout") && d.Contains("| RESOLVED"),
                  "wmo plan: the diagnostic line carries every field");
            d = WmoMaterialSemantics.Describe(PlanOf(WmoSynthetic.Material(0, 0, 3, T)));
            Check(d.Contains("permutation 0 PROVISIONAL") && d.Contains("| UNRESOLVED: U-B2,U-B3,U-B4,U-B5"),
                  "wmo plan: the diagnostic labels a provisional material and its codes");

            // The numbering the builder hands to Unity's blend properties.
            Check((int)WmoBlendFactor.Zero == 0 && (int)WmoBlendFactor.One == 1 && (int)WmoBlendFactor.DstColor == 2 &&
                  (int)WmoBlendFactor.SrcColor == 3 && (int)WmoBlendFactor.SrcAlpha == 5 && (int)WmoBlendFactor.OneMinusSrcAlpha == 10,
                  "wmo plan: blend factors use Unity's BlendMode numbering");

            WmoFourLayerPlanTests();
            WmoTwoLayerAndOpaquePlanTests();
            WmoEnvMetalPlanTests();
        }

        /// <summary>Ids 7 and 5: the diffuse parts of client pixel cases 7 and 5 (10.1 I4/I5); their env
        /// emissives stay unresolved (U-G1, U-E3) and their env maps are neither bound nor decoded.</summary>
        static void WmoEnvMetalPlanTests()
        {
            const uint L1 = 7001, L2 = 7002, Env = 7003;

            // ---- id 7, blend 0: the diffuse part of the client's pixel case 7 ------------------------------
            WmoMaterialPlan p = PlanOf(WmoSynthetic.Material(0, 7, 0, L1, L2, Env));
            Check(p.Permutation == WmoPermutation.TwoLayerEnvMetal && p.Resolution == WmoResolution.ResolvedPartial &&
                  p.Verdict == "resolved-partial: U-G1,U-E3" && !p.Provisional && !p.ProvisionalFallback &&
                  p.PermutationName.Contains("case 7") && p.PermutationName.Contains("without its env emissive"),
                  "wmo 7: id 7 with both layers is the two-layer env metal permutation, resolved-partial U-G1,U-E3");
            Check(p.Samplers.Length == 3 &&
                  p.Samplers[0].Register == 0 && p.Samplers[0].Slot == 0 && p.Samplers[0].FileDataID == L1 && p.Samplers[0].UvChannel == 0 &&
                  !p.Samplers[0].KeepAlpha && !p.Samplers[0].Unread &&
                  p.Samplers[1].Register == 1 && p.Samplers[1].Slot == 1 && p.Samplers[1].FileDataID == L2 && p.Samplers[1].UvChannel == 1 &&
                  !p.Samplers[1].KeepAlpha && !p.Samplers[1].Unread &&
                  p.Samplers[2].Register == 2 && p.Samplers[2].Slot == 2 && p.Samplers[2].FileDataID == Env && p.Samplers[2].Unread,
                  "wmo 7: t0 = +0x0C on UV 0 (MOTV set 1), t1 = +0x18 on UV 1 (MOTV set 2), alphas not read; env +0x24 listed, unread");
            Check(p.ReadsSet2Alpha && !p.ReadsMoc2 && p.VertexColour.StartsWith("MOCV set-2 alpha") &&
                  !p.AlphaTest && p.ZWrite && p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero &&
                  p.RenderQueue == WmoMaterialSemantics.QueueGeometry && !p.CullOff && !p.LightBypass,
                  "wmo 7: reads the set-2 alpha; opaque, depth write, no test");
            var sampled = new List<uint>();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 2 && sampled.Contains(L1) && sampled.Contains(L2) && !sampled.Contains(Env),
                  "wmo 7: the decode set is +0x0C and +0x18, never the env map");
            Check(HasNote(p, "U-G1") && HasNote(p, "U-E3") && HasNote(p, "+0x24 is not decoded"),
                  "wmo 7: the note says why the env emissive is not drawn");
            string d = WmoMaterialSemantics.Describe(p);
            Check(d.Contains("permutation 5 two-layer env metal") && d.Contains("t0 layer 1 (va = 1) <- +0x0C 7001 @ UV0 (MOTV set 1), alpha not read") &&
                  d.Contains("t1 layer 2 (va = 0) <- +0x18 7002 @ UV1 (MOTV set 2), alpha not read") &&
                  d.Contains("t2 env map (emissive, not drawn) <- +0x24 7003, not bound") &&
                  d.Contains("lerp(t1.rgb, t0.rgb, va)") && d.Contains("emissive c.rgb * c.a * env(t2) NOT drawn (U-G1, U-E3)") &&
                  d.Contains("| RESOLVED-PARTIAL: U-G1,U-E3"),
                  "wmo 7: the diagnostic line shows the registers, the unbound env map, the combiner and what is not drawn");

            // ---- id 7: blend 1, flags, empty slots, blend >= 2 -------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 7, 1, L1, L2, Env));
            Check(p.Permutation == WmoPermutation.TwoLayerEnvMetal && !p.AlphaTest && p.RenderQueue == WmoMaterialSemantics.QueueGeometry &&
                  p.Resolution == WmoResolution.ResolvedPartial && HasNote(p, "case 7's alpha is 1") && HasNote(p, "never discards"),
                  "wmo 7: blend 1 is never clipped (case alpha 1), unlike the archived baseline's key");
            p = PlanOf(WmoSynthetic.Material(0xC5, 7, 0, L1, L2, Env));
            Check(p.CullOff && p.ClampU && p.ClampV && !p.LightBypass && SameCodes(p, "U-G1", "U-E3", "U-F1") && HasNote(p, "F_UNLIT"),
                  "wmo 7: flags 0x04/0x40/0x80 apply; F_UNLIT is not honoured on an emissive id (U-F1)");
            p = PlanOf(WmoSynthetic.Material(0, 7, 0, L1, 0, Env));
            Check(p.Permutation == WmoPermutation.TwoLayerEnvMetal && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  SameCodes(p, "U-23b", "U-G1", "U-E3") && p.PermutationName.StartsWith("PROVISIONAL two-layer env metal fallback") &&
                  HasNote(p, "+0x18 is empty") && p.Samplers[1].FileDataID == 0 && !p.Samplers[1].Unread,
                  "wmo 7: an empty +0x18 is unresolved U-23b, drawn by the labelled fallback (its register reads white)");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 1 && sampled.Contains(L1), "wmo 7: the fallback decodes only what it samples");
            p = PlanOf(WmoSynthetic.Material(0, 7, 0, L1, L2, 0));
            Check(p.Resolution == WmoResolution.ResolvedPartial && SameCodes(p, "U-G1", "U-E3") && HasNote(p, "+0x24 is empty"),
                  "wmo 7: an empty env slot changes nothing drawn (the emissive is not drawn in any case)");
            p = PlanOf(WmoSynthetic.Material(0, 7, 3, L1, L2, Env));
            Check(p.Permutation == WmoPermutation.TwoLayerEnvMetal && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", "U-G1", "U-E3") && !p.AlphaTest && p.ZWrite &&
                  p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero && HasNote(p, "no blend factors guessed"),
                  "wmo 7: blend 3 is unresolved U-B2..U-B5 (plus U-G1,U-E3), the case diffuse drawn opaque, no Src/Dst guessed");
            p = PlanOf(WmoSynthetic.Material(0x10, 7, 0, L1, L2, Env, tail: new uint[] { 0x3D4CCCCD }));
            Check(SameCodes(p, "U-G1", "U-E3", "U-F2") && HasNote(p, "F_SIDN") && HasNote(p, "0x3D4CCCCD"),
                  "wmo 7: F_SIDN and a non-zero +0x28 add U-F2");

            // ---- id 5, blend 0: the diffuse part of the client's pixel case 5 ------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 5, 0, L1, Env));
            Check(p.Permutation == WmoPermutation.EnvMetal && p.Resolution == WmoResolution.ResolvedPartial &&
                  p.Verdict == "resolved-partial: U-G1,U-E3" && !p.Provisional && p.PermutationName.Contains("case 5") &&
                  !p.ReadsSet2Alpha && !p.ReadsMoc2 && p.VertexColour == "none",
                  "wmo 5: id 5 is the env metal permutation, resolved-partial U-G1,U-E3, no vertex stream");
            Check(p.Samplers.Length == 2 && p.Samplers[0].Slot == 0 && p.Samplers[0].FileDataID == L1 && p.Samplers[0].UvChannel == 0 &&
                  !p.Samplers[0].KeepAlpha && !p.Samplers[0].Unread && p.Samplers[1].Register == 1 && p.Samplers[1].Slot == 1 &&
                  p.Samplers[1].FileDataID == Env && p.Samplers[1].Unread,
                  "wmo 5: t0 = +0x0C on UV 0 with its reflectivity-mask alpha dropped; env +0x18 listed, unread");
            Check(!p.AlphaTest && p.ZWrite && p.RenderQueue == WmoMaterialSemantics.QueueGeometry && p.RenderType == "Opaque",
                  "wmo 5: opaque, depth write, no test");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 1 && sampled.Contains(L1), "wmo 5: the decode set is +0x0C only");
            d = WmoMaterialSemantics.Describe(p);
            Check(d.Contains("permutation 6 env metal") && d.Contains("<- +0x0C 7001 @ UV0 (MOTV set 1), alpha not read") &&
                  d.Contains("t1 env map (emissive, not drawn) <- +0x18 7003, not bound") &&
                  d.Contains("combiner diffuse = t0.rgb, case alpha 1; emissive t0.rgb * t0.a * env(t1) NOT drawn (U-G1, U-E3)") &&
                  d.Contains("| RESOLVED-PARTIAL: U-G1,U-E3"),
                  "wmo 5: the diagnostic line");

            // ---- id 5: blend 1, flags, empty +0x0C, blend >= 2 ---------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 5, 1, L1, Env));
            Check(p.Permutation == WmoPermutation.EnvMetal && !p.AlphaTest && !p.Samplers[0].KeepAlpha &&
                  p.Resolution == WmoResolution.ResolvedPartial && HasNote(p, "case 5's alpha is 1"),
                  "wmo 5: blend 1 is never clipped (the reflectivity mask is not transparency)");
            p = PlanOf(WmoSynthetic.Material(0x05, 5, 0, L1, Env));
            Check(p.CullOff && !p.LightBypass && SameCodes(p, "U-G1", "U-E3", "U-F1"),
                  "wmo 5: two-sided from 0x04; F_UNLIT not honoured (U-F1)");
            p = PlanOf(WmoSynthetic.Material(0, 5, 0, 0, Env));
            Check(p.Permutation == WmoPermutation.EnvMetal && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  SameCodes(p, "U-23b", "U-G1", "U-E3") && p.PermutationName.StartsWith("PROVISIONAL env metal fallback") &&
                  p.PermutationName.Contains("an empty +0x0C reads white"),
                  "wmo 5: an empty +0x0C is unresolved U-23b, the labelled fallback (as id 4), drawn white");
            p = PlanOf(WmoSynthetic.Material(0, 5, 2, 0, Env));
            Check(SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", "U-23b", "U-G1", "U-E3") && p.ProvisionalFallback,
                  "wmo 5: blend 2 with an empty +0x0C carries both reasons");
            p = PlanOf(WmoSynthetic.Material(0, 5, 2, L1, Env));
            Check(p.Permutation == WmoPermutation.EnvMetal && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  !p.AlphaTest && SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", "U-G1", "U-E3") &&
                  p.PermutationName.StartsWith("PROVISIONAL env metal fallback"),
                  "wmo 5: blend 2 is unresolved U-B2..U-B5, the case diffuse drawn opaque and untested");

            Check((int)WmoPermutation.TwoLayerEnvMetal == 5 && (int)WmoPermutation.EnvMetal == 6,
                  "wmo 7/5: permutation numbers match the shader's _WmoPermutation switch");
        }

        static void WmoTwoLayerAndOpaquePlanTests()
        {
            const uint L1 = 6001, L2 = 6002;

            // ---- id 13, blend 0: the client's pixel case 13 -----------------------------------------------
            WmoMaterialPlan p = PlanOf(WmoSynthetic.Material(0, 13, 0, L1, L2));
            Check(p.Permutation == WmoPermutation.TwoLayer && p.Resolution == WmoResolution.Resolved && p.Verdict == "resolved" &&
                  !p.Provisional && !p.ProvisionalFallback && p.PermutationName.Contains("case 13"),
                  "wmo 13: id 13 with both layers and blend 0 is the resolved two-layer permutation");
            Check(p.Samplers.Length == 2 &&
                  p.Samplers[0].Register == 0 && p.Samplers[0].Slot == 0 && p.Samplers[0].FileDataID == L1 && p.Samplers[0].UvChannel == 0 &&
                  !p.Samplers[0].KeepAlpha && !p.Samplers[0].Unread &&
                  p.Samplers[1].Register == 1 && p.Samplers[1].Slot == 1 && p.Samplers[1].FileDataID == L2 && p.Samplers[1].UvChannel == 1 &&
                  !p.Samplers[1].KeepAlpha && !p.Samplers[1].Unread,
                  "wmo 13: t0 = +0x0C on UV channel 0 (MOTV set 1), t1 = +0x18 on UV channel 1 (MOTV set 2), neither alpha read");
            Check(p.ReadsSet2Alpha && !p.ReadsMoc2 && p.VertexColour.StartsWith("MOCV set-2 alpha"),
                  "wmo 13: reads the MOCV set-2 alpha (not MOC2, not set 1)");
            Check(!p.AlphaTest && p.ZWrite && p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero &&
                  p.RenderQueue == WmoMaterialSemantics.QueueGeometry && p.RenderType == "Opaque" && !p.CullOff && !p.LightBypass,
                  "wmo 13: blend 0 is opaque, depth write, no test");
            var sampled = new List<uint>();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 2 && sampled.Contains(L1) && sampled.Contains(L2), "wmo 13: the decode set is +0x0C and +0x18");
            string d = WmoMaterialSemantics.Describe(p);
            Check(d.Contains("t0 layer 1 (va = 1) <- +0x0C 6001 @ UV0 (MOTV set 1), alpha not read") &&
                  d.Contains("t1 layer 2 (va = 0) <- +0x18 6002 @ UV1 (MOTV set 2), alpha not read") &&
                  d.Contains("vertex colour MOCV set-2 alpha") && d.Contains("lerp(t1.rgb, t0.rgb, va)") && d.Contains("| RESOLVED"),
                  "wmo 13: the diagnostic line shows both registers, their UV sets, va and the combiner");

            // ---- blend 1: case alpha 1, never clipped ---------------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 13, 1, L1, L2));
            Check(p.Permutation == WmoPermutation.TwoLayer && !p.AlphaTest && p.RenderQueue == WmoMaterialSemantics.QueueGeometry &&
                  p.Resolution == WmoResolution.Resolved && !p.Samplers[0].KeepAlpha && HasNote(p, "never discards"),
                  "wmo 13: blend 1 draws untested (case alpha 1 never reaches 128/255)");

            // ---- flags ----------------------------------------------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0xC5, 13, 0, L1, L2));
            Check(p.CullOff && p.ClampU && p.ClampV && p.LightBypass && p.Resolution == WmoResolution.Resolved && HasNote(p, "F_UNLIT"),
                  "wmo 13: flags 0x04/0x40/0x80 apply and F_UNLIT bypasses the preview light (no emissive term)");
            p = PlanOf(WmoSynthetic.Material(0x20, 13, 0, L1, L2, tail: new uint[] { 0x3D4CCCCD }));
            Check(p.Resolution == WmoResolution.ResolvedPartial && SameCodes(p, "U-F2") && HasNote(p, "F_WINDOW") && HasNote(p, "0x3D4CCCCD"),
                  "wmo 13: F_WINDOW and a non-zero +0x28 are resolved-partial U-F2");

            // ---- an empty layer slot: U-23b, labelled fallback --------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 13, 0, L1, 0));
            Check(p.Permutation == WmoPermutation.TwoLayer && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  p.Provisional && SameCodes(p, "U-23b") && p.PermutationName.StartsWith("PROVISIONAL two-layer fallback") &&
                  HasNote(p, "+0x18 is empty") && p.Samplers[1].FileDataID == 0 && !p.Samplers[1].Unread &&
                  p.Samplers[1].Role.Contains("MISSING"),
                  "wmo 13: an empty +0x18 is unresolved U-23b, drawn by the labelled two-layer fallback (the register reads white)");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 1 && sampled.Contains(L1), "wmo 13: the fallback decodes only what it samples");
            p = PlanOf(WmoSynthetic.Material(0, 13, 0, 0, L2));
            Check(p.ProvisionalFallback && HasNote(p, "+0x0C is empty") && SameCodes(p, "U-23b"),
                  "wmo 13: an empty +0x0C is the same fallback");
            d = WmoMaterialSemantics.Describe(p);
            Check(d.Contains("| UNRESOLVED (PROVISIONAL fallback): U-23b") && d.Contains("<- +0x0C empty @ UV0"),
                  "wmo 13: the diagnostic labels the fallback and the empty register");

            // ---- blend 2 and above: the case kept, opaque, unresolved ------------------------------------------
            foreach (uint blend in new uint[] { 2, 3 })
            {
                p = PlanOf(WmoSynthetic.Material(0x04, 13, blend, L1, L2));
                Check(p.Permutation == WmoPermutation.TwoLayer && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                      SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5") && !p.AlphaTest && p.ZWrite &&
                      p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero && p.CullOff &&
                      HasNote(p, "no blend factors guessed"),
                      "wmo 13: blend " + blend + " is unresolved U-B2..U-B5, the case drawn opaque and untested (no Src/Dst guessed)");
            }
            p = PlanOf(WmoSynthetic.Material(0, 13, 3, L1, 0));
            Check(SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", "U-23b"), "wmo 13: blend 3 with an empty layer carries both reasons");

            // ---- the case-13 arithmetic: polarity -------------------------------------------------------------
            Check(Math.Abs(WmoMaterialSemantics.TwoLayerMix(0.8f, 0.2f, 1f) - 0.8f) < 1e-6f &&
                  Math.Abs(WmoMaterialSemantics.TwoLayerMix(0.8f, 0.2f, 0f) - 0.2f) < 1e-6f &&
                  Math.Abs(WmoMaterialSemantics.TwoLayerMix(0.8f, 0.2f, 0.25f) - 0.35f) < 1e-6f,
                  "wmo 13 mix: va 1 draws layer 1 (+0x0C), va 0 layer 2 (+0x18), linear between");
            Check(WmoMaterialSemantics.SetTwoAlpha(255) == 1f && WmoMaterialSemantics.SetTwoAlpha(0) == 0f &&
                  Math.Abs(WmoMaterialSemantics.SetTwoAlpha(127) - 127f / 255f) < 1e-7f,
                  "wmo 13 mix: the layer factor is the stored set-2 alpha / 255, no fix-up");

            // ---- id 4: the client's pixel case 4 ------------------------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 4, 0, L1));
            Check(p.Permutation == WmoPermutation.Opaque && p.Resolution == WmoResolution.Resolved && p.Verdict == "resolved" &&
                  !p.Provisional && p.PermutationName.Contains("case 4") && p.Samplers.Length == 1 && p.Samplers[0].Slot == 0 &&
                  p.Samplers[0].FileDataID == L1 && p.Samplers[0].UvChannel == 0 && !p.Samplers[0].KeepAlpha &&
                  !p.ReadsSet2Alpha && !p.ReadsMoc2 && p.VertexColour == "none",
                  "wmo 4: id 4 is the resolved opaque permutation: +0x0C on UV channel 0, alpha dropped, no vertex stream");
            Check(!p.AlphaTest && p.ZWrite && p.RenderQueue == WmoMaterialSemantics.QueueGeometry && p.RenderType == "Opaque",
                  "wmo 4: opaque, depth write, no test");
            p = PlanOf(WmoSynthetic.Material(0, 4, 1, L1));
            Check(p.Permutation == WmoPermutation.Opaque && !p.AlphaTest && !p.Samplers[0].KeepAlpha && p.Resolution == WmoResolution.Resolved &&
                  HasNote(p, "never discards"), "wmo 4: blend 1 is never clipped (case alpha 1)");
            p = PlanOf(WmoSynthetic.Material(0xC5, 4, 0, L1));
            Check(p.CullOff && p.ClampU && p.ClampV && p.LightBypass && p.Resolution == WmoResolution.Resolved,
                  "wmo 4: flags 0x04/0x40/0x80 apply, F_UNLIT bypasses the preview light");
            p = PlanOf(WmoSynthetic.Material(0, 4, 0, 0));
            Check(p.Permutation == WmoPermutation.Opaque && p.Resolution == WmoResolution.Unresolved && SameCodes(p, "U-23b") &&
                  p.ProvisionalFallback && p.PermutationName.StartsWith("PROVISIONAL opaque fallback") &&
                  HasNote(p, "whole surface"), "wmo 4: an empty +0x0C is unresolved U-23b, the labelled fallback (as ids 0/16)");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 0, "wmo 4: ... which decodes nothing");
            p = PlanOf(WmoSynthetic.Material(0, 4, 2, L1));
            Check(p.Permutation == WmoPermutation.Opaque && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  !p.AlphaTest && SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5") && p.PermutationName.StartsWith("PROVISIONAL opaque fallback"),
                  "wmo 4: blend 2 is unresolved U-B2..U-B5, the case drawn opaque and untested");
            d = WmoMaterialSemantics.Describe(PlanOf(WmoSynthetic.Material(0, 4, 0, L1)));
            Check(d.Contains("permutation 4 opaque (client pixel case 4)") && d.Contains("t0 diffuse (alpha not read) <- +0x0C 6001 @ UV0") &&
                  d.Contains("combiner diffuse = t0.rgb, case alpha 1"), "wmo 4: the diagnostic line");
            Check((int)WmoPermutation.TwoLayer == 3 && (int)WmoPermutation.Opaque == 4,
                  "wmo 13/4: permutation numbers match the shader's _WmoPermutation switch");
        }

        static void WmoFourLayerPlanTests()
        {
            const uint Env = 351431, L1 = 4001, L2 = 4002, L3 = 4003, L4 = 4004, H1 = 5001, H2 = 5002, H3 = 5003, H4 = 5004;

            // ---- complete id 23: four layers, four heights --------------------------------------------
            WmoMaterialPlan p = PlanOf(WmoSynthetic.Material(0, 23, 0, Env, L1, L2, new uint[] { L3, L4, H1, H2, H3, H4 }));
            Check(p.Permutation == WmoPermutation.FourLayer && p.Resolution == WmoResolution.ResolvedPartial && !p.Provisional &&
                  !p.ProvisionalFallback && SameCodes(p, "U-23a", "U-E2", "U-E3") &&
                  p.Verdict == "resolved-partial: U-23a,U-E2,U-E3" && p.PermutationName.Contains("case 23"),
                  "wmo 23: a complete id-23 material is the four-layer permutation, resolved-partial U-23a,U-E2,U-E3");
            bool bindings = p.Samplers.Length == 9;
            uint[] layers = { L1, L2, L3, L4 }, heights = { H1, H2, H3, H4 };
            for (int k = 0; bindings && k < 4; k++)
            {
                WmoSamplerBinding l = p.Samplers[1 + k], h = p.Samplers[5 + k];
                bindings = l.Register == 1 + k && l.ClientRegister == 1 + k && l.Slot == 1 + k && l.FileDataID == layers[k] &&
                           l.UvChannel == k && !l.KeepAlpha && !l.Unread &&
                           h.Register == 5 + k && h.ClientRegister == 17 + k && h.Slot == 5 + k && h.FileDataID == heights[k] &&
                           h.UvChannel == k && h.KeepAlpha && !h.Unread;
            }
            Check(bindings, "wmo 23: layer k = +0x18/+0x24/+0x28/+0x2C in t1..t4 and height k = +0x30..+0x3C in 5..8 (client " +
                            "t17..t20), both on UV channel k-1 (MOTV set k); only the height alpha is read");
            Check(p.Samplers[0].Register == 0 && p.Samplers[0].FileDataID == Env && p.Samplers[0].Unread,
                  "wmo 23: the +0x0C env map is listed but not bound (its emissive is not drawn)");
            Check(p.ReadsMoc2 && p.VertexColour.StartsWith("MOC2") && p.LayerMask[0] == 1f && p.LayerMask[1] == 1f &&
                  p.LayerMask[2] == 1f && p.LayerMask[3] == 1f,
                  "wmo 23: reads MOC2, every layer present in the mask");
            Check(!p.AlphaTest && p.ZWrite && p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero &&
                  p.RenderQueue == WmoMaterialSemantics.QueueGeometry && p.RenderType == "Opaque" && !p.LightBypass,
                  "wmo 23: blend 0 is opaque, depth write, no test");
            var sampled = new List<uint>();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 8 && !sampled.Contains(Env), "wmo 23: the decode set is the four layers and four heights, not the env map");
            Check(HasNote(p, "U-23a") && HasNote(p, "not decoded"), "wmo 23: the notes name the byte-3 lerp and the undrawn env emissive");

            // ---- blend 1: case alpha 1, no test -----------------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 23, 1, Env, L1, L2, new uint[] { L3, L4, H1, H2, H3, H4 }));
            Check(p.Permutation == WmoPermutation.FourLayer && !p.AlphaTest && p.RenderQueue == WmoMaterialSemantics.QueueGeometry &&
                  p.Resolution == WmoResolution.ResolvedPartial && HasNote(p, "never discards"),
                  "wmo 23: blend 1 draws untested (case alpha 1 never reaches 128/255)");

            // ---- non-contiguous layers 1011: empty layer masked, its height not read ----------------------
            p = PlanOf(WmoSynthetic.Material(0, 23, 0, 0, L1, 0, new uint[] { L3, L4, H1, H2, H3, H4 }));
            Check(p.Resolution == WmoResolution.ResolvedPartial && SameCodes(p, "U-23b", "U-23a", "U-E2", "U-E3") &&
                  p.Verdict == "resolved-partial: U-23b,U-23a,U-E2,U-E3" && !p.ProvisionalFallback &&
                  p.LayerMask[0] == 1f && p.LayerMask[1] == 0f && p.LayerMask[2] == 1f && p.LayerMask[3] == 1f &&
                  p.Samplers[2].Unread && p.Samplers[6].Unread && p.Samplers[6].FileDataID == H2 && !p.Samplers[3].Unread,
                  "wmo 23: layers 1011 mask layer 2 and leave its (present) height unread; resolved-partial with U-23b for the forced weight");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 6 && !sampled.Contains(H2), "wmo 23: an empty layer's height map is not decoded");
            Check(HasNote(p, "layer(s) 2 empty") && HasNote(p, "+0x0C is empty"), "wmo 23: the notes name the empty layer and empty env");

            // ---- a present layer without its height: U-23b, labelled fallback -----------------------------
            p = PlanOf(WmoSynthetic.Material(0, 23, 0, Env, L1, L2, new uint[] { L3, L4, 0, 0, 0, 0 }));
            Check(p.Permutation == WmoPermutation.FourLayer && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  p.Provisional && SameCodes(p, "U-23b", "U-23a", "U-E2", "U-E3") && p.PermutationName.StartsWith("PROVISIONAL") &&
                  HasNote(p, "PROVISIONAL fallback") && p.Samplers[5].FileDataID == 0 && !p.Samplers[5].Unread,
                  "wmo 23: layers without height maps are unresolved U-23b, drawn by the labelled four-layer fallback");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 4, "wmo 23: the fallback decodes only what it samples (the four layers)");
            p = PlanOf(WmoSynthetic.Material(0, 23, 0, 0, L1, L2, new uint[] { 0, 0, H1, 0, 0, 0 }));
            Check(p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback && HasNote(p, "layer(s) 2 have"),
                  "wmo 23: one present layer missing its height is enough for U-23b");
            string d = WmoMaterialSemantics.Describe(p);
            Check(d.Contains("t6 (client t18) height 2 MISSING") && d.Contains("| UNRESOLVED (PROVISIONAL fallback): U-23b") &&
                  d.Contains("t3 layer 3 (empty: weight forced to 0) <- +0x28 empty, not bound") && d.Contains("layer mask (1,1,0,0)"),
                  "wmo 23: the diagnostic line shows client registers, the missing height, unbound layers and the mask");

            // ---- no layer at all, blend 2+, flags -----------------------------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 23, 0, Env, 0, 0, new uint[] { 0, 0, H1, 0, 0, 0 }));
            Check(p.Permutation == WmoPermutation.ProvisionalBaseline && p.Resolution == WmoResolution.Unresolved &&
                  SameCodes(p, "U-23b") && p.Samplers.Length == 1 && p.Samplers[0].FileDataID == Env && p.Samplers[0].Unread,
                  "wmo 23: no layer texture at all stays on the baseline, unresolved U-23b, its env map listed but not bound");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 0, "wmo 23: ... and not decoded (the baseline draws the register's white, never the env map)");
            p = PlanOf(WmoSynthetic.Material(0, 23, 3, Env, 0, 0));
            Check(p.Permutation == WmoPermutation.ProvisionalBaseline && p.AlphaTest && !p.Samplers[0].KeepAlpha &&
                  SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", "U-23b"),
                  "wmo 23: no layer and blend 3 carries both reasons on the baseline");
            p = PlanOf(WmoSynthetic.Material(0, 23, 2, Env, L1, L2, new uint[] { L3, L4, H1, H2, H3, H4 }));
            Check(p.Permutation == WmoPermutation.FourLayer && p.Resolution == WmoResolution.Unresolved && p.ProvisionalFallback &&
                  SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", "U-23a", "U-E2", "U-E3") && !p.AlphaTest && p.ZWrite &&
                  p.SrcColor == WmoBlendFactor.One && p.DstColor == WmoBlendFactor.Zero &&
                  p.RenderQueue == WmoMaterialSemantics.QueueGeometry && HasNote(p, "no blend factors guessed") &&
                  p.PermutationName.StartsWith("PROVISIONAL four-layer fallback") && p.PermutationName.Contains("blend state not established") &&
                  p.Samplers[0].Unread,
                  "wmo 23: blend 2 keeps the four-layer arithmetic in the labelled opaque fallback, unresolved U-B2..U-B5 (no Src/Dst guessed)");
            sampled.Clear();
            WmoMaterialSemantics.CollectSampledTextures(p, sampled);
            Check(sampled.Count == 8 && !sampled.Contains(Env), "wmo 23: ... decoding its layers and heights, never the env map");
            p = PlanOf(WmoSynthetic.Material(0, 23, 5, 0, L1, 0, new uint[] { L3, L4, H1, 0, H3, H4 }));
            Check(SameCodes(p, "U-B2", "U-B3", "U-B4", "U-B5", "U-23b", "U-23a", "U-E2", "U-E3") && p.ProvisionalFallback,
                  "wmo 23: blend 5 with an empty layer carries the blend codes and U-23b once");
            p = PlanOf(WmoSynthetic.Material(0xC5, 23, 0, Env, L1, L2, new uint[] { L3, L4, H1, H2, H3, H4 }));
            Check(p.CullOff && p.ClampU && p.ClampV && !p.LightBypass && SameCodes(p, "U-23a", "U-E2", "U-E3", "U-F1"),
                  "wmo 23: flags 0x04/0x40/0x80 apply; F_UNLIT is not a bypass on an id with an emissive (U-F1)");
            p = PlanOf(WmoSynthetic.Material(0x10, 23, 0, Env, L1, L2, new uint[] { L3, L4, H1, H2, H3, H4 }));
            Check(SameCodes(p, "U-23a", "U-E2", "U-E3", "U-F2"), "wmo 23: F_SIDN adds U-F2");

            // ---- the E23 weight arithmetic on hand vectors -------------------------------------------------
            float[] one = { 1f, 1f, 1f, 1f }, all = { 1f, 1f, 1f, 1f };
            Check(Near4(WmoMaterialSemantics.FourLayerWeights(1f, 0f, 0f, one, all), 1f, 0f, 0f, 0f) &&
                  Near4(WmoMaterialSemantics.FourLayerWeights(0f, 1f, 0f, one, all), 0f, 1f, 0f, 0f) &&
                  Near4(WmoMaterialSemantics.FourLayerWeights(0f, 0f, 1f, one, all), 0f, 0f, 1f, 0f) &&
                  Near4(WmoMaterialSemantics.FourLayerWeights(0f, 0f, 0f, one, all), 0f, 0f, 0f, 1f),
                  "wmo 23 weights: the four one-hot cases (layer 4 takes 1 - sum)");
            // Equal stored weights (layer 4 takes the remaining 0.25), unequal heights, the last below the
            // 0.004 floor: aw = (0.25, 0.125, 0.0625, 0.001); the strongest keeps aw, the others
            // (1 - (max - aw)) * aw, then all four are normalised.
            float[] b = WmoMaterialSemantics.FourLayerWeights(0.25f, 0.25f, 0.25f, new[] { 1f, 0.5f, 0.25f, 0f }, all);
            float a1 = 0.25f, a2 = 0.125f, a3 = 0.0625f, a4 = 0.25f * 0.004f;
            float r1 = a1, r2 = (1f - (a1 - a2)) * a2, r3 = (1f - (a1 - a3)) * a3, r4 = (1f - (a1 - a4)) * a4, t = r1 + r2 + r3 + r4;
            Check(Near4(b, r1 / t, r2 / t, r3 / t, r4 / t) && b[0] > b[1] && b[1] > b[2] && b[2] > b[3],
                  "wmo 23 weights: equal weights with unequal heights favour the higher height map (height floor 0.004)");
            // Byte sum above 255: layer 4 is saturated to 0, the rest are used as stored.
            b = WmoMaterialSemantics.FourLayerWeights(200f / 255f, 100f / 255f, 0f, one, all);
            float s1 = 200f / 255f, s2 = 100f / 255f, q2 = (1f - (s1 - s2)) * s2;
            Check(Near4(b, s1 / (s1 + q2), q2 / (s1 + q2), 0f, 0f), "wmo 23 weights: a byte sum above 255 saturates layer 4 to 0");
            // All four in the maximum: a strong layer 3 pulls the others down to (1 - 0.7) * 0.1 each, giving
            // (0.03, 0.03, 0.8, 0) / 0.86. A maximum that skipped the third component would give 0.8 for it.
            b = WmoMaterialSemantics.FourLayerWeights(0.1f, 0.1f, 0.8f, one, all);
            Check(Near4(b, 0.03f / 0.86f, 0.03f / 0.86f, 0.8f / 0.86f, 0f), "wmo 23 weights: the third layer enters the maximum");
            // The mask: an empty layer's stored weight is ignored; everything masked gives zeros, not NaN.
            b = WmoMaterialSemantics.FourLayerWeights(0.5f, 0.5f, 0f, one, new[] { 1f, 0f, 1f, 1f });
            Check(Near4(b, 1f, 0f, 0f, 0f), "wmo 23 weights: a masked layer gets no weight");
            b = WmoMaterialSemantics.FourLayerWeights(0f, 1f, 0f, one, new[] { 1f, 0f, 1f, 1f });
            Check(Near4(b, 0f, 0f, 0f, 0f) && !float.IsNaN(b[0]), "wmo 23 weights: all weight on masked layers gives zeros, not NaN");

            // ---- an absent stream the load found: U-V4 joins the plan -------------------------------------
            p = PlanOf(WmoSynthetic.Material(0, 13, 0, 6001, 6002));
            WmoMaterialSemantics.NoteAbsentStream(p, 0, "MOCV set 2");
            Check(p.Verdict == "resolved", "wmo U-V4: no absent-stream vertex changes nothing");
            WmoMaterialSemantics.NoteAbsentStream(p, 12, "MOCV set 2");
            WmoMaterialSemantics.NoteAbsentStream(p, 3, "MOTV set 2");
            Check(p.Resolution == WmoResolution.ResolvedPartial && p.Verdict == "resolved-partial: U-V4" &&
                  HasNote(p, "12 drawn vertex(es) lack MOCV set 2") && HasNote(p, "3 drawn vertex(es) lack MOTV set 2"),
                  "wmo U-V4: a resolved material with vertices lacking a stream becomes resolved-partial U-V4, the code once");
            p = PlanOf(WmoSynthetic.Material(0, 13, 0, 6001, 0));
            WmoMaterialSemantics.NoteAbsentStream(p, 4, "MOCV set 2");
            Check(p.Resolution == WmoResolution.Unresolved && p.Verdict == "unresolved: U-23b,U-V4",
                  "wmo U-V4: an unresolved fallback stays unresolved, the code appended");
        }

        static bool Near4(float[] v, float a, float b, float c, float d)
        {
            return v.Length == 4 && Math.Abs(v[0] - a) < 1e-5f && Math.Abs(v[1] - b) < 1e-5f &&
                   Math.Abs(v[2] - c) < 1e-5f && Math.Abs(v[3] - d) < 1e-5f;
        }

        static void WmoPreservedChunkTests()
        {
            var spec = TwoGroupRootSpec();
            var modd = new byte[80];
            PutU32(modd, 0, 0x12000001);                       // MODI index 1, flags 0x12
            PutF32(modd, 4, 1f); PutF32(modd, 8, 2f); PutF32(modd, 12, 3f);
            PutF32(modd, 16, 0f); PutF32(modd, 20, 0f); PutF32(modd, 24, 0.7071f); PutF32(modd, 28, 0.7071f);
            PutF32(modd, 32, 1.5f);
            PutU32(modd, 36, 0x02FFEBDA);
            PutU32(modd, 40, 0x00000000);
            PutF32(modd, 72, 1f);
            PutF32(modd, 68, 1f);                              // identity quaternion x,y,z,w = 0,0,0,1
            var mods = new byte[32];
            string setName = "Set_$DefaultGlobal";
            for (int i = 0; i < setName.Length; i++) mods[i] = (byte)setName[i];
            PutU32(mods, 20, 0); PutU32(mods, 24, 2);
            var molt = new byte[96];
            molt[0] = 1; molt[48] = 2;
            var mfog = new byte[48];
            PutU32(mfog, 0, 0x1000);
            var mopv = WmoSynthetic.F32s(0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 5, 5, 5, 6, 5, 5, 6, 5, 6, 5, 5, 6);
            var mopt = new byte[40];
            PutU16(mopt, 0, 0); PutU16(mopt, 2, 4); PutF32(mopt, 12, 1f); PutF32(mopt, 16, -2.5f);
            PutU16(mopt, 20, 4); PutU16(mopt, 22, 4);
            var mopr = new byte[16];
            PutU16(mopr, 0, 0); PutU16(mopr, 2, 1); PutU16(mopr, 4, unchecked((ushort)-1));
            PutU16(mopr, 8, 1); PutU16(mopr, 10, 0); PutU16(mopr, 12, 1);
            var mnld = new byte[184];
            mnld[5] = 0xAB;
            // MODD deliberately BEFORE MODI: resolution must not depend on chunk order
            spec.ExtraChunks.Add(Kv("MODS", mods));
            spec.ExtraChunks.Add(Kv("MODD", modd));
            spec.ExtraChunks.Add(Kv("MOLT", molt));
            spec.ExtraChunks.Add(Kv("MFOG", mfog));
            spec.ExtraChunks.Add(Kv("MOPV", mopv));
            spec.ExtraChunks.Add(Kv("MOPT", mopt));
            spec.ExtraChunks.Add(Kv("MOPR", mopr));
            spec.ExtraChunks.Add(Kv("MNLD", mnld));
            spec.ExtraChunks.Add(Kv("MODI", WmoSynthetic.U32s(0, 9001)));
            byte[] file = WmoSynthetic.BuildRoot(spec);
            WmoRoot r = WmoParser.ParseRoot(file, "preserved");

            Check(r.DoodadSets.Length == 1 && r.DoodadSets[0].Name == setName && r.DoodadSets[0].StartIndex == 0 &&
                  r.DoodadSets[0].Count == 2, "wmo root MODS: set name, start and count");
            Check(r.DoodadDefs.Length == 2 && r.DoodadFileDataIDs.Length == 2 &&
                  r.DoodadFileDataIDs[r.DoodadDefs[0].NameIndex] == 9001,
                  "wmo root MODD/MODI: MODI after MODD still resolves the doodad FileDataID");
            WmoDoodadDef d0 = r.DoodadDefs[0];
            Check(d0.NameIndexAndFlags == 0x12000001 && d0.NameIndex == 1 && d0.Flags == 0x12,
                  "wmo root MODD: 24-bit index and 8-bit flags split");
            Check(d0.Position.Y == 2f && d0.Rotation.Z == 0.7071f && d0.Rotation.W == 0.7071f && d0.Rotation.X == 0f &&
                  d0.Scale == 1.5f && d0.Color == 0x02FFEBDA, "wmo root MODD: position, x,y,z,w rotation, scale, colour");
            Check(r.Lights.Count == 2 && r.Lights.GetRecord(1)[0] == 2 && r.Lights.GetRecord(2) == null,
                  "wmo root MOLT: raw 48-byte records preserved");
            Check(r.Fogs.Count == 1 && r.Fogs.Bytes.Length == 48 && r.Fogs.GetRecord(0)[1] == 0x10,
                  "wmo root MFOG: raw 48-byte records preserved");
            Check(r.PortalVertices.Length == 8 && r.PortalVertices[4].X == 5f, "wmo root MOPV: portal vertices");
            Check(r.Portals.Length == 2 && r.Portals[1].StartVertex == 4 && r.Portals[1].VertexCount == 4 &&
                  r.Portals[0].PlaneNormal.Z == 1f && r.Portals[0].PlaneDistance == -2.5f, "wmo root MOPT: portal info");
            Check(r.PortalRefs.Length == 2 && r.PortalRefs[0].GroupIndex == 1 && r.PortalRefs[0].Side == -1 &&
                  r.PortalRefs[1].Side == 1, "wmo root MOPR: portal references");
            WmoChunkInfo unknown;
            Check(r.TryGetChunk("MNLD", out unknown) && unknown.Size == 184 && r.CopyChunkBytes(unknown)[5] == 0xAB,
                  "wmo root: an undecoded chunk is kept as (tag, offset, size) with its bytes reachable");
            Check(Tags(r.Chunks) == "MVER,MOHD,MOMT,MOGN,MOGI,GFID,MODS,MODD,MOLT,MFOG,MOPV,MOPT,MOPR,MNLD,MODI",
                  "wmo root: preserved chunks listed in file order");
            Check(!r.HasMotx && !r.HasModn && r.Warnings.Length == 0, "wmo root: no historical name tables");

            var hist = TwoGroupRootSpec();
            hist.ExtraChunks.Add(Kv("MOTX", new byte[] { (byte)'a', 0 }));
            hist.ExtraChunks.Add(Kv("MODN", new byte[] { (byte)'b', 0 }));
            WmoRoot h = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(hist), "historical");
            Check(h.HasMotx && h.HasModn && h.Warnings.Length == 2 && h.Materials[0].Texture1 == 1001,
                  "wmo root MOTX/MODN: presence recorded, textures still FileDataIDs");

            var bad = TwoGroupRootSpec();
            bad.ExtraChunks.Add(Kv("MOLT", new byte[47]));
            bad.ExtraChunks.Add(Kv("MODD", new byte[41]));
            WmoRoot b = WmoParser.ParseRoot(WmoSynthetic.BuildRoot(bad), "bad preserved");
            WmoChunkInfo molt2;
            Check(b.Lights.Count == 0 && b.DoodadDefs.Length == 0 && b.Warnings.Length == 2 && b.TryGetChunk("MOLT", out molt2) &&
                  molt2.Size == 47, "wmo root: a malformed preserved chunk is a warning and stays listed, not a failed load");
        }

        static void WmoMalformedRootTests()
        {
            byte[] file = WmoSynthetic.BuildRoot(TwoGroupRootSpec());
            ThrowsWmo(() => WmoParser.ParseRoot(null, "null"), "root", "wmo root malformed: null data");
            ThrowsWmo(() => WmoParser.ParseRoot(new byte[0], "empty"), "MVER", "wmo root malformed: empty file");
            ThrowsWmo(() => WmoParser.ParseRoot(WithChunkSize(file, "GFID", 0xFFFFFFFF), "huge"), "GFID",
                      "wmo root malformed: a chunk claiming more bytes than the file names that chunk");
            ThrowsWmo(() => WmoParser.ParseRoot(WithChunkSize(file, "MOMT", 1000), "momt past"), "MOMT",
                      "wmo root malformed: MOMT running past the end of file");
            ThrowsWmo(() => WmoParser.ParseRoot(WmoSynthetic.Concat(file, new byte[] { 1, 2, 3 }), "trailing"), "root",
                      "wmo root malformed: trailing bytes that cannot hold a chunk header");
            ThrowsWmo(() => WmoParser.ParseRoot(file.TakeBytes(file.Length - 1), "truncated"), "GFID",
                      "wmo root malformed: a truncated last chunk");
            byte[] shortMohd = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOHD", new byte[60]));
            ThrowsWmo(() => WmoParser.ParseRoot(shortMohd, "short mohd"), "MOHD", "wmo root malformed: MOHD shorter than 64 bytes");
            ThrowsWmo(() => WmoParser.ParseRoot(WmoSynthetic.Mver(), "mver only"), "MOHD", "wmo root malformed: MOHD missing");
            byte[] hugeCount = (byte[])file.Clone();
            PutU32(hugeCount, 24, 0x7FFFFFFF);   // MOHD group count
            ThrowsWmo(() => WmoParser.ParseRoot(hugeCount, "huge count"), "MOHD", "wmo root malformed: implausible group count");
            byte[] groups3 = (byte[])file.Clone();
            PutU32(groups3, 24, 3);
            ThrowsWmo(() => WmoParser.ParseRoot(groups3, "mogi count"), "MOGI", "wmo root malformed: MOGI count unlike MOHD");
            var badMogi = TwoGroupRootSpec();
            badMogi.GroupInfos[1] = new byte[31];
            ThrowsWmo(() => WmoParser.ParseRoot(WmoSynthetic.BuildRoot(badMogi), "mogi 63"), "MOGI",
                      "wmo root malformed: MOGI not a whole number of records");
            var dup = TwoGroupRootSpec();
            dup.ExtraChunks.Add(Kv("MOMT", new byte[64 * 3]));
            ThrowsWmo(() => WmoParser.ParseRoot(WmoSynthetic.BuildRoot(dup), "dup momt"), "MOMT",
                      "wmo root malformed: a second MOMT is ambiguous and rejected");
            ThrowsWmo(() => WmoParser.ParseRoot(WmoSynthetic.SquaresGroup(0), "group as root"), "MOHD",
                      "wmo root malformed: a group file passed as a root names the missing MOHD");
        }

        /// <summary>A group exercising every stream: 4 vertices, 2 triangles, 4 MOTV, 2 MOCV, MOC2, MPY2.</summary>
        static WmoSynthetic.GroupSpec FullGroupSpec()
        {
            var s = new WmoSynthetic.GroupSpec
            {
                Flags = WmoGroupFlags.Indoor | WmoGroupFlags.ColorSet1 | WmoGroupFlags.ColorSet2 |
                        WmoGroupFlags.TwoTexCoordSets | WmoGroupFlags.ThreeTexCoordSets | WmoGroupFlags.DoodadRefs,
                Positions = new[] { 0f, 0f, 0f, 4f, 0f, 0f, 4f, 2f, 0f, 0f, 2f, 1f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 1f, 0f, 1f, 0f, 0f },
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[]
                {
                    WmoSynthetic.Batch(0, 3, 0, 2, 0, 1, decoyLarge: 77),
                    WmoSynthetic.Batch(3, 3, 0, 3, WmoBatch.FlagLargeMaterialId, 2, decoySmall: 5),
                },
                BatchCounts = new ushort[] { 1, 1, 0 },
                Mpy2 = WmoSynthetic.U16s(0x20, 1, 0x20, 300),
                Moc2 = new byte[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
                NameOffset = 1,
                DescriptiveNameOffset = 7,
                UniqueId = 4242,
                GroupLiquid = 15,
                Flags2 = 0x240,
            };
            for (int k = 0; k < 4; k++)
                s.TexCoordSets.Add(new[] { k + 0.1f, -k - 0.2f, k + 1.5f, 0f, 11.25f * (k + 1), -9.5f, 0.25f, 3f + k });
            s.ColorSets.Add(new byte[] { 10, 20, 30, 255, 11, 21, 31, 255, 12, 22, 32, 255, 13, 23, 33, 255 });
            s.ColorSets.Add(new byte[] { 0, 0, 0, 128, 0, 0, 0, 0, 1, 1, 1, 64, 2, 2, 2, 255 });
            s.ExtraChunks.Add(Kv("MOBN", new byte[16]));
            s.ExtraChunks.Add(Kv("MODR", WmoSynthetic.U16s(0, 1)));
            return s;
        }

        static void WmoGroupTests()
        {
            // MOGP header: every field, written through the header builder directly
            byte[] header = WmoSynthetic.MogpHeader(0x80202005, WV(-1f, -2f, -3f), WV(1f, 2f, 3f), 2, 3, 4, 16, 20,
                                                    portalStart: 5, portalCount: 6, fogIds: new byte[] { 1, 2, 3, 4 },
                                                    groupLiquid: 941, uniqueId: 123456, flags2: 0x280,
                                                    splitParent: -1, splitNext: 7, batchesD: 0);
            byte[] hfile = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOGP", header));
            WmoGroup h = WmoParser.ParseGroup(hfile, "header only", 3);
            WmoGroupHeader gh = h.Header;
            Check(h.Version == 17 && h.GroupIndex == 3, "wmo group MVER: version read, caller's group index recorded");
            Check(gh.GroupNameOffset == 16 && gh.DescriptiveNameOffset == 20 && gh.Flags == 0x80202005,
                  "wmo group MOGP: name offsets and flags");
            Check(gh.BoundsMin.X == -1f && gh.BoundsMin.Z == -3f && gh.BoundsMax.Y == 2f, "wmo group MOGP: bounds");
            Check(gh.PortalStart == 5 && gh.PortalCount == 6, "wmo group MOGP: portal start and count");
            Check(gh.BatchCountA == 2 && gh.BatchCountB == 3 && gh.BatchCountC == 4 && gh.BatchCountD == 0 && gh.TotalBatchCount == 9,
                  "wmo group MOGP: batch counts A/B/C");
            Check(gh.FogId0 == 1 && gh.FogId1 == 2 && gh.FogId2 == 3 && gh.FogId3 == 4, "wmo group MOGP: fog ids");
            Check(gh.GroupLiquid == 941 && gh.UniqueId == 123456 && gh.Flags2 == 0x280, "wmo group MOGP: liquid u32@52, unique id, flags2");
            Check(gh.SplitGroupParent == -1 && gh.SplitGroupNext == 7, "wmo group MOGP: split-group indices");
            Check(gh.Raw != null && gh.Raw.Length == 68 && SameBytes(gh.Raw, header), "wmo group MOGP: raw 68-byte header kept");
            Check(h.Warnings.Length == 2 && h.Warnings[0].StartsWith("MOCV") && h.Warnings[1].StartsWith("MOBA"),
                  "wmo group MOGP: flags announcing colours and counts announcing batches that are absent are warnings");

            WmoSynthetic.GroupSpec spec = FullGroupSpec();
            byte[] file = WmoSynthetic.BuildGroup(spec);
            WmoGroup g = WmoParser.ParseGroup(file, "full group");

            // MOVT / MONR / MOVI
            Check(g.VertexCount == 4 && g.Positions[1].X == 4f && g.Positions[3].Z == 1f && g.Positions[2].Y == 2f,
                  "wmo group MOVT: positions read unconverted");
            Check(g.Normals.Length == 4 && g.Normals[2].Y == 1f && g.Normals[3].X == 1f, "wmo group MONR: one normal per vertex");
            Check(g.Indices.Length == 6 && g.Indices[4] == 2 && g.Indices[5] == 3, "wmo group MOVI: u16 triangle indices");
            Check(g.Header.BoundsMax.X == 4f && g.Header.BoundsMax.Z == 1f && g.Header.UniqueId == 4242 &&
                  g.Header.GroupLiquid == 15 && g.Header.Flags2 == 0x240, "wmo group MOGP: header of a built group");
            Check(g.HasRenderGeometry && g.Warnings.Length == 0, "wmo group: a well-formed group has geometry and no warnings");
            Check(Tags(g.TopChunks) == "MVER,MOGP", "wmo group: MVER then one MOGP");
            Check(Tags(g.Chunks) == "MPY2,MOVI,MOVT,MONR,MOTV,MOBA,MOBN,MODR,MOCV,MOTV,MOTV,MOTV,MOCV,MOC2",
                  "wmo group: every sub-chunk listed in file order, undecoded ones included");
            WmoChunkInfo modr;
            Check(g.TryGetChunk("MODR", out modr) && modr.Size == 4 && g.CopyChunkBytes(modr)[2] == 1,
                  "wmo group: an undecoded sub-chunk keeps its bytes reachable");
            Check(g.Chunks[0].DataOffset == 8 + 4 + 8 + 68 + 8, "wmo group: sub-chunks start after the 68-byte header");
            WowVec3 bmin, bmax;
            Check(g.TryGetBatchBounds(out bmin, out bmax) && bmin.X == 0f && bmax.X == 4f && bmax.Z == 1f,
                  "wmo group: bounds of the batched vertices");

            var badNormals = FullGroupSpec();
            badNormals.Normals = new[] { 0f, 0f, 1f };
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(badNormals), "normals"), "MONR",
                      "wmo group MONR: a count unlike the vertex count is rejected");
            var noNormals = FullGroupSpec();
            noNormals.Normals = null;
            WmoGroup nn = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(noNormals), "no normals");
            Check(nn.Normals != null && nn.Normals.Length == 0 && nn.HasRenderGeometry, "wmo group MONR: absent normals are an empty array");
        }

        static void WmoStreamTests()
        {
            WmoSynthetic.GroupSpec spec = FullGroupSpec();
            WmoGroup g = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(spec), "streams");

            // MOTV: all four streams, file order, even though three follow other chunks
            Check(g.TexCoordSets.Length == 4, "wmo group MOTV: all four UV streams kept");
            bool uvOk = true;
            for (int k = 0; k < 4; k++)
                for (int v = 0; v < 4; v++)
                    uvOk &= g.TexCoordSets[k][v].X == spec.TexCoordSets[k][v * 2] && g.TexCoordSets[k][v].Y == spec.TexCoordSets[k][v * 2 + 1];
            Check(uvOk, "wmo group MOTV: each stream's values in file order (no stream overwrites another)");
            Check(g.TexCoordSets[2][2].X == 33.75f && g.TexCoordSets[0][2].Y == -9.5f,
                  "wmo group MOTV: coordinates outside 0..1 kept as stored");
            var badUv = FullGroupSpec();
            badUv.TexCoordSets[3] = new[] { 0f, 0f, 1f, 1f };
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(badUv), "uv count"), "MOTV",
                      "wmo group MOTV: a stream shorter than the vertex count is rejected");

            // MOCV: two streams with their set numbers
            Check(g.ColorSets.Length == 2 && g.ColorSets[0].SetNumber == 1 && g.ColorSets[1].SetNumber == 2 &&
                  g.ColorSets[0].FileOrder == 0 && g.ColorSets[1].FileOrder == 1, "wmo group MOCV: two streams are sets 1 and 2");
            Check(g.ColorSet1 != null && SameBytes(g.ColorSet1.Bgra, spec.ColorSets[0]) &&
                  SameBytes(g.ColorSet2.Bgra, spec.ColorSets[1]) && g.ColorSet2.Count == 4,
                  "wmo group MOCV: BGRA bytes per set kept as stored");
            var lone1 = FullGroupSpec();
            lone1.Flags = WmoGroupFlags.Indoor | WmoGroupFlags.ColorSet1;
            lone1.ColorSets.RemoveAt(1);
            WmoGroup l1 = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(lone1), "lone set 1");
            Check(l1.ColorSets.Length == 1 && l1.ColorSets[0].SetNumber == 1 && l1.ColorSet2 == null && l1.Warnings.Length == 0,
                  "wmo group MOCV: a lone stream with flag 0x4 is set 1");
            var lone2 = FullGroupSpec();
            lone2.Flags = WmoGroupFlags.Outdoor | WmoGroupFlags.ColorSet2;
            lone2.ColorSets.RemoveAt(1);
            WmoGroup l2 = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(lone2), "lone set 2");
            Check(l2.ColorSets.Length == 1 && l2.ColorSets[0].SetNumber == 2 && l2.ColorSet1 == null && l2.Warnings.Length == 0,
                  "wmo group MOCV: a lone stream with flag 0x4 clear is set 2");
            var unannounced = FullGroupSpec();
            unannounced.Flags = WmoGroupFlags.Outdoor;
            unannounced.ColorSets.RemoveAt(1);
            WmoGroup un = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(unannounced), "unannounced");
            Check(un.ColorSets.Length == 1 && un.ColorSets[0].SetNumber == 2 && un.Warnings.Length == 1 &&
                  un.Warnings[0].StartsWith("MOCV"), "wmo group MOCV: a stream the flags do not announce is kept, with a warning");
            Check(un.Warnings.Length == 1 && un.Warnings[0].Contains("assigned set(s) 2 "),
                  "wmo group MOCV: the warning names the set number actually assigned, not file order");
            Check(WmoParser.AssignColorSetNumbers(0x4, 1)[0] == 1 && WmoParser.AssignColorSetNumbers(0x1000000, 1)[0] == 2 &&
                  WmoParser.AssignColorSetNumbers(0x1000004, 2)[1] == 2 && WmoParser.AssignColorSetNumbers(0, 0).Length == 0 &&
                  WmoParser.AssignColorSetNumbers(0x1000004, 3)[2] == 3, "wmo group MOCV: set-number rule");
            var badCv = FullGroupSpec();
            badCv.ColorSets[1] = new byte[12];
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(badCv), "cv count"), "MOCV",
                      "wmo group MOCV: a stream shorter than the vertex count is rejected");

            // MOC2
            Check(g.Moc2 != null && SameBytes(g.Moc2, spec.Moc2), "wmo group MOC2: kept as stored");
            var noMoc2 = FullGroupSpec();
            noMoc2.Moc2 = null;
            Check(WmoParser.ParseGroup(WmoSynthetic.BuildGroup(noMoc2), "no moc2").Moc2 == null, "wmo group MOC2: null when absent");
            var shortMoc2 = FullGroupSpec();
            shortMoc2.Moc2 = new byte[8];
            WmoGroup sm = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(shortMoc2), "short moc2");
            Check(sm.Moc2.Length == 8 && sm.Warnings.Length == 1 && sm.Warnings[0].StartsWith("MOC2"),
                  "wmo group MOC2: a count unlike the vertex count is a warning (not rendered)");

            // MPY2 / MOPY
            Check(g.HasMpy2 && !g.HasMopy && g.PolyMaterials2.Length == 2 && g.PolyMaterials2[0].Flags == 0x20 &&
                  g.PolyMaterials2[1].MaterialId == 300 && g.PolyMaterials.Length == 0,
                  "wmo group MPY2: u16 flags + u16 material per triangle");
            var mopy = FullGroupSpec();
            mopy.Mpy2 = null;
            mopy.Mopy = new byte[] { 0x20, 1, 0x08, 0xFF };
            WmoGroup mp = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(mopy), "mopy");
            Check(mp.HasMopy && !mp.HasMpy2 && mp.PolyMaterials.Length == 2 && mp.PolyMaterials[0].MaterialId == 1 &&
                  mp.PolyMaterials[1].Flags == 0x08 && mp.PolyMaterials[1].MaterialId == 0xFF,
                  "wmo group MOPY: u8 flags + u8 material per triangle");
            var both = FullGroupSpec();
            both.Mopy = new byte[] { 0x20, 1, 0x20, 2 };
            WmoGroup bt = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(both), "both");
            Check(bt.HasMopy && bt.HasMpy2 && bt.PolyMaterials.Length == 2 && bt.PolyMaterials2.Length == 2,
                  "wmo group MOPY/MPY2: both retained when both are present");
            var oddMopy = FullGroupSpec();
            oddMopy.Mpy2 = null;
            oddMopy.Mopy = new byte[3];
            WmoGroup om = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(oddMopy), "mopy 3");
            Check(om.HasMopy && om.PolyMaterials.Length == 0 && om.HasRenderGeometry && om.Warnings.Length == 1 &&
                  om.Warnings[0].StartsWith("MOPY"), "wmo group MOPY: a partial record is a warning, the group still draws");
            var oddMpy2 = FullGroupSpec();
            oddMpy2.Mpy2 = new byte[6];
            WmoGroup o2 = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(oddMpy2), "mpy2 6");
            Check(o2.HasMpy2 && o2.PolyMaterials2.Length == 0 && o2.HasRenderGeometry && o2.Warnings.Length == 1 &&
                  o2.Warnings[0].StartsWith("MPY2"), "wmo group MPY2: a partial record is a warning, the group still draws");
            var oddMoc2 = FullGroupSpec();
            oddMoc2.Moc2 = new byte[6];
            WmoGroup oc = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(oddMoc2), "moc2 6");
            Check(oc.Moc2 == null && oc.HasRenderGeometry && oc.Warnings.Length == 1 && oc.Warnings[0].StartsWith("MOC2"),
                  "wmo group MOC2: a partial entry is a warning, the group still draws");
            var fewMopy = FullGroupSpec();
            fewMopy.Mpy2 = WmoSynthetic.U16s(0x20, 1);
            WmoGroup fm = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(fewMopy), "few mpy2");
            Check(fm.PolyMaterials2.Length == 1 && fm.Warnings.Length == 1 && fm.Warnings[0].StartsWith("MPY2"),
                  "wmo group MPY2: a count unlike the triangle count is a warning (not needed to draw)");
        }

        static void WmoBatchRuleTests()
        {
            Check(WmoBatch.ResolveMaterialId(0x02, 300, 7) == 300, "wmo MOBA rule: flag 0x2 reads the u16 at 0x0A");
            Check(WmoBatch.ResolveMaterialId(0x00, 300, 7) == 7, "wmo MOBA rule: flag 0x2 clear reads the u8 at 0x17");
            Check(WmoBatch.ResolveMaterialId(0x02, 0x0102, 0x02) == 258,
                  "wmo MOBA rule: the 16-bit id is not replaced by its low byte");

            var spec = FullGroupSpec();
            spec.Indices = new ushort[] { 0, 1, 2, 0, 2, 3, 1, 2, 3, 3, 2, 1 };
            spec.Mpy2 = null;
            spec.Batches = new[]
            {
                WmoSynthetic.Batch(0, 3, 0, 2, 0x00, 5, decoyLarge: 999),
                WmoSynthetic.Batch(3, 3, 0, 3, 0x02, 300, decoySmall: 44),
                WmoSynthetic.Batch(6, 5, 1, 3, 0x02, 0, decoySmall: 9),
            };
            spec.BatchCounts = new ushort[] { 1, 1, 1 };
            byte[] file = WmoSynthetic.BuildGroup(spec);
            WmoGroup g = WmoParser.ParseGroup(file, "batches");
            Check(g.Batches.Length == 3, "wmo MOBA: 24-byte records read");
            Check(g.Batches[0].MaterialId == 5 && g.Batches[0].MaterialIdLarge == 999 && g.Batches[0].MaterialIdSmall == 5,
                  "wmo MOBA: flag clear -> u8 @0x17, the u16 @0x0A (box value) ignored");
            Check(g.Batches[1].MaterialId == 300 && g.Batches[1].MaterialIdSmall == 44,
                  "wmo MOBA: flag 0x2 -> u16 @0x0A (above 255), the u8 @0x17 ignored");
            Check(g.Batches[2].MaterialId == 0 && g.Batches[2].MaterialIdSmall == 9, "wmo MOBA: flag 0x2 with id 0");
            Check(g.Batches[1].StartIndex == 3 && g.Batches[1].IndexCount == 3 && g.Batches[1].MinVertex == 0 &&
                  g.Batches[1].MaxVertex == 3 && g.Batches[1].Flags == 0x02, "wmo MOBA: start index u32@0x0C, count, min/max vertex, flags");
            Check(g.Batches[0].Raw.Length == 24 && SameBytes(g.Batches[1].Raw, spec.Batches[1]), "wmo MOBA: raw record kept");
            Check(g.Batches[2].IndexCount == 5 && g.Batches[2].TriangleIndexCount == 3,
                  "wmo MOBA: an index count off a whole triangle draws whole triangles only");
            Check(g.Warnings.Length == 1 && g.Warnings[0].Contains("batch 2"), "wmo MOBA: and says so in a warning");
            var box = new byte[24];
            PutU16(box, 0, unchecked((ushort)-2)); PutU16(box, 2, unchecked((ushort)-3)); PutU16(box, 4, unchecked((ushort)-1));
            PutU16(box, 6, 2); PutU16(box, 8, 3); PutU16(box, 10, 4); PutU16(box, 0x10, 3); PutU16(box, 0x14, 2);
            spec.Batches = new[] { box };
            spec.BatchCounts = new ushort[] { 1, 0, 0 };
            WmoBatch bb = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(spec), "box").Batches[0];
            Check(bb.BoxMinX == -2 && bb.BoxMinY == -3 && bb.BoxMinZ == -1 && bb.BoxMaxX == 2 && bb.BoxMaxY == 3 &&
                  bb.MaterialIdLarge == 4 && bb.MaterialId == 0, "wmo MOBA: box fields signed, material from u8 when flag clear");
            var miscount = FullGroupSpec();
            miscount.BatchCounts = new ushort[] { 0, 0, 5 };
            WmoGroup mc = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(miscount), "miscount");
            Check(mc.Batches.Length == 2 && mc.Warnings.Length == 1 && mc.Warnings[0].StartsWith("MOBA"),
                  "wmo MOBA: a record count unlike A+B+C is a warning; the records win");
        }

        static void WmoEmptyGroupTests()
        {
            byte[] noGeo = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOGP",
                WmoSynthetic.MogpHeader(WmoGroupFlags.NoVertices | WmoGroupFlags.Outdoor | 0x80, WV(0f, 0f, 0f), WV(0f, 0f, 0f), 0, 0, 0)));
            WmoGroup g = WmoParser.ParseGroup(noGeo, "no geometry");
            Check(g.Positions.Length == 0 && g.Normals.Length == 0 && g.Indices.Length == 0 && g.TexCoordSets.Length == 0 &&
                  g.ColorSets.Length == 0 && g.Batches.Length == 0 && g.PolyMaterials.Length == 0 && g.Moc2 == null,
                  "wmo group empty: a group without MOVT parses to empty arrays, never null");
            Check(!g.HasRenderGeometry && g.Warnings.Length == 0 && g.Chunks.Length == 0, "wmo group empty: nothing to draw, nothing wrong");
            WowVec3 mn, mx;
            Check(!g.TryGetBatchBounds(out mn, out mx), "wmo group empty: no batch bounds");

            var antiportal = new WmoSynthetic.GroupSpec
            {
                Flags = WmoGroupFlags.NoBatches | WmoGroupFlags.Outdoor,
                Positions = new[] { 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                Indices = new ushort[] { 0, 1, 2 },
                Mopy = new byte[] { 0x08, 0xFF },
            };
            WmoGroup a = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(antiportal), "no batches");
            Check(a.VertexCount == 3 && a.Batches.Length == 0 && !a.HasRenderGeometry && a.Warnings.Length == 0,
                  "wmo group empty: vertices without batches draw nothing (collision-only triangles)");

            var zeroCount = new WmoSynthetic.GroupSpec { Batches = new[] { WmoSynthetic.Batch(0, 0, 0, 0, 0, 0) } };
            WmoGroup z = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(zeroCount), "empty batch");
            Check(z.Batches.Length == 1 && z.VertexCount == 0 && !z.HasRenderGeometry, "wmo group empty: an empty batch over no vertices is harmless");

            byte[] squaresRoot = WmoSynthetic.SquaresRoot(3, 900000, 700000);
            WmoRoot sr = WmoParser.ParseRoot(squaresRoot, "squares root");
            WmoGroup sg = WmoParser.ParseGroup(WmoSynthetic.SquaresGroup(2), "squares group", 2);
            Check(sr.GroupCount == 3 && SameU32(sr.Lod0GroupFileDataIDs, 900000, 900001, 900002) && sr.GetGroupName(sr.GroupInfos[2].NameOffset) == "square2" &&
                  SameU32(sr.GetAllTextureFileDataIDs(), 700000) && sr.Warnings.Length == 0,
                  "wmo synthetic: the canned self-test root parses cleanly");
            Check(sg.HasRenderGeometry && sg.Batches[0].MaterialId == 0 && sg.Positions[0].X == 24f && sg.Warnings.Length == 0 &&
                  WmoParser.ParseGroup(WmoSynthetic.SquaresGroup(1), "sq1").Batches[0].MaterialId == 1,
                  "wmo synthetic: the canned self-test groups parse cleanly");
        }

        static void WmoMalformedGroupTests()
        {
            var past = FullGroupSpec();
            past.Batches[1] = WmoSynthetic.Batch(3, 6, 0, 3, 0x02, 2);
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(past), "past movi"), "MOBA",
                      "wmo group malformed: a batch range past the end of MOVI");
            var huge = FullGroupSpec();
            huge.Batches[1] = WmoSynthetic.Batch(0xFFFFFFFF, 3, 0, 3, 0x02, 2);
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(huge), "huge start"), "MOBA",
                      "wmo group malformed: a batch start index near 2^32 does not wrap around");
            var badIndex = FullGroupSpec();
            badIndex.Indices = new ushort[] { 0, 1, 2, 0, 2, 4 };
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(badIndex), "bad index"), "MOVI",
                      "wmo group malformed: a batched index naming a vertex past MOVT");
            var stray = FullGroupSpec();
            stray.Indices = new ushort[] { 0, 1, 2, 0, 2, 3, 9, 9, 9 };
            stray.Mpy2 = WmoSynthetic.U16s(0x20, 1, 0x20, 300, 0x20, 1);
            WmoGroup st = WmoParser.ParseGroup(WmoSynthetic.BuildGroup(stray), "stray");
            Check(st.Batches.Length == 2 && st.Warnings.Length == 1 && st.Warnings[0].StartsWith("MOVI"),
                  "wmo group malformed: a bad index outside every batch is a warning (collision data, never drawn)");
            var oddIdx = FullGroupSpec();
            oddIdx.ExtraChunks.Clear();
            byte[] oi = WmoSynthetic.BuildGroup(oddIdx);
            ThrowsWmo(() => WmoParser.ParseGroup(ResizeSubChunk(oi, "MOVI", 11), "movi 11"), "MOVI",
                      "wmo group malformed: MOVI with an odd byte count");
            ThrowsWmo(() => WmoParser.ParseGroup(ResizeSubChunk(oi, "MOVT", 13), "movt 13"), "MOVT",
                      "wmo group malformed: MOVT not a whole number of positions");
            ThrowsWmo(() => WmoParser.ParseGroup(ResizeSubChunk(oi, "MOBA", 47), "moba 47"), "MOBA",
                      "wmo group malformed: MOBA not a whole number of records");
            var nan = FullGroupSpec();
            nan.Positions[4] = float.NaN;
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(nan), "nan"), "MOVT",
                      "wmo group malformed: a non-finite position (it would poison bounds and framing)");
            var dup = FullGroupSpec();
            dup.ExtraChunks.Add(Kv("MOVT", new byte[48]));
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildGroup(dup), "dup movt"), "MOVT",
                      "wmo group malformed: two MOVT chunks are ambiguous");

            // a sub-chunk that overruns MOGP must fail even when the FILE has bytes to spare
            byte[] overrun = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOGP", WmoSynthetic.Concat(
                WmoSynthetic.MogpHeader(0x8, WV(0f, 0f, 0f), WV(1f, 1f, 1f), 0, 0, 0),
                WmoSynthetic.Chunk("MOVT", new byte[12]))), WmoSynthetic.Chunk("ZZZZ", new byte[200]));
            PutU32(overrun, 12 + 8 + 68 + 4, 100);   // MOVT claims 100 bytes, MOGP holds 12
            ThrowsWmo(() => WmoParser.ParseGroup(overrun, "overrun"), "MOVT",
                      "wmo group malformed: a sub-chunk overrunning MOGP (reads are bounded by the chunk, not the file)");
            byte[] trailing = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOGP", WmoSynthetic.Concat(
                WmoSynthetic.MogpHeader(0x8, WV(0f, 0f, 0f), WV(1f, 1f, 1f), 0, 0, 0), new byte[5])));
            ThrowsWmo(() => WmoParser.ParseGroup(trailing, "trailing"), "MOGP",
                      "wmo group malformed: bytes inside MOGP that cannot hold a sub-chunk header");
            byte[] shortHeader = WmoSynthetic.Concat(WmoSynthetic.Mver(), WmoSynthetic.Chunk("MOGP", new byte[67]));
            ThrowsWmo(() => WmoParser.ParseGroup(shortHeader, "short header"), "MOGP", "wmo group malformed: MOGP shorter than its header");
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.Mver(), "mver only"), "MOGP", "wmo group malformed: no MOGP");
            ThrowsWmo(() => WmoParser.ParseGroup(WmoSynthetic.BuildRoot(TwoGroupRootSpec()), "root as group"), "MOGP",
                      "wmo group malformed: a root passed as a group names the missing MOGP");
            byte[] full = WmoSynthetic.BuildGroup(FullGroupSpec());
            ThrowsWmo(() => WmoParser.ParseGroup(full.SkipBytes(12), "no mver"), "MVER", "wmo group malformed: no MVER first");
            ThrowsWmo(() => WmoParser.ParseGroup(full.TakeBytes(full.Length - 3), "cut"), "MOGP", "wmo group malformed: truncated file");
            ThrowsWmo(() => WmoParser.ParseGroup(null, "null"), "group", "wmo group malformed: null data");
            byte[] twoMogp = WmoSynthetic.Concat(full, WmoSynthetic.Chunk("MOGP", new byte[68]));
            ThrowsWmo(() => WmoParser.ParseGroup(twoMogp, "two mogp"), "MOGP", "wmo group malformed: two MOGP chunks");
        }

        /// <summary>Set the size of the first MOGP sub-chunk `tag` to `size` and move the MOGP end to match
        /// by truncating or padding the file, so only that one chunk is malformed.</summary>
        static byte[] ResizeSubChunk(byte[] groupFile, string tag, int size)
        {
            WmoGroup g = WmoParser.ParseGroup(groupFile, "resize");
            WmoChunkInfo c;
            if (!g.TryGetChunk(tag, out c)) throw new InvalidOperationException("fixture has no " + tag);
            var before = new byte[c.DataOffset];
            Buffer.BlockCopy(groupFile, 0, before, 0, c.DataOffset);
            var payload = new byte[size];
            Buffer.BlockCopy(groupFile, c.DataOffset, payload, 0, Math.Min(size, c.Size));
            int afterStart = c.DataOffset + c.Size;
            var after = new byte[groupFile.Length - afterStart];
            Buffer.BlockCopy(groupFile, afterStart, after, 0, after.Length);
            byte[] r = WmoSynthetic.Concat(before, payload, after);
            PutU32(r, c.HeaderOffset + 4, (uint)size);
            PutU32(r, 12 + 4, (uint)(r.Length - 20));   // MOGP size
            return r;
        }

        static void WmoNoUnsafeReadTests()
        {
            var rootSpec = TwoGroupRootSpec();
            rootSpec.ExtraChunks.Add(Kv("MODS", new byte[32]));
            rootSpec.ExtraChunks.Add(Kv("MODD", new byte[40]));
            rootSpec.ExtraChunks.Add(Kv("MODI", WmoSynthetic.U32s(1)));
            rootSpec.ExtraChunks.Add(Kv("MOLT", new byte[48]));
            rootSpec.ExtraChunks.Add(Kv("MFOG", new byte[48]));
            rootSpec.ExtraChunks.Add(Kv("MOPV", new byte[48]));
            rootSpec.ExtraChunks.Add(Kv("MOPT", new byte[20]));
            rootSpec.ExtraChunks.Add(Kv("MOPR", new byte[16]));
            byte[] root = WmoSynthetic.BuildRoot(rootSpec);
            byte[] group = WmoSynthetic.BuildGroup(FullGroupSpec());

            int other = 0, rejected = 0, accepted = 0;
            string firstOther = null;
            Action<byte[], bool> run = (bytes, isRoot) =>
            {
                try
                {
                    if (isRoot) WmoParser.ParseRoot(bytes, "fuzz"); else WmoParser.ParseGroup(bytes, "fuzz");
                    accepted++;
                }
                catch (WmoParseException) { rejected++; }
                catch (Exception e)
                {
                    other++;
                    if (firstOther == null) firstOther = e.GetType().Name + ": " + e.Message;
                }
            };

            int rootCutsRejected = 0;
            for (int len = 0; len < root.Length; len++)
            {
                int before = rejected;
                run(root.TakeBytes(len), true);
                if (rejected > before) rootCutsRejected++;
            }
            int groupCutsRejected = 0;
            for (int len = 0; len < group.Length; len++)
            {
                int before = rejected;
                run(group.TakeBytes(len), false);
                if (rejected > before) groupCutsRejected++;
            }
            Check(other == 0, "wmo safety: every truncation of a root and a group throws only WmoParseException" +
                              (firstOther != null ? " (got " + firstOther + ")" : ""));
            Check(groupCutsRejected == group.Length, "wmo safety: every truncated group is rejected (MOGP runs to end of file)");
            Check(rootCutsRejected >= root.Length - 16, "wmo safety: truncated roots are rejected unless cut at a chunk boundary");

            var rnd = new Random(20260914);
            for (int i = 0; i < 3000; i++)
            {
                bool isRoot = (i & 1) == 0;
                byte[] b = (byte[])(isRoot ? root : group).Clone();
                int flips = 1 + rnd.Next(6);
                for (int f = 0; f < flips; f++) b[rnd.Next(b.Length)] = (byte)rnd.Next(256);
                run(b, isRoot);
            }
            for (int p = 0; p < group.Length; p++)
            {
                byte[] b = (byte[])group.Clone();
                b[p] = 0xFF;
                run(b, false);
            }
            for (int p = 0; p < root.Length; p++)
            {
                byte[] b = (byte[])root.Clone();
                b[p] = 0xFF;
                run(b, true);
            }
            Check(other == 0, "wmo safety: random corruption and every byte set to 0xFF throw only WmoParseException" +
                              (firstOther != null ? " (got " + firstOther + ")" : ""));
            Check(accepted > 0 && rejected > 0, "wmo safety: the corruption sweep exercised both outcomes");
            WowParseException asBase = null;
            try { WmoParser.ParseGroup(new byte[3], "base"); }
            catch (WowParseException e) { asBase = e; }
            Check(asBase is WmoParseException, "wmo safety: WmoParseException is a WowParseException for existing handlers");
        }
    }

    static class WmoTestBytes
    {
        public static byte[] SkipBytes(this byte[] b, int n)
        {
            var r = new byte[Math.Max(0, b.Length - n)];
            Buffer.BlockCopy(b, Math.Min(n, b.Length), r, 0, r.Length);
            return r;
        }

        public static byte[] TakeBytes(this byte[] b, int n)
        {
            var r = new byte[Math.Min(n, b.Length)];
            Buffer.BlockCopy(b, 0, r, 0, r.Length);
            return r;
        }
    }
}
