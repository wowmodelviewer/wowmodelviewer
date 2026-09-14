// WmoSynthetic.cs
//
// Synthetic WMO root / group byte images for tests that need a map object with a KNOWN shape
// rather than a client file. WowParserTests uses the chunk helpers to build valid and malformed
// files; a player self-test can use Root/Group specs to drive the real WMO runtime without
// any client data. No Unity type in this file, and no game content: only format structure.
//
// Layout written by BuildGroup (sub-chunk order modelled on retail, where the second and later
// MOTV/MOCV streams follow the BSP chunks):
//   MVER, MOGP { header, MOPY|MPY2, MOVI, MOVT, MONR, MOTV[0], MOBA, extra chunks, MOCV[0],
//                MOTV[1..], MOCV[1..], MOC2 }

using System;
using System.Collections.Generic;

namespace Wmv.Wow
{
    public static class WmoSynthetic
    {
        // ------------------------------------------------------------------ byte helpers

        public static void PutU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); b[o + 2] = (byte)(v >> 16); b[o + 3] = (byte)(v >> 24);
        }
        public static void PutU16(byte[] b, int o, ushort v) { b[o] = (byte)v; b[o + 1] = (byte)(v >> 8); }
        public static void PutF32(byte[] b, int o, float v) { Buffer.BlockCopy(BitConverter.GetBytes(v), 0, b, o, 4); }

        /// <summary>One chunk: the tag stored reversed (as the client files do), u32 size, payload.</summary>
        public static byte[] Chunk(string tag, byte[] payload)
        {
            if (tag == null || tag.Length != 4)
                throw new ArgumentException("chunk tags have four characters");
            payload = payload ?? new byte[0];
            var b = new byte[8 + payload.Length];
            for (int i = 0; i < 4; i++)
                b[i] = (byte)tag[3 - i];
            PutU32(b, 4, (uint)payload.Length);
            Buffer.BlockCopy(payload, 0, b, 8, payload.Length);
            return b;
        }

        public static byte[] Concat(params byte[][] parts)
        {
            int n = 0;
            foreach (byte[] p in parts)
                if (p != null) n += p.Length;
            var r = new byte[n];
            int o = 0;
            foreach (byte[] p in parts)
            {
                if (p == null) continue;
                Buffer.BlockCopy(p, 0, r, o, p.Length);
                o += p.Length;
            }
            return r;
        }

        public static byte[] U32s(params uint[] values)
        {
            var b = new byte[values.Length * 4];
            for (int i = 0; i < values.Length; i++)
                PutU32(b, i * 4, values[i]);
            return b;
        }

        public static byte[] U16s(params ushort[] values)
        {
            var b = new byte[values.Length * 2];
            for (int i = 0; i < values.Length; i++)
                PutU16(b, i * 2, values[i]);
            return b;
        }

        public static byte[] F32s(params float[] values)
        {
            var b = new byte[values.Length * 4];
            for (int i = 0; i < values.Length; i++)
                PutF32(b, i * 4, values[i]);
            return b;
        }

        public static byte[] Mver(uint version = 17) { return Chunk("MVER", U32s(version)); }

        // ------------------------------------------------------------------ root records

        /// <summary>A 64-byte MOHD payload.</summary>
        public static byte[] Mohd(uint materials, uint groups, ushort flags = 0, ushort lodCount = 0,
                                  WowVec3 boundsMin = default(WowVec3), WowVec3 boundsMax = default(WowVec3),
                                  uint portals = 0, uint lights = 0, uint doodadNames = 0, uint doodadDefs = 0,
                                  uint doodadSets = 0, uint ambient = 0, uint wmoId = 0)
        {
            var b = new byte[64];
            PutU32(b, 0x00, materials);
            PutU32(b, 0x04, groups);
            PutU32(b, 0x08, portals);
            PutU32(b, 0x0C, lights);
            PutU32(b, 0x10, doodadNames);
            PutU32(b, 0x14, doodadDefs);
            PutU32(b, 0x18, doodadSets);
            PutU32(b, 0x1C, ambient);
            PutU32(b, 0x20, wmoId);
            PutVec3(b, 0x24, boundsMin);
            PutVec3(b, 0x30, boundsMax);
            PutU16(b, 0x3C, flags);
            PutU16(b, 0x3E, lodCount);
            return b;
        }

        /// <summary>A 64-byte MOMT record. `tail` fills +0x28..+0x3C (up to six u32).</summary>
        public static byte[] Material(uint flags, uint shader, uint blend, uint texture1, uint texture2 = 0,
                                      uint texture3 = 0, uint[] tail = null, uint sidnColor = 0, uint diffColor = 0,
                                      uint groundType = 0, uint frameSidnColor = 0)
        {
            var b = new byte[64];
            PutU32(b, 0x00, flags);
            PutU32(b, 0x04, shader);
            PutU32(b, 0x08, blend);
            PutU32(b, 0x0C, texture1);
            PutU32(b, 0x10, sidnColor);
            PutU32(b, 0x14, frameSidnColor);
            PutU32(b, 0x18, texture2);
            PutU32(b, 0x1C, diffColor);
            PutU32(b, 0x20, groundType);
            PutU32(b, 0x24, texture3);
            if (tail != null)
                for (int i = 0; i < tail.Length && i < 6; i++)
                    PutU32(b, 0x28 + i * 4, tail[i]);
            return b;
        }

        /// <summary>A 32-byte MOGI record.</summary>
        public static byte[] GroupInfo(uint flags, WowVec3 boundsMin, WowVec3 boundsMax, int nameOffset)
        {
            var b = new byte[32];
            PutU32(b, 0, flags);
            PutVec3(b, 4, boundsMin);
            PutVec3(b, 16, boundsMax);
            PutU32(b, 28, (uint)nameOffset);
            return b;
        }

        // ------------------------------------------------------------------ group records

        /// <summary>A 68-byte MOGP header.</summary>
        public static byte[] MogpHeader(uint flags, WowVec3 boundsMin, WowVec3 boundsMax, ushort batchesA, ushort batchesB,
                                        ushort batchesC, int nameOffset = 0, int descriptiveNameOffset = 0,
                                        ushort portalStart = 0, ushort portalCount = 0, byte[] fogIds = null,
                                        uint groupLiquid = 0, uint uniqueId = 0, uint flags2 = 0,
                                        short splitParent = 0, short splitNext = 0, ushort batchesD = 0)
        {
            var b = new byte[68];
            PutU32(b, 0x00, (uint)nameOffset);
            PutU32(b, 0x04, (uint)descriptiveNameOffset);
            PutU32(b, 0x08, flags);
            PutVec3(b, 0x0C, boundsMin);
            PutVec3(b, 0x18, boundsMax);
            PutU16(b, 0x24, portalStart);
            PutU16(b, 0x26, portalCount);
            PutU16(b, 0x28, batchesA);
            PutU16(b, 0x2A, batchesB);
            PutU16(b, 0x2C, batchesC);
            PutU16(b, 0x2E, batchesD);
            if (fogIds != null)
                for (int i = 0; i < 4 && i < fogIds.Length; i++)
                    b[0x30 + i] = fogIds[i];
            PutU32(b, 0x34, groupLiquid);
            PutU32(b, 0x38, uniqueId);
            PutU32(b, 0x3C, flags2);
            PutU16(b, 0x40, unchecked((ushort)splitParent));
            PutU16(b, 0x42, unchecked((ushort)splitNext));
            return b;
        }

        /// <summary>
        /// A 24-byte MOBA record whose material id is stored where the format puts it: the u16 at
        /// +0x0A when flags has 0x2, otherwise the u8 at +0x17. `decoyLarge` / `decoySmall` fill the
        /// OTHER field, so a test can prove the parser reads the right one.
        /// </summary>
        public static byte[] Batch(uint startIndex, ushort indexCount, ushort minVertex, ushort maxVertex, byte flags,
                                   int materialId, ushort decoyLarge = 0, byte decoySmall = 0)
        {
            var b = new byte[24];
            bool large = (flags & WmoBatch.FlagLargeMaterialId) != 0;
            PutU16(b, 0x0A, large ? (ushort)materialId : decoyLarge);
            PutU32(b, 0x0C, startIndex);
            PutU16(b, 0x10, indexCount);
            PutU16(b, 0x12, minVertex);
            PutU16(b, 0x14, maxVertex);
            b[0x16] = flags;
            b[0x17] = large ? decoySmall : (byte)materialId;
            return b;
        }

        static void PutVec3(byte[] b, int o, WowVec3 v)
        {
            PutF32(b, o, v.X);
            PutF32(b, o + 4, v.Y);
            PutF32(b, o + 8, v.Z);
        }

        // ------------------------------------------------------------------ whole files

        /// <summary>What BuildRoot writes. Arrays left null are written empty.</summary>
        public sealed class RootSpec
        {
            public byte[][] Materials = new byte[0][];      // Material(...) records
            public byte[][] GroupInfos = new byte[0][];     // GroupInfo(...) records
            public uint[] GroupFileDataIDs = new uint[0];   // all LOD blocks
            public ushort LodCount;                         // MOHD u16 @0x3E
            public ushort Flags;                            // MOHD u16 @0x3C
            public WowVec3 BoundsMin, BoundsMax;
            public uint Ambient, WmoId;
            public string[] GroupNames = new string[0];     // MOGN, packed with a leading NUL
            /// <summary>Extra (tag, payload) chunks written after GFID, e.g. MODS/MODD/MOLT.</summary>
            public List<KeyValuePair<string, byte[]>> ExtraChunks = new List<KeyValuePair<string, byte[]>>();
        }

        public static byte[] BuildRoot(RootSpec s)
        {
            var mogn = new List<byte> { 0 };
            foreach (string name in s.GroupNames)
            {
                foreach (char ch in name) mogn.Add((byte)ch);
                mogn.Add(0);
            }
            var parts = new List<byte[]>
            {
                Mver(),
                Chunk("MOHD", Mohd((uint)s.Materials.Length, (uint)s.GroupInfos.Length, s.Flags, s.LodCount,
                                   s.BoundsMin, s.BoundsMax, ambient: s.Ambient, wmoId: s.WmoId)),
                Chunk("MOMT", Concat(s.Materials)),
                Chunk("MOGN", mogn.ToArray()),
                Chunk("MOGI", Concat(s.GroupInfos)),
                Chunk("GFID", U32s(s.GroupFileDataIDs)),
            };
            foreach (var kv in s.ExtraChunks)
                parts.Add(Chunk(kv.Key, kv.Value));
            return Concat(parts.ToArray());
        }

        /// <summary>What BuildGroup writes. Stream arrays are per vertex; null streams are omitted.</summary>
        public sealed class GroupSpec
        {
            public uint Flags = WmoGroupFlags.Outdoor;
            public WowVec3 BoundsMin, BoundsMax;
            /// <summary>When true the header bounds are computed from Positions.</summary>
            public bool AutoBounds = true;
            public float[] Positions = new float[0];        // x, y, z per vertex
            public float[] Normals;                         // x, y, z per vertex; null = no MONR
            public ushort[] Indices = new ushort[0];
            public List<float[]> TexCoordSets = new List<float[]>();   // u, v per vertex, one list entry per MOTV
            public List<byte[]> ColorSets = new List<byte[]>();       // B, G, R, A per vertex, one entry per MOCV
            public byte[] Moc2;
            public byte[] Mopy;                             // 2 bytes per triangle
            public byte[] Mpy2;                             // 4 bytes per triangle
            public byte[][] Batches = new byte[0][];        // Batch(...) records
            /// <summary>Header A/B/C counts; when null, all batches are counted in C.</summary>
            public ushort[] BatchCounts;
            public int NameOffset, DescriptiveNameOffset;
            public uint UniqueId, GroupLiquid, Flags2;
            public List<KeyValuePair<string, byte[]>> ExtraChunks = new List<KeyValuePair<string, byte[]>>();
        }

        public static byte[] BuildGroup(GroupSpec s)
        {
            WowVec3 mn = s.BoundsMin, mx = s.BoundsMax;
            if (s.AutoBounds && s.Positions.Length >= 3)
            {
                mn = new WowVec3(float.MaxValue, float.MaxValue, float.MaxValue);
                mx = new WowVec3(-float.MaxValue, -float.MaxValue, -float.MaxValue);
                for (int i = 0; i + 2 < s.Positions.Length; i += 3)
                {
                    mn.X = Math.Min(mn.X, s.Positions[i]); mx.X = Math.Max(mx.X, s.Positions[i]);
                    mn.Y = Math.Min(mn.Y, s.Positions[i + 1]); mx.Y = Math.Max(mx.Y, s.Positions[i + 1]);
                    mn.Z = Math.Min(mn.Z, s.Positions[i + 2]); mx.Z = Math.Max(mx.Z, s.Positions[i + 2]);
                }
            }
            ushort[] counts = s.BatchCounts ?? new ushort[] { 0, 0, (ushort)s.Batches.Length };
            var sub = new List<byte[]>();
            sub.Add(MogpHeader(s.Flags, mn, mx, counts[0], counts[1], counts[2], s.NameOffset, s.DescriptiveNameOffset,
                               groupLiquid: s.GroupLiquid, uniqueId: s.UniqueId, flags2: s.Flags2));
            if (s.Mopy != null) sub.Add(Chunk("MOPY", s.Mopy));
            if (s.Mpy2 != null) sub.Add(Chunk("MPY2", s.Mpy2));
            if (s.Positions.Length > 0 || s.Indices.Length > 0)
                sub.Add(Chunk("MOVI", U16s(s.Indices)));
            if (s.Positions.Length > 0)
                sub.Add(Chunk("MOVT", F32s(s.Positions)));
            if (s.Normals != null)
                sub.Add(Chunk("MONR", F32s(s.Normals)));
            if (s.TexCoordSets.Count > 0)
                sub.Add(Chunk("MOTV", F32s(s.TexCoordSets[0])));
            if (s.Batches.Length > 0)
                sub.Add(Chunk("MOBA", Concat(s.Batches)));
            foreach (var kv in s.ExtraChunks)
                sub.Add(Chunk(kv.Key, kv.Value));
            if (s.ColorSets.Count > 0)
                sub.Add(Chunk("MOCV", s.ColorSets[0]));
            for (int i = 1; i < s.TexCoordSets.Count; i++)
                sub.Add(Chunk("MOTV", F32s(s.TexCoordSets[i])));
            for (int i = 1; i < s.ColorSets.Count; i++)
                sub.Add(Chunk("MOCV", s.ColorSets[i]));
            if (s.Moc2 != null)
                sub.Add(Chunk("MOC2", s.Moc2));
            return Concat(Mver(), Chunk("MOGP", Concat(sub.ToArray())));
        }

        // ------------------------------------------------------------------ canned model

        /// <summary>
        /// A small valid WMO for runtime self-tests: `groupCount` groups, each a 10-unit square
        /// (two triangles, WoW Z up) offset along +X by 12 units per group, one batch per group
        /// using material (group index % 2). Two materials: opaque shader 0 with texture1 =
        /// `textureFileDataId`, and a two-sided (flag 0x04) alpha-keyed (blend 1) one with no
        /// texture. GFID uses `firstGroupFileDataId` + i.
        /// </summary>
        public static byte[] SquaresRoot(int groupCount, uint firstGroupFileDataId, uint textureFileDataId)
        {
            var spec = new RootSpec
            {
                Materials = new[]
                {
                    Material(0, 0, 0, textureFileDataId),
                    Material(WmoMaterial.FlagCullDisabled, 0, 1, 0),
                },
                GroupInfos = new byte[groupCount][],
                GroupFileDataIDs = new uint[groupCount],
                GroupNames = new string[groupCount],
                BoundsMin = new WowVec3(0f, 0f, 0f),
                BoundsMax = new WowVec3(12f * (groupCount - 1) + 10f, 10f, 0f),
            };
            int nameOffset = 1;
            for (int i = 0; i < groupCount; i++)
            {
                spec.GroupNames[i] = "square" + i;
                spec.GroupInfos[i] = GroupInfo(WmoGroupFlags.Outdoor, new WowVec3(12f * i, 0f, 0f),
                                               new WowVec3(12f * i + 10f, 10f, 0f), nameOffset);
                nameOffset += spec.GroupNames[i].Length + 1;
                spec.GroupFileDataIDs[i] = firstGroupFileDataId + (uint)i;
            }
            return BuildRoot(spec);
        }

        /// <summary>Group `groupIndex` of SquaresRoot.</summary>
        public static byte[] SquaresGroup(int groupIndex)
        {
            float x = 12f * groupIndex;
            var spec = new GroupSpec
            {
                Positions = new[] { x, 0f, 0f, x + 10f, 0f, 0f, x + 10f, 10f, 0f, x, 10f, 0f },
                Normals = new[] { 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f, 0f, 0f, 1f },
                // counter-clockwise seen from +Z, the format's front-face convention
                Indices = new ushort[] { 0, 1, 2, 0, 2, 3 },
                Batches = new[] { Batch(0, 6, 0, 3, WmoBatch.FlagLargeMaterialId, groupIndex % 2) },
                Mpy2 = new byte[] { 0x20, 0, (byte)(groupIndex % 2), 0, 0x20, 0, (byte)(groupIndex % 2), 0 },
            };
            spec.TexCoordSets.Add(new[] { 0f, 0f, 1f, 0f, 1f, 1f, 0f, 1f });
            return BuildGroup(spec);
        }
    }
}
