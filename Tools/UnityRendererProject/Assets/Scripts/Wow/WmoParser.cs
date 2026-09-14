// WmoParser.cs
//
// Runtime parser for current-client WMO root and group files, from the raw bytes the host serves
// by FileDataID. Pure C#, no UnityEngine: WowParserTests runs it outside the editor.
//
// Rules this parser is built on (all proven against the client bytes, not assumed):
//   - A file is a flat list of chunks: 4-byte tag stored REVERSED ("REVM" for MVER), u32 size,
//     payload. A root is MVER, MOHD, then tables in no fixed order; a group is MVER then ONE
//     MOGP whose payload is a 68-byte header followed by sub-chunks.
//   - Chunks are collected first and decoded afterwards, so nothing depends on chunk order
//     (MODI after MODD is common on retail).
//   - Group files are resolved by GFID FileDataID only. Filename conventions for groups
//     (<root>_NNN.wmo) are wrong for hundreds of retail groups and are not used anywhere.
//   - A group may carry up to 4 MOTV and 2 MOCV streams; all are kept in file order. The role of
//     a lone MOCV comes from the flags: MOCV count == [flag 0x4] + [flag 0x1000000], and a lone
//     MOCV with 0x4 clear is colour set 2.
//   - MOBA material id: (flags & 0x2) ? u16 @0x0A : u8 @0x17.
//
// Safety: every read is checked against the chunk it belongs to (not just the file), and every
// failure is a WmoParseException naming the chunk. After a successful ParseGroup every index a
// batch covers is a valid vertex index, so a builder can index without further checks. Data that
// rendering does not depend on (doodads, lights, fog, portals, MOC2, MOPY, MPY2) never fails a
// parse: a malformed preserved chunk is recorded in Warnings and left undecoded in the chunk list.
// The per-vertex streams a mesh is built from (MOVT, MONR, every MOTV and MOCV) must match the
// vertex count exactly, or the group is rejected.

using System;
using System.Collections.Generic;

namespace Wmv.Wow
{
    public static class WmoParser
    {
        public const uint SupportedVersion = 17;

        // ======================================================================= entry points

