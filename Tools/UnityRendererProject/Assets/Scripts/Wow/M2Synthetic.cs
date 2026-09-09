// M2Synthetic.cs
//
// Synthetic M2 / skin byte images for tests that need a model with a KNOWN shape rather than a
// client file: here, a two-sequence model whose only material input is a texture transform that
// has translation and scale keys in one sequence and none in the other. The parser tests use it
// to check the evaluator, and WmvLifecycleSelfTest builds and drives it through the real runtime
// (build, sequence change, sequence change back) inside the player. No Unity type in this file.
//
// Layout written (bare MD20, header 0x100):
//   3 vertices (one triangle), 1 texture, 1 material (opaque, unlit), 1 texture lookup,
//   1 global sequence (1000 ms), 2 sequences (animId 0 "Stand" and animId 1, both in-file,
//   1000 ms), 1 texture transform with translation (0.25, 0.5) and scale (2, 2) keyed at t = 0
//   in `keyedSequence` only, 1 transform lookup [0]; optionally one bone (root, no tracks) that
//   every vertex is fully weighted to, so the model is built through the skinned path with a
//   sequence that moves no bone.

using System;

namespace Wmv.Wow
{
    public static class M2Synthetic
    {
        const int HeaderSize = 0x100;
        const int BoneStride = 88;