        /// <summary>Parse a root WMO. `what` names the asset in error messages.</summary>
        public static WmoRoot ParseRoot(byte[] data, string what = null)
        {
            what = what ?? "wmo root";
            if (data == null)
                throw new WmoParseException(what, "root", "no data");

            var root = new WmoRoot { What = what, Data = data };
            var warnings = new List<string>();
            List<WmoChunkInfo> chunks = WalkChunks(data, 0, data.Length, what, "root");
            root.Chunks = chunks.ToArray();

            // ---- MVER ----
            WmoChunkInfo c;
            if (chunks.Count == 0 || chunks[0].Tag != "MVER")
                throw new WmoParseException(what, "MVER", "the file does not start with an MVER chunk");
            root.Version = ReadVersion(data, chunks[0], what);

            // ---- MOHD ----
            if (!FindSingle(chunks, "MOHD", what, true, out c))
                throw new WmoParseException(what, "MOHD", "missing (a group file or not a WMO root)");
            root.Header = ReadHeader(new Span(data, c, what));
            if (c.Size != 64)
                warnings.Add(string.Format("MOHD: {0} bytes, expected 64; the extra bytes are ignored", c.Size));
            int nMaterials = CheckedCount(root.Header.MaterialCount, "MOHD", "material count", what);
            int nGroups = CheckedCount(root.Header.GroupCount, "MOHD", "group count", what);

            root.HasMotx = HasChunk(chunks, "MOTX");
            root.HasModn = HasChunk(chunks, "MODN");
            if (root.HasMotx)
                warnings.Add("MOTX: historical texture-name table present; material textures are read as FileDataIDs only");
            if (root.HasModn)
                warnings.Add("MODN: historical doodad-name table present; doodad models are read from MODI only");

            // ---- MOMT (complete 64-byte records) ----
            if (FindSingle(chunks, "MOMT", what, true, out c))
            {
                var s = new Span(data, c, what);
                s.RequireRecords(WmoMaterial.RecordSize, "material");
                int n = c.Size / WmoMaterial.RecordSize;
                if (n != nMaterials)
                    throw new WmoParseException(what, "MOMT", string.Format(
                        "{0} material record(s), but MOHD declares {1}", n, nMaterials));
                root.Materials = new WmoMaterial[n];
                for (int i = 0; i < n; i++)
                    root.Materials[i] = ReadMaterial(s, i);
            }
            else if (nMaterials != 0)
                throw new WmoParseException(what, "MOMT", string.Format("missing, but MOHD declares {0} material(s)", nMaterials));

            // ---- MOGN ----
            if (FindSingle(chunks, "MOGN", what, false, out c))
            {
                root.GroupNameBlock = WmoChunks.Copy(data, c);
                var names = new List<WmoGroupName>();
                for (int p = 0; p < root.GroupNameBlock.Length; p++)
                {
                    if (root.GroupNameBlock[p] == 0)
                        continue;
                    string name = root.GetGroupName(p);
                    names.Add(new WmoGroupName { Offset = p, Name = name });
                    p += name.Length;
                }
                root.GroupNames = names.ToArray();
            }
            else if (nGroups != 0)
                warnings.Add("MOGN: missing; groups have no names");

            // ---- MOGI ----
            if (FindSingle(chunks, "MOGI", what, true, out c))
            {
                var s = new Span(data, c, what);
                s.RequireRecords(32, "group info");
                int n = c.Size / 32;
                if (n != nGroups)
                    throw new WmoParseException(what, "MOGI", string.Format(
                        "{0} group info record(s), but MOHD declares {1} group(s)", n, nGroups));
                root.GroupInfos = new WmoGroupInfo[n];
                for (int i = 0; i < n; i++)
                {
                    int o = i * 32;
                    root.GroupInfos[i] = new WmoGroupInfo
                    {
                        Flags = s.U32(o),
                        BoundsMin = s.Vec3(o + 4),
                        BoundsMax = s.Vec3(o + 16),
                        NameOffset = s.I32(o + 28),
                    };
                }
            }
            else if (nGroups != 0)
                throw new WmoParseException(what, "MOGI", string.Format("missing, but MOHD declares {0} group(s)", nGroups));

            // ---- GFID (all LOD blocks) ----
            if (FindSingle(chunks, "GFID", what, true, out c))
            {
                var s = new Span(data, c, what);
                s.RequireRecords(4, "FileDataID");
                root.GroupFileDataIDs = s.U32Array(0, c.Size / 4);
                int n = root.GroupFileDataIDs.Length;
                if (n < nGroups)
                    throw new WmoParseException(what, "GFID", string.Format(
                        "{0} entr(ies), fewer than the {1} group(s) MOHD declares", n, nGroups));
                long expected = (long)nGroups * root.Header.EffectiveLodCount;
                if (n != expected)
                    warnings.Add(string.Format("GFID: {0} entries, expected {1} ({2} group(s) x {3} LOD level(s))",
                                               n, expected, nGroups, root.Header.EffectiveLodCount));
                for (int i = 0; i < nGroups; i++)
                    if (root.GroupFileDataIDs[i] == 0)
                        warnings.Add(string.Format("GFID: full-detail group {0} has FileDataID 0", i));
            }
            else if (nGroups != 0)
                throw new WmoParseException(what, "GFID", string.Format(
                    "missing, but MOHD declares {0} group(s); roots that name their groups by filename are not supported",
                    nGroups));

            // ---- preserved for later stages: never fatal ----
            ReadPreserved(root, data, chunks, what, warnings);

            root.Warnings = warnings.ToArray();
            return root;
        }

        /// <summary>
        /// Parse a group WMO. `groupIndex` is only recorded (GroupIndex) for the caller's
        /// bookkeeping; the file itself does not store it.
        /// </summary>
        public static WmoGroup ParseGroup(byte[] data, string what = null, int groupIndex = -1)
        {
            what = what ?? "wmo group";
            if (data == null)
                throw new WmoParseException(what, "group", "no data");

            var g = new WmoGroup { What = what, Data = data, GroupIndex = groupIndex };
            var warnings = new List<string>();
            List<WmoChunkInfo> top = WalkChunks(data, 0, data.Length, what, "group");
            g.TopChunks = top.ToArray();

            if (top.Count == 0 || top[0].Tag != "MVER")
                throw new WmoParseException(what, "MVER", "the file does not start with an MVER chunk");
            g.Version = ReadVersion(data, top[0], what);

            WmoChunkInfo mogp;
            if (top.Count < 2 || top[1].Tag != "MOGP")
            {
                if (HasChunk(top, "MOHD"))
                    throw new WmoParseException(what, "MOGP", "missing: this is a root WMO, not a group file");
                throw new WmoParseException(what, "MOGP", "missing: MVER is not followed by a MOGP chunk");
            }
            mogp = top[1];
            if (FindAll(top, "MOGP").Count > 1)
                throw new WmoParseException(what, "MOGP", "more than one MOGP chunk in one group file");
            for (int i = 2; i < top.Count; i++)
                warnings.Add(string.Format("group: top-level chunk {0} after MOGP is not decoded", top[i].Tag));

            if (mogp.Size < WmoGroupHeader.Size)
                throw new WmoParseException(what, "MOGP", string.Format(
                    "the chunk holds {0} byte(s), less than its {1}-byte header", mogp.Size, WmoGroupHeader.Size));
            g.Header = ReadGroupHeader(new Span(data, mogp, what));

            int subStart = mogp.DataOffset + WmoGroupHeader.Size;
            List<WmoChunkInfo> sub = WalkChunks(data, subStart, mogp.DataOffset + mogp.Size, what, "MOGP");
            g.Chunks = sub.ToArray();

            WmoChunkInfo c;

            // ---- MOVT ----
            if (FindSingle(sub, "MOVT", what, true, out c))
            {
                var s = new Span(data, c, what);
                s.RequireRecords(12, "position");
                g.Positions = s.Vec3ArrayFinite(0, c.Size / 12, "position");
            }
            int nV = g.Positions.Length;

            // ---- MONR ----
            if (FindSingle(sub, "MONR", what, true, out c))
            {
                var s = new Span(data, c, what);
                s.RequireRecords(12, "normal");
                int n = c.Size / 12;
                if (n != nV)
                    throw new WmoParseException(what, "MONR", string.Format("{0} normal(s) for {1} vertices", n, nV));
                g.Normals = s.Vec3Array(0, n);
            }

            // ---- MOVI ----
            if (FindSingle(sub, "MOVI", what, true, out c))
            {
                var s = new Span(data, c, what);
                s.RequireRecords(2, "index");
                g.Indices = s.U16Array(0, c.Size / 2);
                if (g.Indices.Length % 3 != 0)
                    warnings.Add(string.Format("MOVI: {0} indices is not a whole number of triangles", g.Indices.Length));
            }

            // ---- every MOTV, file order ----
            List<WmoChunkInfo> motv = FindAll(sub, "MOTV");
            g.TexCoordSets = new WowVec2[motv.Count][];
            for (int k = 0; k < motv.Count; k++)
            {
                var s = new Span(data, motv[k], what);
                s.RequireRecords(8, "texture coordinate");
                int n = motv[k].Size / 8;
                if (n != nV)
                    throw new WmoParseException(what, "MOTV", string.Format(
                        "texture coordinate set {0} has {1} entries for {2} vertices", k, n, nV));
                g.TexCoordSets[k] = s.Vec2Array(0, n);
            }

            // ---- every MOCV, file order, with its colour-set number ----
            List<WmoChunkInfo> mocv = FindAll(sub, "MOCV");
            int[] setNumbers = AssignColorSetNumbers(g.Header.Flags, mocv.Count);
            int announced = ((g.Header.Flags & WmoGroupFlags.ColorSet1) != 0 ? 1 : 0) +
                            ((g.Header.Flags & WmoGroupFlags.ColorSet2) != 0 ? 1 : 0);
            if (mocv.Count != announced)
                warnings.Add(string.Format(
                    "MOCV: {0} colour stream(s), but the group flags 0x{1:X8} announce {2}; assigned set(s) {3} " +
                    "(a lone stream is set 2 unless flag 0x4 is set; several are numbered 1, 2, ... in file order)",
                    mocv.Count, g.Header.Flags, announced, string.Join(",", setNumbers)));
            g.ColorSets = new WmoColorSet[mocv.Count];
            for (int k = 0; k < mocv.Count; k++)
            {
                var s = new Span(data, mocv[k], what);
                s.RequireRecords(4, "colour");
                int n = mocv[k].Size / 4;
                if (n != nV)
                    throw new WmoParseException(what, "MOCV", string.Format(
                        "colour stream {0} has {1} entries for {2} vertices", k, n, nV));
                g.ColorSets[k] = new WmoColorSet { SetNumber = setNumbers[k], FileOrder = k, Bgra = WmoChunks.Copy(data, mocv[k]) };
            }

            // ---- MOC2, MOPY, MPY2: kept for later stages, not needed to draw, so never fatal ----
            if (Preserved(sub, "MOC2", 4, data, what, warnings, out c))
            {
                if (c.Size / 4 != nV)
                    warnings.Add(string.Format("MOC2: {0} entries for {1} vertices", c.Size / 4, nV));
                g.Moc2 = WmoChunks.Copy(data, c);
            }

            int nTriangles = g.Indices.Length / 3;
            g.HasMopy = HasChunk(sub, "MOPY");
            g.HasMpy2 = HasChunk(sub, "MPY2");
            if (Preserved(sub, "MOPY", 2, data, what, warnings, out c))
            {
                var s = new Span(data, c, what);
                int n = c.Size / 2;
                g.PolyMaterials = new WmoPolyMaterial[n];
                for (int i = 0; i < n; i++)
                    g.PolyMaterials[i] = new WmoPolyMaterial { Flags = s.U8(i * 2), MaterialId = s.U8(i * 2 + 1) };
                if (n != nTriangles)
                    warnings.Add(string.Format("MOPY: {0} records for {1} triangles", n, nTriangles));
            }
            if (Preserved(sub, "MPY2", 4, data, what, warnings, out c))
            {
                var s = new Span(data, c, what);
                int n = c.Size / 4;
                g.PolyMaterials2 = new WmoPolyMaterial2[n];
                for (int i = 0; i < n; i++)
                    g.PolyMaterials2[i] = new WmoPolyMaterial2 { Flags = s.U16(i * 4), MaterialId = s.U16(i * 4 + 2) };
                if (n != nTriangles)
                    warnings.Add(string.Format("MPY2: {0} records for {1} triangles", n, nTriangles));
            }

            // ---- MOBA ----
            if (FindSingle(sub, "MOBA", what, true, out c))
            {
                var s = new Span(data, c, what);
                s.RequireRecords(WmoBatch.RecordSize, "batch");
                int n = c.Size / WmoBatch.RecordSize;
                g.Batches = new WmoBatch[n];
                for (int i = 0; i < n; i++)
                    g.Batches[i] = ReadBatch(s, i);
            }
            if (g.Batches.Length != g.Header.TotalBatchCount)
                warnings.Add(string.Format("MOBA: {0} batches, but the MOGP header counts {1} + {2} + {3}",
                                           g.Batches.Length, g.Header.BatchCountA, g.Header.BatchCountB, g.Header.BatchCountC));
            ValidateBatchRanges(g, what, warnings);

            g.Warnings = warnings.ToArray();
            return g;
        }