        static void PutU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24);
        }
        static void PutU16(byte[] b, int o, ushort v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); }
        static void PutF32(byte[] b, int o, float v) { Buffer.BlockCopy(BitConverter.GetBytes(v), 0, b, o, 4); }
        static void PutMagic(byte[] b, int o, string m) { for (int i = 0; i < 4; i++) b[o + i] = (byte)m[i]; }

        /// <summary>A growable byte image with a pool for the nested key arrays.</summary>
        sealed class Image
        {
            public byte[] B;
            public int Pool;
            public Image(int fixedSize) { B = new byte[fixedSize + 4096]; Pool = fixedSize; }
            public int Take(int n)
            {
                int o = Pool;
                Pool += n;
                if (Pool > B.Length) Array.Resize(ref B, Pool * 2);
                return o;
            }
            public byte[] Done() { Array.Resize(ref B, Pool); return B; }
        }

        /// <summary>
        /// Write one M2Track (20 bytes) at trackOffset: interpolation, global sequence, and one
        /// nested (times, values) array pair per sequence. A null entry is a sequence with no keys.
        /// values[s] is already encoded (12 bytes per vec3 key).
        /// </summary>
        static void Track(Image f, int trackOffset, ushort interpolation, short globalSeq, uint[][] times, byte[][] values)
        {
            int nSeq = times.Length;
            PutU16(f.B, trackOffset, interpolation);
            PutU16(f.B, trackOffset + 2, unchecked((ushort)globalSeq));
            int th = f.Take(nSeq * 8), vh = f.Take(nSeq * 8);
            PutU32(f.B, trackOffset + 4, (uint)nSeq); PutU32(f.B, trackOffset + 8, (uint)th);
            PutU32(f.B, trackOffset + 12, (uint)nSeq); PutU32(f.B, trackOffset + 16, (uint)vh);
            for (int s = 0; s < nSeq; s++)
            {
                if (times[s] == null)
                {
                    PutU32(f.B, th + s * 8, 0); PutU32(f.B, th + s * 8 + 4, 0);
                    PutU32(f.B, vh + s * 8, 0); PutU32(f.B, vh + s * 8 + 4, 0);
                    continue;
                }
                int to = f.Take(times[s].Length * 4);
                for (int k = 0; k < times[s].Length; k++) PutU32(f.B, to + k * 4, times[s][k]);
                int vo = f.Take(values[s].Length);
                Buffer.BlockCopy(values[s], 0, f.B, vo, values[s].Length);
                PutU32(f.B, th + s * 8, (uint)times[s].Length); PutU32(f.B, th + s * 8 + 4, (uint)to);
                PutU32(f.B, vh + s * 8, (uint)times[s].Length); PutU32(f.B, vh + s * 8 + 4, (uint)vo);
            }
        }

        static byte[] V3(float x, float y, float z)
        {
            var b = new byte[12];
            PutF32(b, 0, x); PutF32(b, 4, y); PutF32(b, 8, z);
            return b;
        }

        /// <summary>The translation and scale the keyed sequence carries, WoW UV space.</summary>
        public const float KeyedTx = 0.25f, KeyedTy = 0.5f, KeyedSx = 2f, KeyedSy = 2f;

        /// <summary>
        /// The two-sequence model. keyedSequence (0 or 1) is the sequence whose transform has keys;
        /// the other has none. skinned adds one root bone with no tracks and weights every vertex
        /// to it.
        /// </summary>
        public static byte[] TransformSwitchModel(int keyedSequence, bool skinned)
        {
            const int nSeq = 2;
            int boneCount = skinned ? 1 : 0;
            int oVerts = HeaderSize;                          // 3 * 48
            int oTex = oVerts + 3 * 48;                       // 1 * 16
            int oMat = oTex + 16;                             // 4
            int oTexLookup = oMat + 4;                        // 2
            int oGlobals = oTexLookup + 2;                    // 4
            int oSeqs = oGlobals + 4;                         // nSeq * 64
            int oBones = oSeqs + nSeq * 64;                   // boneCount * 88
            int oXf = oBones + boneCount * BoneStride;        // 60
            int oXfLookup = oXf + 60;                         // 2
            int fixedEnd = oXfLookup + 2;

            var f = new Image(fixedEnd);
            byte[] b = f.B;
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x14, 1); PutU32(b, 0x18, (uint)oGlobals);
            PutU32(b, oGlobals, 1000);
            PutU32(b, 0x1C, nSeq); PutU32(b, 0x20, (uint)oSeqs);
            for (int i = 0; i < nSeq; i++)
            {
                int o = oSeqs + i * 64;
                PutU16(b, o, (ushort)i);                      // animId: 0 = Stand, 1 = the other
                PutU32(b, o + 4, 1000);                       // length
                PutU32(b, o + 12, 0x20);                      // keys are in this file
            }
            PutU32(b, 0x2C, (uint)boneCount); PutU32(b, 0x30, (uint)oBones);
            PutU32(b, 0x3C, 3); PutU32(b, 0x40, (uint)oVerts);
            for (int i = 0; i < 3; i++)
            {
                int o = oVerts + i * 48;
                PutF32(b, o + 0, i == 1 ? 1f : 0f);           // a triangle in the XY plane
                PutF32(b, o + 4, i == 2 ? 1f : 0f);
                PutF32(b, o + 8, 0f);
                if (skinned) { b[o + 12] = 255; b[o + 16] = 0; }   // fully weighted to bone 0
                PutF32(b, o + 20, 0f); PutF32(b, o + 24, 0f); PutF32(b, o + 28, 1f);   // normal
                PutF32(b, o + 32, i == 1 ? 1f : 0f);          // uv0
                PutF32(b, o + 36, i == 2 ? 1f : 0f);
            }
            PutU32(b, 0x44, 1);                               // one skin profile
            PutU32(b, 0x50, 1); PutU32(b, 0x54, (uint)oTex);
            PutU32(b, oTex, 0); PutU32(b, oTex + 4, 3);       // type 0 (named), wrap x|y
            PutU32(b, 0x70, 1); PutU32(b, 0x74, (uint)oMat);
            PutU16(b, oMat, 0x01);                            // unlit
            PutU16(b, oMat + 2, 0);                           // opaque
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)oTexLookup);
            PutU16(b, oTexLookup, 0);
            PutU32(b, 0x60, 1); PutU32(b, 0x64, (uint)oXf);
            PutU32(b, 0x98, 1); PutU32(b, 0x9C, (uint)oXfLookup);
            PutU16(b, oXfLookup, 0);
            for (int i = 0; i < boneCount; i++)
            {
                int o = oBones + i * BoneStride;
                PutU32(b, o, unchecked((uint)-1));            // keyBoneId
                PutU32(b, o + 4, 0);                          // flags
                PutU16(b, o + 8, unchecked((ushort)-1));      // parent: none
                PutU16(b, o + 10, 0);
                // three tracks with no data (all zero), pivot at the origin
            }
            int k = keyedSequence, e = 1 - keyedSequence;
            var tT = new uint[nSeq][]; var tV = new byte[nSeq][];
            tT[k] = new uint[] { 0 }; tV[k] = V3(KeyedTx, KeyedTy, 0f); tT[e] = null; tV[e] = null;
            Track(f, oXf, 0, -1, tT, tV);
            Track(f, oXf + 20, 0, -1, new uint[nSeq][], new byte[nSeq][]);   // rotation: nothing
            var sT = new uint[nSeq][]; var sV = new byte[nSeq][];
            sT[k] = new uint[] { 0 }; sV[k] = V3(KeyedSx, KeyedSy, 1f); sT[e] = null; sV[e] = null;
            Track(f, oXf + 40, 0, -1, sT, sV);
            return f.Done();
        }


        /// <summary>
        /// The size of the FULL MD20 header. TransformSwitchModel and friends stop at 0x100
        /// because nothing they test lives past it; the emitter arrays are the last two entries
        /// in the header, at 0x120 and 0x128, so a model that carries emitters must be built out
        /// to here or those offsets land in whatever array came first.
        /// </summary>
        const int FullHeaderSize = 0x138;

        public const int EmitterParticleStride = 492;
        public const int EmitterRibbonStride = 176;

        /// <summary>
        /// A model with one particle emitter and one ribbon emitter, whose EMISSION RATE has keys
        /// in exactly one of two sequences.
        ///
        /// That last part is the point of the fixture: an emitter with no rate keys in the
        /// playing sequence emits nothing, which is authored behaviour rather than a fault (11 of
        /// Val'anyr's 13 emitters are in exactly that state on its first sub-animation), and a
        /// sequence change has to start and stop them accordingly. One bone, animated, so the
        /// ribbon has something to trail behind.
        /// </summary>
        public static byte[] EmitterModel(int keyedSequence)
        {
            const int nSeq = 2;
            const int boneCount = 1;
            int oVerts = FullHeaderSize;
            int oTex = oVerts + 3 * 48;
            int oMat = oTex + 2 * 16;
            int oTexLookup = oMat + 2 * 4;
            int oSeqs = oTexLookup + 2;
            int oBones = oSeqs + nSeq * 64;
            int oRampTimes = oBones + boneCount * BoneStride;
            int oRampColor = oRampTimes + 3 * 2;
            int oRampAlpha = oRampColor + 3 * 12;
            int oRampSize = oRampAlpha + 3 * 2;
            int oRibbonTex = oRampSize + 3 * 8;
            int oRibbonMat = oRibbonTex + 2;
            int oParticle = oRibbonMat + 2;
            int oRibbon = oParticle + EmitterParticleStride;
            int fixedEnd = oRibbon + EmitterRibbonStride;

            var f = new Image(fixedEnd);
            byte[] b = f.B;
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x1C, nSeq); PutU32(b, 0x20, (uint)oSeqs);
            for (int i = 0; i < nSeq; i++)
            {
                int o = oSeqs + i * 64;
                PutU16(b, o, (ushort)i);
                PutU32(b, o + 4, 1000);
                PutU32(b, o + 12, 0x20);
            }
            PutU32(b, 0x2C, boneCount); PutU32(b, 0x30, (uint)oBones);
            PutU32(b, 0x3C, 3); PutU32(b, 0x40, (uint)oVerts);
            for (int i = 0; i < 3; i++)
            {
                int o = oVerts + i * 48;
                PutF32(b, o + 0, i == 1 ? 1f : 0f);
                PutF32(b, o + 4, i == 2 ? 1f : 0f);
                b[o + 12] = 255; b[o + 16] = 0;               // fully weighted to bone 0
                PutF32(b, o + 28, 1f);                        // normal +Z
            }
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, 2); PutU32(b, 0x54, (uint)oTex);
            for (int i = 0; i < 2; i++) { PutU32(b, oTex + i * 16, 0); PutU32(b, oTex + i * 16 + 4, 3); }
            PutU32(b, 0x70, 2); PutU32(b, 0x74, (uint)oMat);
            PutU16(b, oMat, 0x01); PutU16(b, oMat + 2, 0);            // material 0: unlit opaque
            PutU16(b, oMat + 4, 0x01); PutU16(b, oMat + 6, 4);        // material 1: additive
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)oTexLookup);
            PutU16(b, oTexLookup, 0);
            PutU32(b, 0x120, 1); PutU32(b, 0x124, (uint)oRibbon);
            PutU32(b, 0x128, 1); PutU32(b, 0x12C, (uint)oParticle);

            // Bone 0: a translation track that MOVES in both sequences, so a ribbon has a spine.
            int ob = oBones;
            PutU32(b, ob, unchecked((uint)-1));
            PutU16(b, ob + 8, unchecked((ushort)-1));         // no parent
            var bT = new uint[nSeq][]; var bV = new byte[nSeq][];
            for (int s = 0; s < nSeq; s++)
            {
                bT[s] = new uint[] { 0, 500, 1000 };
                bV[s] = Concat(V3(0f, 0f, 0f), V3(1f, 0f, 0f), V3(0f, 0f, 0f));
            }
            Track(f, ob + 16, 1, -1, bT, bV);
            Track(f, ob + 36, 0, -1, new uint[nSeq][], new byte[nSeq][]);
            Track(f, ob + 56, 0, -1, new uint[nSeq][], new byte[nSeq][]);
            b = f.B;                                          // Track may have grown the array

            // Ramps: three stops, with the middle one at 0.25 rather than the halfway point.
            PutU16(b, oRampTimes + 0, 0);
            PutU16(b, oRampTimes + 2, 8192);
            PutU16(b, oRampTimes + 4, 32767);
            for (int i = 0; i < 3; i++)
            {
                PutF32(b, oRampColor + i * 12 + 0, 255f);
                PutF32(b, oRampColor + i * 12 + 4, 128f);
                PutF32(b, oRampColor + i * 12 + 8, 0f);
                PutU16(b, oRampAlpha + i * 2, (ushort)(i == 1 ? 32767 : 0));
                PutF32(b, oRampSize + i * 8 + 0, 0.2f);
                PutF32(b, oRampSize + i * 8 + 4, 0.1f);
            }
            PutU16(b, oRibbonTex, 1);          // texture slot 1
            PutU16(b, oRibbonMat, 1);          // material 1 -> blend 4

            // ---- the particle emitter ----
            int p = oParticle;
            PutU32(b, p + 0, unchecked((uint)-1));
            PutU32(b, p + 4, 0);
            PutF32(b, p + 8, 0f); PutF32(b, p + 12, 0f); PutF32(b, p + 16, 0f);
            PutU16(b, p + 20, 0);              // bone 0
            PutU16(b, p + 22, 0);              // texture slot 0
            b[p + 40] = 4;                     // blend: additive on alpha
            b[p + 41] = 1;                     // plane emitter
            PutU16(b, p + 48, 1); PutU16(b, p + 50, 1);     // 1x1
            int pp = p + 260;
            PutU32(b, pp + 0, 3); PutU32(b, pp + 4, (uint)oRampTimes);
            PutU32(b, pp + 8, 3); PutU32(b, pp + 12, (uint)oRampColor);
            PutU32(b, pp + 16, 3); PutU32(b, pp + 20, (uint)oRampTimes);
            PutU32(b, pp + 24, 3); PutU32(b, pp + 28, (uint)oRampAlpha);
            PutU32(b, pp + 32, 3); PutU32(b, pp + 36, (uint)oRampTimes);
            PutU32(b, pp + 40, 3); PutU32(b, pp + 44, (uint)oRampSize);
            PutF32(b, pp + 100, 1f); PutF32(b, pp + 104, 1f);

            // Lifespan: keys in BOTH sequences, so only the rate decides whether anything emits.
            var lT = new uint[nSeq][]; var lV = new byte[nSeq][];
            for (int s = 0; s < nSeq; s++) { lT[s] = new uint[] { 0 }; lV[s] = F1(2f); }
            Track(f, p + 152, 0, -1, lT, lV);

            // EmissionRate: keys in ONE sequence only.
            var rT = new uint[nSeq][]; var rV = new byte[nSeq][];
            rT[keyedSequence] = new uint[] { 0 };
            rV[keyedSequence] = F1(30f);
            Track(f, p + 176, 0, -1, rT, rV);

            // ---- the ribbon emitter ----
            int r = oRibbon;
            PutU32(f.B, r + 0, unchecked((uint)-1));
            PutU32(f.B, r + 4, 0);                            // bone 0
            PutU32(f.B, r + 20, 1); PutU32(f.B, r + 24, (uint)oRibbonTex);
            PutU32(f.B, r + 28, 1); PutU32(f.B, r + 32, (uint)oRibbonMat);
            PutF32(f.B, r + 116, 40f);                        // edges per second
            PutF32(f.B, r + 120, 0.5f);                       // edge lifetime
            var aT = new uint[nSeq][]; var aV = new byte[nSeq][];
            for (int s = 0; s < nSeq; s++) { aT[s] = new uint[] { 0 }; aV[s] = F1(0.3f); }
            Track(f, r + 76, 0, -1, aT, aV);                  // above
            var bT2 = new uint[nSeq][]; var bV2 = new byte[nSeq][];
            for (int s = 0; s < nSeq; s++) { bT2[s] = new uint[] { 0 }; bV2[s] = F1(0.3f); }
            Track(f, r + 96, 0, -1, bT2, bV2);                // below
            return f.Done();
        }

        static byte[] F1(float v) { return BitConverter.GetBytes(v); }

        static byte[] Concat(params byte[][] parts)
        {
            int n = 0;
            foreach (byte[] p in parts) n += p.Length;
            var outp = new byte[n];
            int o = 0;
            foreach (byte[] p in parts) { Buffer.BlockCopy(p, 0, outp, o, p.Length); o += p.Length; }
            return outp;
        }

        /// <summary>
        /// A model with one triangle per submesh and nothing animated: the shape the geoset tests
        /// need, where each submesh can carry its own geoset id. Same single unlit opaque material
        /// and single texture as the transform model, so nothing but the geosets varies.
        /// </summary>
        public static byte[] GeosetModel(int submeshCount)
        {
            const int nSeq = 1;
            int verts = submeshCount * 3;
            int oVerts = HeaderSize;
            int oTex = oVerts + verts * 48;
            int oMat = oTex + 16;
            int oTexLookup = oMat + 4;
            int oSeqs = oTexLookup + 2;
            int fixedEnd = oSeqs + nSeq * 64;

            var f = new Image(fixedEnd);
            byte[] b = f.B;
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x1C, nSeq); PutU32(b, 0x20, (uint)oSeqs);
            PutU16(b, oSeqs, 0);
            PutU32(b, oSeqs + 4, 1000);
            PutU32(b, oSeqs + 12, 0x20);
            PutU32(b, 0x3C, (uint)verts); PutU32(b, 0x40, (uint)oVerts);
            for (int i = 0; i < verts; i++)
            {
                int o = oVerts + i * 48;
                PutF32(b, o + 0, (i % 3) == 1 ? 1f : 0f);
                PutF32(b, o + 4, (i % 3) == 2 ? 1f : 0f);
                PutF32(b, o + 8, i / 3);
                PutF32(b, o + 20, 0f); PutF32(b, o + 24, 0f); PutF32(b, o + 28, 1f);
                PutF32(b, o + 32, (i % 3) == 1 ? 1f : 0f);
                PutF32(b, o + 36, (i % 3) == 2 ? 1f : 0f);
            }
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, 1); PutU32(b, 0x54, (uint)oTex);
            PutU32(b, oTex, 0); PutU32(b, oTex + 4, 3);
            PutU32(b, 0x70, 1); PutU32(b, 0x74, (uint)oMat);
            PutU16(b, oMat, 0x01);
            PutU16(b, oMat + 2, 0);
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)oTexLookup);
            PutU16(b, oTexLookup, 0);
            return f.Done();
        }

        /// <summary>
        /// A model whose materials cover a chosen set of (render flags, blend mode) pairs, one
        /// submesh and one batch each. It exists so the OUTPUT GATE can be tested against real
        /// materials built by the real builder: _Emissive is what decides whether the preview-light
        /// roll-off runs, and it is set from the blend mode (additive) or the material's 0x01 UNLIT
        /// flag. Each pair is (flags[i], blends[i]); batch i draws submesh i against material i.
        /// </summary>
        public static byte[] MaterialModeModel(int[] flags, int[] blends)
        {
            int n = flags.Length;
            int verts = n * 3;
            int oVerts = HeaderSize;
            int oTex = oVerts + verts * 48;
            int oMat = oTex + 16;
            int oTexLookup = oMat + n * 4;
            int oSeqs = oTexLookup + 2;
            int fixedEnd = oSeqs + 64;

            var f = new Image(fixedEnd);
            byte[] b = f.B;
            PutMagic(b, 0, "MD20");
            PutU32(b, 0x04, 272);
            PutU32(b, 0x1C, 1); PutU32(b, 0x20, (uint)oSeqs);
            PutU16(b, oSeqs, 0);
            PutU32(b, oSeqs + 4, 1000);
            PutU32(b, oSeqs + 12, 0x20);
            PutU32(b, 0x3C, (uint)verts); PutU32(b, 0x40, (uint)oVerts);
            for (int i = 0; i < verts; i++)
            {
                int o = oVerts + i * 48;
                PutF32(b, o + 0, (i % 3) == 1 ? 1f : 0f);
                PutF32(b, o + 4, (i % 3) == 2 ? 1f : 0f);
                PutF32(b, o + 8, i / 3);
                PutF32(b, o + 20, 0f); PutF32(b, o + 24, 0f); PutF32(b, o + 28, 1f);
                PutF32(b, o + 32, (i % 3) == 1 ? 1f : 0f);
                PutF32(b, o + 36, (i % 3) == 2 ? 1f : 0f);
            }
            PutU32(b, 0x44, 1);
            PutU32(b, 0x50, 1); PutU32(b, 0x54, (uint)oTex);
            PutU32(b, oTex, 0); PutU32(b, oTex + 4, 3);
            PutU32(b, 0x70, (uint)n); PutU32(b, 0x74, (uint)oMat);
            for (int i = 0; i < n; i++)
            {
                PutU16(b, oMat + i * 4, (ushort)flags[i]);
                PutU16(b, oMat + i * 4 + 2, (ushort)blends[i]);
            }
            PutU32(b, 0x80, 1); PutU32(b, 0x84, (uint)oTexLookup);
            PutU16(b, oTexLookup, 0);
            return f.Done();
        }

        /// <summary>The matching skin: submesh i drawn by batch i against material i.</summary>
        public static byte[] MaterialModeSkin(int n)
        {
            const int headerSize = 0x30;
            int vertOffset = headerSize;
            int triOffset = vertOffset + n * 3 * 2;
            int subOffset = triOffset + n * 3 * 2;
            int batchOffset = subOffset + n * 48;
            var b = new byte[batchOffset + n * 24];
            PutMagic(b, 0, "SKIN");
            PutU32(b, 0x04, (uint)(n * 3)); PutU32(b, 0x08, (uint)vertOffset);
            PutU32(b, 0x0C, (uint)(n * 3)); PutU32(b, 0x10, (uint)triOffset);
            PutU32(b, 0x14, 0); PutU32(b, 0x18, 0);
            PutU32(b, 0x1C, (uint)n); PutU32(b, 0x20, (uint)subOffset);
            PutU32(b, 0x24, (uint)n); PutU32(b, 0x28, (uint)batchOffset);
            for (int i = 0; i < n * 3; i++)
            {
                PutU16(b, vertOffset + i * 2, (ushort)i);
                PutU16(b, triOffset + i * 2, (ushort)i);
            }
            for (int s = 0; s < n; s++)
            {
                int o = subOffset + s * 48;
                PutU16(b, o + 0, 0);                         // every submesh at geoset id 0
                PutU16(b, o + 4, (ushort)(s * 3));
                PutU16(b, o + 6, 3);
                PutU16(b, o + 8, (ushort)(s * 3));
                PutU16(b, o + 10, 3);
                int bo = batchOffset + s * 24;
                PutU16(b, bo + 4, (ushort)s);                // submesh
                PutU16(b, bo + 8, 0xFFFF);                   // no colour entry
                PutU16(b, bo + 10, (ushort)s);               // material s
                PutU16(b, bo + 14, 1);                       // one texture unit
                PutU16(b, bo + 16, 0);
                PutU16(b, bo + 18, 0xFFFF);
                PutU16(b, bo + 20, 0xFFFF);
                PutU16(b, bo + 22, 0xFFFF);
            }
            return b;
        }

        /// <summary>
        /// The matching skin: one submesh per entry of geosetIds, each one triangle, each with its
        /// own batch. This is how a component model that carries alternatives is shaped.
        /// </summary>
        public static byte[] GeosetSkin(int[] geosetIds)
        {
            int n = geosetIds.Length;
            const int headerSize = 0x30;
            int vertOffset = headerSize;
            int triOffset = vertOffset + n * 3 * 2;
            int subOffset = triOffset + n * 3 * 2;
            int batchOffset = subOffset + n * 48;
            var b = new byte[batchOffset + n * 24];
            PutMagic(b, 0, "SKIN");
            PutU32(b, 0x04, (uint)(n * 3)); PutU32(b, 0x08, (uint)vertOffset);
            PutU32(b, 0x0C, (uint)(n * 3)); PutU32(b, 0x10, (uint)triOffset);
            PutU32(b, 0x14, 0); PutU32(b, 0x18, 0);
            PutU32(b, 0x1C, (uint)n); PutU32(b, 0x20, (uint)subOffset);
            PutU32(b, 0x24, (uint)n); PutU32(b, 0x28, (uint)batchOffset);
            for (int i = 0; i < n * 3; i++)
            {
                PutU16(b, vertOffset + i * 2, (ushort)i);
                PutU16(b, triOffset + i * 2, (ushort)i);
            }
            for (int s = 0; s < n; s++)
            {
                int o = subOffset + s * 48;
                PutU16(b, o + 0, (ushort)geosetIds[s]);      // the geoset id
                PutU16(b, o + 4, (ushort)(s * 3));           // vertexStart
                PutU16(b, o + 6, 3);                         // vertexCount
                PutU16(b, o + 8, (ushort)(s * 3));           // indexStart
                PutU16(b, o + 10, 3);                        // indexCount
                int bo = batchOffset + s * 24;
                PutU16(b, bo + 4, (ushort)s);                // submesh
                PutU16(b, bo + 8, 0xFFFF);                   // no colour entry
                PutU16(b, bo + 14, 1);                       // one texture unit
                PutU16(b, bo + 16, 0);
                PutU16(b, bo + 18, 0xFFFF);
                PutU16(b, bo + 20, 0xFFFF);
                PutU16(b, bo + 22, 0xFFFF);
            }
            return b;
        }

        /// <summary>
        /// The skin for TransformSwitchModel: three vertices, one triangle, one submesh, one batch
        /// with one texture unit, no colour entry, no weight entry, transform combo 0.
        /// </summary>
        public static byte[] TransformSwitchSkin()
        {
            const int headerSize = 0x30;
            int vertOffset = headerSize;
            int triOffset = vertOffset + 3 * 2;
            int subOffset = triOffset + 3 * 2;
            int batchOffset = subOffset + 48;
            var b = new byte[batchOffset + 24];
            PutMagic(b, 0, "SKIN");
            PutU32(b, 0x04, 3); PutU32(b, 0x08, (uint)vertOffset);
            PutU32(b, 0x0C, 3); PutU32(b, 0x10, (uint)triOffset);
            PutU32(b, 0x14, 0); PutU32(b, 0x18, 0);
            PutU32(b, 0x1C, 1); PutU32(b, 0x20, (uint)subOffset);
            PutU32(b, 0x24, 1); PutU32(b, 0x28, (uint)batchOffset);
            for (int i = 0; i < 3; i++) { PutU16(b, vertOffset + i * 2, (ushort)i); PutU16(b, triOffset + i * 2, (ushort)i); }
            PutU16(b, subOffset + 0, 0);                      // geoset 0: always drawn
            PutU16(b, subOffset + 6, 3);                      // vertexCount
            PutU16(b, subOffset + 10, 3);                     // indexCount
            PutU16(b, batchOffset + 4, 0);                    // submesh
            PutU16(b, batchOffset + 8, 0xFFFF);               // no colour entry
            PutU16(b, batchOffset + 14, 1);                   // one texture unit
            PutU16(b, batchOffset + 16, 0);                   // texture combo
            PutU16(b, batchOffset + 18, 0xFFFF);              // coord combo
            PutU16(b, batchOffset + 20, 0xFFFF);              // weight combo: none resolves
            PutU16(b, batchOffset + 22, 0);                   // transform combo -> lookup[0] -> transform 0
            return b;
        }
    }
}