        /// <summary>
        /// Colour-set number (1 or 2) for each MOCV chunk of a group, in file order. Proven on every
        /// retail group: the MOCV count equals [flag 0x4] + [flag 0x1000000]; with both, the first is
        /// set 1 and the second set 2; a lone MOCV is set 1 when flag 0x4 is set and set 2 otherwise.
        /// A file with more MOCV chunks than that keeps them, numbered on from its file order.
        /// </summary>
        public static int[] AssignColorSetNumbers(uint groupFlags, int mocvCount)
        {
            var r = new int[Math.Max(0, mocvCount)];
            if (r.Length == 0)
                return r;
            if (r.Length == 1)
            {
                r[0] = (groupFlags & WmoGroupFlags.ColorSet1) != 0 ? 1 : 2;
                return r;
            }
            for (int i = 0; i < r.Length; i++)
                r[i] = i + 1;
            return r;
        }

        // ============================================================================ chunking

        /// <summary>Walk the chunks in [start, end). A header or payload that does not fit is fatal.</summary>
        static List<WmoChunkInfo> WalkChunks(byte[] data, int start, int end, string what, string context)
        {
            var list = new List<WmoChunkInfo>();
            int p = start;
            while (p < end)
            {
                if (end - p < 8)
                    throw new WmoParseException(what, context, string.Format(
                        "{0} trailing byte(s) at offset 0x{1:X} cannot hold a chunk header", end - p, p));
                string tag = new string(new[] { (char)data[p + 3], (char)data[p + 2], (char)data[p + 1], (char)data[p] });
                uint size = (uint)(data[p + 4] | (data[p + 5] << 8) | (data[p + 6] << 16) | (data[p + 7] << 24));
                int payload = p + 8;
                if (size > (uint)(end - payload))
                    throw new WmoParseException(what, PrintableTag(tag), string.Format(
                        "chunk at offset 0x{0:X} claims {1} byte(s), but only {2} remain in the {3}",
                        p, size, end - payload, context == "MOGP" ? "MOGP chunk" : "file"));
                list.Add(new WmoChunkInfo { Tag = tag, HeaderOffset = p, DataOffset = payload, Size = (int)size });
                p = payload + (int)size;
            }
            return list;
        }

        static string PrintableTag(string tag)
        {
            foreach (char ch in tag)
                if (ch < 0x20 || ch > 0x7E)
                    return "chunk";
            return tag;
        }

        static bool HasChunk(List<WmoChunkInfo> chunks, string tag)
        {
            for (int i = 0; i < chunks.Count; i++)
                if (chunks[i].Tag == tag)
                    return true;
            return false;
        }

        static List<WmoChunkInfo> FindAll(List<WmoChunkInfo> chunks, string tag)
        {
            var r = new List<WmoChunkInfo>();
            for (int i = 0; i < chunks.Count; i++)
                if (chunks[i].Tag == tag)
                    r.Add(chunks[i]);
            return r;
        }

        /// <summary>The chunk with this tag. A second copy is fatal when `unique` (two MOVT or two MOBA
        /// would make the geometry ambiguous); otherwise the first copy is used.</summary>
        static bool FindSingle(List<WmoChunkInfo> chunks, string tag, string what, bool unique, out WmoChunkInfo chunk)
        {
            chunk = default(WmoChunkInfo);
            bool found = false;
            for (int i = 0; i < chunks.Count; i++)
            {
                if (chunks[i].Tag != tag)
                    continue;
                if (found)
                {
                    if (unique)
                        throw new WmoParseException(what, tag, "more than one " + tag + " chunk");
                    break;
                }
                chunk = chunks[i];
                found = true;
            }
            return found;
        }

        static int CheckedCount(uint value, string chunk, string name, string what)
        {
            // A count this large cannot be backed by any real file; refusing it early keeps later
            // arithmetic (count x record size) far from overflow.
            if (value > 0x00FFFFFF)
                throw new WmoParseException(what, chunk, string.Format("implausible {0} {1}", name, value));
            return (int)value;
        }

        static uint ReadVersion(byte[] data, WmoChunkInfo c, string what)
        {
            var s = new Span(data, c, what);
            uint v = s.U32(0);
            if (v != SupportedVersion)
                throw new WmoParseException(what, "MVER", string.Format(
                    "version {0} is not supported (current-client WMOs are version {1})", v, SupportedVersion));
            return v;
        }

        // ============================================================================= records

        static WmoHeader ReadHeader(Span s)
        {
            s.Require(0, 64);
            return new WmoHeader
            {
                MaterialCount = s.U32(0x00),
                GroupCount = s.U32(0x04),
                PortalCount = s.U32(0x08),
                LightCount = s.U32(0x0C),
                DoodadNameCount = s.U32(0x10),
                DoodadDefCount = s.U32(0x14),
                DoodadSetCount = s.U32(0x18),
                AmbientColor = s.U32(0x1C),
                WmoId = s.U32(0x20),
                BoundsMin = s.Vec3(0x24),
                BoundsMax = s.Vec3(0x30),
                Flags = s.U16(0x3C),
                LodCountRaw = s.U16(0x3E),
            };
        }

        static WmoMaterial ReadMaterial(Span s, int i)
        {
            int o = i * WmoMaterial.RecordSize;
            var m = new WmoMaterial
            {
                Index = i,
                Raw = s.Bytes(o, WmoMaterial.RecordSize),
                Flags = s.U32(o + 0x00),
                Shader = s.U32(o + 0x04),
                BlendMode = s.U32(o + 0x08),
                Texture1 = s.U32(o + 0x0C),
                SidnColor = s.U32(o + 0x10),
                FrameSidnColor = s.U32(o + 0x14),
                Texture2 = s.U32(o + 0x18),
                DiffColor = s.U32(o + 0x1C),
                GroundType = s.U32(o + 0x20),
                Texture3 = s.U32(o + 0x24),
                Color3 = s.U32(o + 0x28),
                Flags2 = s.U32(o + 0x2C),
                RuntimeData = s.U32Array(o + 0x30, 4),
                Tail = s.U32Array(o + 0x28, 6),
            };
            return m;
        }

        static WmoGroupHeader ReadGroupHeader(Span s)
        {
            s.Require(0, WmoGroupHeader.Size);
            return new WmoGroupHeader
            {
                Raw = s.Bytes(0, WmoGroupHeader.Size),
                GroupNameOffset = s.I32(0x00),
                DescriptiveNameOffset = s.I32(0x04),
                Flags = s.U32(0x08),
                BoundsMin = s.Vec3(0x0C),
                BoundsMax = s.Vec3(0x18),
                PortalStart = s.U16(0x24),
                PortalCount = s.U16(0x26),
                BatchCountA = s.U16(0x28),
                BatchCountB = s.U16(0x2A),
                BatchCountC = s.U16(0x2C),
                BatchCountD = s.U16(0x2E),
                FogId0 = s.U8(0x30),
                FogId1 = s.U8(0x31),
                FogId2 = s.U8(0x32),
                FogId3 = s.U8(0x33),
                GroupLiquid = s.U32(0x34),
                UniqueId = s.U32(0x38),
                Flags2 = s.U32(0x3C),
                SplitGroupParent = (short)s.U16(0x40),
                SplitGroupNext = (short)s.U16(0x42),
            };
        }

        static WmoBatch ReadBatch(Span s, int i)
        {
            int o = i * WmoBatch.RecordSize;
            var b = new WmoBatch
            {
                Raw = s.Bytes(o, WmoBatch.RecordSize),
                BoxMinX = (short)s.U16(o + 0x00),
                BoxMinY = (short)s.U16(o + 0x02),
                BoxMinZ = (short)s.U16(o + 0x04),
                BoxMaxX = (short)s.U16(o + 0x06),
                BoxMaxY = (short)s.U16(o + 0x08),
                MaterialIdLarge = s.U16(o + 0x0A),
                StartIndex = s.U32(o + 0x0C),
                IndexCount = s.U16(o + 0x10),
                MinVertex = s.U16(o + 0x12),
                MaxVertex = s.U16(o + 0x14),
                Flags = s.U8(o + 0x16),
                MaterialIdSmall = s.U8(o + 0x17),
            };
            b.MaterialId = WmoBatch.ResolveMaterialId(b.Flags, b.MaterialIdLarge, b.MaterialIdSmall);
            return b;
        }

        /// <summary>
        /// Every batch range must lie inside MOVI and every index it covers must name a vertex:
        /// those are the triangles a builder draws, so a bad one is fatal. Indices outside all
        /// batches are collision-only; a bad one there is only a warning.
        /// </summary>
        static void ValidateBatchRanges(WmoGroup g, string what, List<string> warnings)
        {
            int nIdx = g.Indices.Length, nV = g.Positions.Length;
            var covered = g.Batches.Length > 0 ? new bool[nIdx] : null;
            for (int b = 0; b < g.Batches.Length; b++)
            {
                WmoBatch batch = g.Batches[b];
                long end = (long)batch.StartIndex + batch.IndexCount;
                if (end > nIdx)
                    throw new WmoParseException(what, "MOBA", string.Format(
                        "batch {0} covers indices [{1}, {2}), outside the {3} MOVI indices",
                        b, batch.StartIndex, end, nIdx));
                if (batch.IndexCount % 3 != 0)
                    warnings.Add(string.Format("MOBA: batch {0} has {1} indices, not a whole number of triangles; the remainder is not drawn",
                                               b, batch.IndexCount));
                int start = (int)batch.StartIndex;
                for (int i = start; i < (int)end; i++)
                {
                    if (g.Indices[i] >= nV)
                        throw new WmoParseException(what, "MOVI", string.Format(
                            "index {0} (used by batch {1}) names vertex {2}, but the group has {3} vertices",
                            i, b, g.Indices[i], nV));
                    covered[i] = true;
                }
            }
            int stray = 0;
            for (int i = 0; i < nIdx; i++)
                if (g.Indices[i] >= nV && (covered == null || !covered[i]))
                    stray++;
            if (stray > 0)
                warnings.Add(string.Format("MOVI: {0} index(es) outside every batch name a vertex past {1}; they are not drawn",
                                           stray, nV));
        }

        // ================================================================ preserved root chunks

        static void ReadPreserved(WmoRoot root, byte[] data, List<WmoChunkInfo> chunks, string what, List<string> warnings)
        {
            WmoChunkInfo c;

            if (Preserved(chunks, "MODS", 32, data, what, warnings, out c))
            {
                var s = new Span(data, c, what);
                int n = c.Size / 32;
                root.DoodadSets = new WmoDoodadSet[n];
                for (int i = 0; i < n; i++)
                {
                    int o = i * 32;
                    root.DoodadSets[i] = new WmoDoodadSet
                    {
                        Name = s.FixedString(o, 20),
                        StartIndex = s.U32(o + 20),
                        Count = s.U32(o + 24),
                        Unused = s.U32(o + 28),
                    };
                }
            }

            if (Preserved(chunks, "MODI", 4, data, what, warnings, out c))
                root.DoodadFileDataIDs = new Span(data, c, what).U32Array(0, c.Size / 4);

            if (Preserved(chunks, "MODD", 40, data, what, warnings, out c))
            {
                var s = new Span(data, c, what);
                int n = c.Size / 40;
                root.DoodadDefs = new WmoDoodadDef[n];
                for (int i = 0; i < n; i++)
                {
                    int o = i * 40;
                    uint word = s.U32(o);
                    root.DoodadDefs[i] = new WmoDoodadDef
                    {
                        NameIndexAndFlags = word,
                        NameIndex = word & 0x00FFFFFF,
                        Flags = (byte)(word >> 24),
                        Position = s.Vec3(o + 4),
                        Rotation = new WowQuat(s.F32(o + 16), s.F32(o + 20), s.F32(o + 24), s.F32(o + 28)),
                        Scale = s.F32(o + 32),
                        Color = s.U32(o + 36),
                    };
                }
            }

            if (Preserved(chunks, "MOLT", 48, data, what, warnings, out c))
                root.Lights = new WmoRawRecords { Tag = "MOLT", RecordSize = 48, Count = c.Size / 48, Bytes = WmoChunks.Copy(data, c) };
            if (Preserved(chunks, "MFOG", 48, data, what, warnings, out c))
                root.Fogs = new WmoRawRecords { Tag = "MFOG", RecordSize = 48, Count = c.Size / 48, Bytes = WmoChunks.Copy(data, c) };

            if (Preserved(chunks, "MOPV", 12, data, what, warnings, out c))
                root.PortalVertices = new Span(data, c, what).Vec3Array(0, c.Size / 12);

            if (Preserved(chunks, "MOPT", 20, data, what, warnings, out c))
            {
                var s = new Span(data, c, what);
                int n = c.Size / 20;
                root.Portals = new WmoPortalInfo[n];
                for (int i = 0; i < n; i++)
                {
                    int o = i * 20;
                    root.Portals[i] = new WmoPortalInfo
                    {
                        StartVertex = s.U16(o),
                        VertexCount = s.U16(o + 2),
                        PlaneNormal = s.Vec3(o + 4),
                        PlaneDistance = s.F32(o + 16),
                    };
                }
            }

            if (Preserved(chunks, "MOPR", 8, data, what, warnings, out c))
            {
                var s = new Span(data, c, what);
                int n = c.Size / 8;
                root.PortalRefs = new WmoPortalRef[n];
                for (int i = 0; i < n; i++)
                {
                    int o = i * 8;
                    root.PortalRefs[i] = new WmoPortalRef
                    {
                        PortalIndex = s.U16(o),
                        GroupIndex = s.U16(o + 2),
                        Side = (short)s.U16(o + 4),
                        Filler = s.U16(o + 6),
                    };
                }
            }
        }

        /// <summary>First chunk with this tag when its size is a whole number of records; otherwise a
        /// warning and false (the chunk stays in the chunk list, undecoded).</summary>
        static bool Preserved(List<WmoChunkInfo> chunks, string tag, int recordSize, byte[] data, string what,
                              List<string> warnings, out WmoChunkInfo chunk)
        {
            if (!FindSingle(chunks, tag, what, false, out chunk))
                return false;
            if (FindAll(chunks, tag).Count > 1)
                warnings.Add(string.Format("{0}: more than one chunk; only the first is decoded", tag));
            if (chunk.Size % recordSize != 0)
            {
                warnings.Add(string.Format("{0}: {1} bytes is not a whole number of {2}-byte records; left undecoded",
                                           tag, chunk.Size, recordSize));
                return false;
            }
            return true;
        }

        // ============================================================ bounds-checked chunk view

        /// <summary>
        /// A read-only window onto one chunk's payload. Offsets are relative to the payload and
        /// checked against the CHUNK size, so a malformed record can never read a neighbouring
        /// chunk even though those bytes are inside the file.
        /// </summary>
        struct Span
        {
            readonly byte[] d;
            readonly int start;
            readonly int size;
            readonly string what;
            readonly string tag;

            public Span(byte[] data, WmoChunkInfo c, string what)
            {
                d = data;
                start = c.DataOffset;
                size = c.Size;
                this.what = what;
                tag = c.Tag;
                // The chunk walk already proved this; re-checking keeps a hand-made WmoChunkInfo safe.
                if (data == null || start < 0 || size < 0 || start > data.Length || size > data.Length - start)
                    throw new WmoParseException(what, tag, "chunk extends past the end of the file");
            }

            public void Require(int offset, int count)
            {
                if (offset < 0 || count < 0 || offset > size || count > size - offset)
                    throw new WmoParseException(what, tag, string.Format(
                        "read of {0} byte(s) at +0x{1:X} is outside the {2}-byte chunk", count, offset, size));
            }

            public void RequireRecords(int recordSize, string name)
            {
                if (size % recordSize != 0)
                    throw new WmoParseException(what, tag, string.Format(
                        "{0} byte(s) is not a whole number of {1}-byte {2} records", size, recordSize, name));
            }

            public byte U8(int o) { Require(o, 1); return d[start + o]; }

            public ushort U16(int o)
            {
                Require(o, 2);
                int p = start + o;
                return (ushort)(d[p] | (d[p + 1] << 8));
            }

            public uint U32(int o)
            {
                Require(o, 4);
                int p = start + o;
                return (uint)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (d[p + 3] << 24));
            }

            public int I32(int o) { return (int)U32(o); }

            /// <summary>A float as stored, without a finiteness check (for data that is not rendered).</summary>
            public float F32(int o)
            {
                Require(o, 4);
                return BitConverter.ToSingle(d, start + o);
            }

            public WowVec3 Vec3(int o)
            {
                Require(o, 12);
                int p = start + o;
                return new WowVec3(BitConverter.ToSingle(d, p), BitConverter.ToSingle(d, p + 4), BitConverter.ToSingle(d, p + 8));
            }

            public byte[] Bytes(int o, int n)
            {
                Require(o, n);
                var r = new byte[n];
                Buffer.BlockCopy(d, start + o, r, 0, n);
                return r;
            }

            public string FixedString(int o, int n)
            {
                Require(o, n);
                int len = 0;
                while (len < n && d[start + o + len] != 0)
                    len++;
                var chars = new char[len];
                for (int i = 0; i < len; i++)
                    chars[i] = (char)d[start + o + i];
                return new string(chars);
            }

            // Bulk readers: one range check for the whole array, then no per-element checks or
            // allocations -- a large group has hundreds of thousands of floats. BitConverter reads
            // floats in host byte order; every platform the player is built for is little-endian,
            // like the files.

            public ushort[] U16Array(int o, int count)
            {
                RequireArray(o, count, 2);
                var r = new ushort[count];
                int p = start + o;
                for (int i = 0; i < count; i++, p += 2)
                    r[i] = (ushort)(d[p] | (d[p + 1] << 8));
                return r;
            }

            public uint[] U32Array(int o, int count)
            {
                RequireArray(o, count, 4);
                var r = new uint[count];
                int p = start + o;
                for (int i = 0; i < count; i++, p += 4)
                    r[i] = (uint)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (d[p + 3] << 24));
                return r;
            }

            public WowVec3[] Vec3Array(int o, int count)
            {
                RequireArray(o, count, 12);
                var r = new WowVec3[count];
                int p = start + o;
                for (int i = 0; i < count; i++, p += 12)
                    r[i] = new WowVec3(BitConverter.ToSingle(d, p), BitConverter.ToSingle(d, p + 4), BitConverter.ToSingle(d, p + 8));
                return r;
            }

            /// <summary>Like Vec3Array, but a NaN or infinite component is fatal: one bad position
            /// would poison the group bounds and with them the camera framing.</summary>
            public WowVec3[] Vec3ArrayFinite(int o, int count, string name)
            {
                WowVec3[] r = Vec3Array(o, count);
                for (int i = 0; i < r.Length; i++)
                    if (!IsFinite(r[i].X) || !IsFinite(r[i].Y) || !IsFinite(r[i].Z))
                        throw new WmoParseException(what, tag, string.Format("{0} {1} is not finite {2}", name, i, r[i]));
                return r;
            }

            public WowVec2[] Vec2Array(int o, int count)
            {
                RequireArray(o, count, 8);
                var r = new WowVec2[count];
                int p = start + o;
                for (int i = 0; i < count; i++, p += 8)
                    r[i] = new WowVec2(BitConverter.ToSingle(d, p), BitConverter.ToSingle(d, p + 4));
                return r;
            }

            void RequireArray(int o, int count, int elementSize)
            {
                if (count < 0 || (long)count * elementSize > int.MaxValue)
                    throw new WmoParseException(what, tag, string.Format("implausible element count {0}", count));
                Require(o, count * elementSize);
            }

            static bool IsFinite(float f)
            {
                return !float.IsNaN(f) && !float.IsInfinity(f);
            }
        }
    }
}
