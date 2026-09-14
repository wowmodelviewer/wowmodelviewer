// WmoModel.cs
//
// Plain result types for the runtime WMO (world map object) parser. No UnityEngine types: the
// parsing layer stays testable outside the editor, and the Unity-side builder converts these
// into GameObjects / Meshes / Materials.
//
// A current-client WMO is one ROOT file (shared tables: materials, group info, doodads, lights,
// fog, portals, and the GFID list of group FileDataIDs) plus one GROUP file per group (MVER then
// a single MOGP chunk holding that group's geometry). Everything here mirrors the bytes: field
// meanings are only named where the client data proves them; everything else is kept raw, so a
// later material / lighting stage can decode it without re-reading the files.
//
// Offsets in WmoChunkInfo index into the byte[] the model was parsed from (WmoRoot.Data /
// WmoGroup.Data). The model keeps that array alive; drop the model to release it.

namespace Wmv.Wow
{
    /// <summary>A malformed WMO. Chunk names the four-character chunk the problem was found in
    /// (as read, e.g. "MOBA"), or "root" / "group" for the file-level structure.</summary>
    public class WmoParseException : WowParseException
    {
        public readonly string Chunk;

        public WmoParseException(string what, string chunk, string detail)
            : base(string.Format("{0}: {1}: {2}", what ?? "wmo", chunk ?? "?", detail))
        {
            Chunk = chunk;
        }
    }

    /// <summary>
    /// One chunk as found in a file: its tag in reading order ("MVER", although the file stores
    /// the four bytes reversed), where its 8-byte header starts, where its payload starts, and
    /// the payload size. Every chunk is listed, decoded or not, so nothing is silently dropped.
    /// </summary>
    public struct WmoChunkInfo
    {
        public string Tag;
        public int HeaderOffset;
        public int DataOffset;
        public int Size;

        public override string ToString()
        {
            return string.Format("{0}@0x{1:X}+{2}", Tag, DataOffset, Size);
        }
    }

    // ================================================================================== root

    /// <summary>MOHD, 64 bytes. Counts are as stored; the chunk sizes are authoritative for the
    /// arrays that have their own chunk (the parser checks the ones rendering depends on).</summary>
    public struct WmoHeader
    {
        public uint MaterialCount;      // +0x00, == MOMT size / 64
        public uint GroupCount;         // +0x04, == MOGI size / 32
        public uint PortalCount;        // +0x08, == MOPT size / 20
        public uint LightCount;         // +0x0C
        public uint DoodadNameCount;    // +0x10, number of NON-ZERO MODI entries on retail
        public uint DoodadDefCount;     // +0x14, can exceed the MODD record count
        public uint DoodadSetCount;     // +0x18, == MODS size / 32
        public uint AmbientColor;       // +0x1C, BGRA as a little-endian u32 (meaning not settled)
        public uint WmoId;              // +0x20
        public WowVec3 BoundsMin;       // +0x24, WoW model space (Z up), unconverted
        public WowVec3 BoundsMax;       // +0x30
        /// <summary>+0x3C u16. Bit 0x4 selects the liquid-type source, 0x8 changes how vertex
        /// colours are stored; neither is interpreted by this stage.</summary>
        public ushort Flags;
        /// <summary>+0x3E u16 as stored: the number of GFID blocks (0 means one block).</summary>
        public ushort LodCountRaw;

        /// <summary>GFID holds GroupCount x EffectiveLodCount entries on every retail root.</summary>
        public int EffectiveLodCount { get { return LodCountRaw == 0 ? 1 : LodCountRaw; } }
    }

    /// <summary>
    /// MOMT, one 64-byte material record. The COMPLETE record is kept (Raw) and every u32 is also
    /// decoded. Field names follow the usual labels for these offsets; only the texture slots and
    /// their FileDataID nature are proven by client bytes -- what the shader id, blend value,
    /// colours and flags DO is not settled and must not be inferred from the names.
    ///
    /// Texture slots: +0x0C, +0x18 and +0x24 hold texture FileDataIDs for every shader id. For
    /// shader ids 22 and 23 the whole tail +0x28..+0x3C holds further texture FileDataIDs (up to
    /// nine textures on one material); for ids 0..21 the tail is zero except the +0x28 value
    /// 0x3D4CCCCD (0.05f) seen on some shader-0 materials, which is not a FileDataID.
    /// </summary>
    public struct WmoMaterial
    {
        public const int RecordSize = 64;

        /// <summary>Byte offsets of the nine u32 slots that can hold a texture FileDataID.</summary>
        public static readonly int[] TextureSlotOffsets = { 0x0C, 0x18, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C };

        /// <summary>First slot index (into TextureSlotOffsets) of the tail.</summary>
        public const int FirstTailSlot = 3;

        /// <summary>Material flag the archived viewport used to disable back-face culling.</summary>
        public const uint FlagCullDisabled = 0x04;

        public int Index;               // position in MOMT
        public byte[] Raw;              // the 64 bytes as stored

        public uint Flags;              // +0x00
        public uint Shader;             // +0x04, 0..23 on retail
        public uint BlendMode;          // +0x08, 0,1,2,3,5,6 on retail
        public uint Texture1;           // +0x0C, texture FileDataID (0 = none)
        public uint SidnColor;          // +0x10, BGRA u32
        public uint FrameSidnColor;     // +0x14, zero on retail
        public uint Texture2;           // +0x18, texture FileDataID
        public uint DiffColor;          // +0x1C, BGRA u32
        public uint GroundType;         // +0x20
        public uint Texture3;           // +0x24, texture FileDataID
        public uint Color3;             // +0x28, texture FileDataID for shaders 22/23
        public uint Flags2;             // +0x2C, texture FileDataID for shaders 22/23
        public uint[] RuntimeData;      // +0x30, +0x34, +0x38, +0x3C (4 values); texture FileDataIDs for 22/23

        /// <summary>The six u32 of the tail +0x28..+0x3C, in offset order (the same bytes as
        /// Color3, Flags2 and RuntimeData).</summary>
        public uint[] Tail;

        /// <summary>Raw u32 of texture slot 0..8 (see TextureSlotOffsets).</summary>
        public uint GetSlot(int slot)
        {
            if (slot < 0 || slot >= TextureSlotOffsets.Length || Raw == null || Raw.Length < RecordSize)
                return 0;
            int o = TextureSlotOffsets[slot];
            return (uint)(Raw[o] | (Raw[o + 1] << 8) | (Raw[o + 2] << 16) | (Raw[o + 3] << 24));
        }

        /// <summary>
        /// Whether the tail slots are texture references. True for shader ids 22 and 23 (proven by
        /// bytes) and, conservatively, for any id above 23 (none exists on retail): fetching an id
        /// that is not a file only costs a "missing texture" line, while ignoring a real one would
        /// silently lose a texture. False for 0..21, whose tail is not texture data.
        /// </summary>
        public bool TailHoldsTextures { get { return Shader >= 22; } }

        /// <summary>Whether texture slot 0..8 is a texture reference for this material's shader id.</summary>
        public bool IsTextureSlot(int slot)
        {
            if (slot < 0 || slot >= TextureSlotOffsets.Length)
                return false;
            return slot < FirstTailSlot || TailHoldsTextures;
        }

        /// <summary>
        /// Every non-zero texture FileDataID this material references, in slot order, without
        /// duplicates (shader-23 materials often repeat one texture in two slots).
        /// </summary>
        public uint[] GetTextureFileDataIDs()
        {
            var ids = new System.Collections.Generic.List<uint>(TextureSlotOffsets.Length);
            for (int s = 0; s < TextureSlotOffsets.Length; s++)
            {
                if (!IsTextureSlot(s))
                    continue;
                uint v = GetSlot(s);
                if (v != 0 && !ids.Contains(v))
                    ids.Add(v);
            }
            return ids.ToArray();
        }
    }

    /// <summary>MOGI, 32 bytes per group. GFID[i] is the file of group i (identical bounding
    /// boxes on every checked retail group). Chunk-presence flag bits live only in the group
    /// file's MOGP copy of the flags: use WmoGroupHeader.Flags for anything about contents.</summary>
    public struct WmoGroupInfo
    {
        public uint Flags;              // +0x00
        public WowVec3 BoundsMin;       // +0x04
        public WowVec3 BoundsMax;       // +0x10
        public int NameOffset;          // +0x1C, into MOGN; -1 when unnamed
    }

    /// <summary>One name in MOGN and the offset groups use to refer to it (MOGP name offsets,
    /// MOGI NameOffset).</summary>
    public struct WmoGroupName
    {
        public int Offset;
        public string Name;
    }

    /// <summary>MODS, 32 bytes. Set 0 is the default global set on every retail root; the set
    /// ranges partition MODD. Parsed for later stages; doodads are not rendered yet.</summary>
    public struct WmoDoodadSet
    {
        public string Name;             // +0x00, 20 bytes, NUL-terminated
        public uint StartIndex;         // +0x14, first MODD record
        public uint Count;              // +0x18
        public uint Unused;             // +0x1C
    }

    /// <summary>MODD, 40 bytes. Parsed for later stages; doodads are not rendered yet.</summary>
    public struct WmoDoodadDef
    {
        public uint NameIndexAndFlags;  // +0x00 as stored
        public uint NameIndex;          // low 24 bits: index into MODI (the doodad model FileDataID)
        public byte Flags;              // high 8 bits (meaning not settled)
        public WowVec3 Position;        // +0x04, WoW model space, unconverted
        public WowQuat Rotation;        // +0x10, stored x, y, z, w (proven order)
        public float Scale;             // +0x20
        public uint Color;              // +0x24, BGRA u32 (top byte meaning not settled)
    }

    /// <summary>MOPT, 20 bytes. Parsed for later stages; portals are not used yet.</summary>
    public struct WmoPortalInfo
    {
        public ushort StartVertex;      // +0x00, into MOPV
        public ushort VertexCount;      // +0x02, 4 for most portals but not all
        public WowVec3 PlaneNormal;     // +0x04, as stored (not validated)
        public float PlaneDistance;     // +0x10
    }

    /// <summary>MOPR, 8 bytes. Parsed for later stages.</summary>
    public struct WmoPortalRef
    {
        public ushort PortalIndex;      // +0x00
        public ushort GroupIndex;       // +0x02
        public short Side;              // +0x04, -1 or +1
        public ushort Filler;           // +0x06
    }

    /// <summary>A chunk of fixed-size records kept as raw bytes because its field meanings are
    /// not settled (MOLT lights, MFOG fog). Count is 0 when the chunk is absent.</summary>
    public sealed class WmoRawRecords
    {
        public string Tag;
        public int RecordSize;
        public int Count;
        public byte[] Bytes = new byte[0];   // Count x RecordSize bytes, copied from the chunk

        public byte[] GetRecord(int index)
        {
            if (index < 0 || index >= Count)
                return null;
            var r = new byte[RecordSize];
            System.Buffer.BlockCopy(Bytes, index * RecordSize, r, 0, RecordSize);
            return r;
        }
    }

    /// <summary>A parsed root WMO file.</summary>
    public sealed class WmoRoot
    {
        public string What = "";
        /// <summary>The bytes this was parsed from; every WmoChunkInfo offset indexes into it.</summary>
        public byte[] Data;

        public uint Version;                                        // MVER, 17
        public WmoHeader Header;                                    // MOHD
        public WmoMaterial[] Materials = new WmoMaterial[0];        // MOMT
        public byte[] GroupNameBlock = new byte[0];                 // MOGN, copied as stored
        /// <summary>Every non-empty NUL-terminated string in MOGN with its offset, in block order.</summary>
        public WmoGroupName[] GroupNames = new WmoGroupName[0];
        public WmoGroupInfo[] GroupInfos = new WmoGroupInfo[0];     // MOGI

        /// <summary>GFID, every entry: GroupCount entries per LOD level, LOD0 first. Later
        /// blocks are reduced-detail copies of the same groups; zero entries occur only there.</summary>
        public uint[] GroupFileDataIDs = new uint[0];

        public WmoDoodadSet[] DoodadSets = new WmoDoodadSet[0];     // MODS
        public uint[] DoodadFileDataIDs = new uint[0];              // MODI
        public WmoDoodadDef[] DoodadDefs = new WmoDoodadDef[0];     // MODD
        public WmoRawRecords Lights = new WmoRawRecords { Tag = "MOLT", RecordSize = 48 };
        public WmoRawRecords Fogs = new WmoRawRecords { Tag = "MFOG", RecordSize = 48 };
        public WowVec3[] PortalVertices = new WowVec3[0];           // MOPV
        public WmoPortalInfo[] Portals = new WmoPortalInfo[0];      // MOPT
        public WmoPortalRef[] PortalRefs = new WmoPortalRef[0];     // MOPR

        /// <summary>Historical name tables. Absent from every current-client root; recorded only
        /// so a caller can explain why such a file is not supported. Never used for lookups.</summary>
        public bool HasMotx, HasModn;

        /// <summary>Every top-level chunk in file order, decoded or not.</summary>
        public WmoChunkInfo[] Chunks = new WmoChunkInfo[0];

        /// <summary>Non-fatal oddities (a count that disagrees with its chunk size, a preserved
        /// chunk with a partial record, ...). Empty for every checked retail root.</summary>
        public string[] Warnings = new string[0];

        public int GroupCount { get { return (int)Header.GroupCount; } }
        public int LodCount { get { return Header.EffectiveLodCount; } }

        /// <summary>
        /// The full-detail group FileDataIDs, GFID[0 .. GroupCount-1]: the only groups to load
        /// for a normal view. Loading later blocks as well would stack duplicate geometry.
        /// </summary>
        public uint[] Lod0GroupFileDataIDs { get { return GetGroupFileDataIDsForLod(0); } }

        /// <summary>GFID block for one LOD level (0 = full detail); entries may be 0 past LOD0.
        /// Empty when that block does not exist.</summary>
        public uint[] GetGroupFileDataIDsForLod(int lod)
        {
            int n = GroupCount;
            // Widen before adding one: lod + 1 in int wraps at int.MaxValue, the product turns
            // negative and would pass the check into a wrapped Array.Copy offset.
            if (lod < 0 || n <= 0 || ((long)lod + 1) * n > GroupFileDataIDs.Length)
                return new uint[0];
            var r = new uint[n];
            System.Array.Copy(GroupFileDataIDs, lod * n, r, 0, n);
            return r;
        }

        /// <summary>The NUL-terminated name at a MOGN offset, or null when the offset is negative
        /// or outside the block.</summary>
        public string GetGroupName(int offset)
        {
            if (offset < 0 || offset >= GroupNameBlock.Length)
                return null;
            int end = offset;
            while (end < GroupNameBlock.Length && GroupNameBlock[end] != 0)
                end++;
            var chars = new char[end - offset];
            for (int i = 0; i < chars.Length; i++)
                chars[i] = (char)GroupNameBlock[offset + i];
            return new string(chars);
        }

        public bool TryGetMaterial(int index, out WmoMaterial material)
        {
            if (index >= 0 && index < Materials.Length)
            {
                material = Materials[index];
                return true;
            }
            material = default(WmoMaterial);
            return false;
        }

        /// <summary>Every texture FileDataID referenced by any material, deduplicated across the
        /// whole WMO, in first-use order (material order, then slot order).</summary>
        public uint[] GetAllTextureFileDataIDs()
        {
            var seen = new System.Collections.Generic.HashSet<uint>();
            var ids = new System.Collections.Generic.List<uint>();
            for (int m = 0; m < Materials.Length; m++)
                foreach (uint id in Materials[m].GetTextureFileDataIDs())
                    if (seen.Add(id))
                        ids.Add(id);
            return ids.ToArray();
        }

        /// <summary>First top-level chunk with this tag.</summary>
        public bool TryGetChunk(string tag, out WmoChunkInfo chunk)
        {
            return WmoChunks.TryFind(Chunks, tag, out chunk);
        }

        /// <summary>A copy of a chunk's payload.</summary>
        public byte[] CopyChunkBytes(WmoChunkInfo chunk)
        {
            return WmoChunks.Copy(Data, chunk);
        }
    }

    // ================================================================================= group

    /// <summary>MOGP group-flag bits whose relation to the file contents is proven on every
    /// full-detail retail group. Other bits are kept in the flags but not named.</summary>
    public static class WmoGroupFlags
    {
        public const uint ColorSet1 = 0x00000004;        // a MOCV colour set 1 is present
        public const uint Outdoor = 0x00000008;          // exactly one of Outdoor / Indoor is set
        public const uint DoodadRefs = 0x00000800;       // iff MODR
        public const uint Liquid = 0x00001000;           // iff MLIQ
        public const uint Indoor = 0x00002000;
        public const uint ColorSet2 = 0x01000000;        // a MOCV colour set 2 is present
        public const uint TwoTexCoordSets = 0x02000000;  // at least 2 MOTV
        public const uint NoBatches = 0x04000000;        // such groups have no MOBA records
        public const uint NoVertices = 0x08000000;       // iff no MOVT
        public const uint ThreeTexCoordSets = 0x40000000;// at least 3 MOTV
    }

    /// <summary>MOGP header, 68 bytes at the start of the MOGP payload.</summary>
    public struct WmoGroupHeader
    {
        public const int Size = 68;

        public byte[] Raw;                  // the 68 bytes as stored
        public int GroupNameOffset;         // +0x00, into root MOGN
        public int DescriptiveNameOffset;   // +0x04, into root MOGN
        public uint Flags;                  // +0x08, see WmoGroupFlags
        public WowVec3 BoundsMin;           // +0x0C, == MOGI bounds
        public WowVec3 BoundsMax;           // +0x18
        public ushort PortalStart;          // +0x24, into MOPR
        public ushort PortalCount;          // +0x26
        /// <summary>+0x28/+0x2A/+0x2C: MOBA record count == A + B + C. The ranges are neither a
        /// transparency split nor an indoor/outdoor split; their meaning is not settled.</summary>
        public ushort BatchCountA, BatchCountB, BatchCountC;
        public ushort BatchCountD;          // +0x2E, zero on retail
        public byte FogId0, FogId1, FogId2, FogId3;   // +0x30, into root MFOG
        public uint GroupLiquid;            // +0x34
        public uint UniqueId;               // +0x38, 0 in LOD group files
        public uint Flags2;                 // +0x3C
        public short SplitGroupParent;      // +0x40
        public short SplitGroupNext;        // +0x42

        public int TotalBatchCount { get { return BatchCountA + BatchCountB + BatchCountC; } }
    }

    /// <summary>
    /// MOBA, one 24-byte render batch: a MOVI index range drawn with one material. The batches
    /// are the render geometry; MOVI triangles outside every batch are collision-only.
    /// </summary>
    public struct WmoBatch
    {
        public const int RecordSize = 24;
        public const byte FlagLargeMaterialId = 0x02;

        public byte[] Raw;                  // the 24 bytes as stored
        public short BoxMinX, BoxMinY, BoxMinZ;   // +0x00 (approximate batch box)
        public short BoxMaxX, BoxMaxY;      // +0x06
        /// <summary>+0x0A u16: the material id when Flags has 0x2, otherwise the box max z.</summary>
        public ushort MaterialIdLarge;
        public uint StartIndex;             // +0x0C, first MOVI index
        public ushort IndexCount;           // +0x10
        public ushort MinVertex;            // +0x12
        public ushort MaxVertex;            // +0x14, inclusive
        public byte Flags;                  // +0x16
        public byte MaterialIdSmall;        // +0x17, the material id when Flags lacks 0x2

        /// <summary>The material index into root MOMT, by the rule proven on every retail batch.</summary>
        public int MaterialId;

        /// <summary>IndexCount rounded down to whole triangles (a handful of retail batches carry
        /// a count that is not a multiple of 3; the stray indices are not a triangle).</summary>
        public int TriangleIndexCount { get { return IndexCount - IndexCount % 3; } }

        /// <summary>(flags &amp; 0x2) ? u16 @0x0A : u8 @0x17. The 8-bit byte is NOT the low byte of
        /// the 16-bit id on flag-0x2 batches, so the rule must be applied exactly.</summary>
        public static int ResolveMaterialId(byte flags, ushort materialIdLarge, byte materialIdSmall)
        {
            return (flags & FlagLargeMaterialId) != 0 ? materialIdLarge : materialIdSmall;
        }
    }

    /// <summary>One MOCV vertex-colour stream: 4 bytes per vertex, B G R A.</summary>
    public sealed class WmoColorSet
    {
        /// <summary>1 or 2 (see WmoParser.AssignColorSetNumbers). Numbers above 2 only occur when
        /// a file carries more MOCV chunks than its flags announce.</summary>
        public int SetNumber;
        /// <summary>0-based position among the group's MOCV chunks in file order.</summary>
        public int FileOrder;
        public byte[] Bgra = new byte[0];

        public int Count { get { return Bgra.Length / 4; } }
    }

    /// <summary>MOPY, 2 bytes per triangle.</summary>
    public struct WmoPolyMaterial
    {
        public byte Flags;
        public byte MaterialId;             // 0xFF = collision-only triangle
    }

    /// <summary>MPY2, 4 bytes per triangle (groups that carry MOGX instead of MOPY).</summary>
    public struct WmoPolyMaterial2
    {
        public ushort Flags;
        public ushort MaterialId;
    }

    /// <summary>
    /// A parsed group WMO file. Groups without geometry (flag 0x8000000, antiportals, groups with
    /// no batches) parse to empty arrays, never null.
    /// </summary>
    public sealed class WmoGroup
    {
        public string What = "";
        /// <summary>The bytes this was parsed from; every WmoChunkInfo offset indexes into it.</summary>
        public byte[] Data;
        /// <summary>The caller's group index (position in GFID / MOGI), or -1 when not given.</summary>
        public int GroupIndex = -1;

        public uint Version;                                        // MVER, 17
        public WmoGroupHeader Header;                               // MOGP header

        public WowVec3[] Positions = new WowVec3[0];                // MOVT, WoW model space
        /// <summary>MONR, one per vertex; empty when the group has no MONR.</summary>
        public WowVec3[] Normals = new WowVec3[0];
        public ushort[] Indices = new ushort[0];                    // MOVI, triangle list

        /// <summary>Every MOTV stream in file order (up to 4 on retail), each one entry per vertex.
        /// Which set a material samples is not settled; none is dropped.</summary>
        public WowVec2[][] TexCoordSets = new WowVec2[0][];

        /// <summary>Every MOCV stream in file order (up to 2 on retail), with its colour-set number.
        /// Kept for a later stage; rendering does not use vertex colours in this stage.</summary>
        public WmoColorSet[] ColorSets = new WmoColorSet[0];

        /// <summary>MOC2 (4 bytes per vertex, present with 4 MOTV), or null when absent or not a whole
        /// number of 4-byte entries (then with a warning; its role is not settled and it is not drawn).</summary>
        public byte[] Moc2;

        public WmoPolyMaterial[] PolyMaterials = new WmoPolyMaterial[0];     // MOPY
        public WmoPolyMaterial2[] PolyMaterials2 = new WmoPolyMaterial2[0];  // MPY2
        /// <summary>Whether the chunk exists. A malformed one (partial record) is left undecoded
        /// with a warning, because per-triangle records are not needed to draw batches.</summary>
        public bool HasMopy, HasMpy2;

        public WmoBatch[] Batches = new WmoBatch[0];                // MOBA

        /// <summary>Top-level chunks (MVER, MOGP, anything after MOGP) in file order.</summary>
        public WmoChunkInfo[] TopChunks = new WmoChunkInfo[0];
        /// <summary>Every MOGP sub-chunk in file order, decoded or not (MOBN, MODR, MLIQ, MOGX, ...).</summary>
        public WmoChunkInfo[] Chunks = new WmoChunkInfo[0];

        public string[] Warnings = new string[0];

        public int VertexCount { get { return Positions.Length; } }

        /// <summary>True when there is anything to draw: vertices and at least one batch.</summary>
        public bool HasRenderGeometry { get { return Positions.Length > 0 && Batches.Length > 0; } }

        /// <summary>The colour stream with this set number (1 or 2), or null.</summary>
        public WmoColorSet GetColorSet(int setNumber)
        {
            for (int i = 0; i < ColorSets.Length; i++)
                if (ColorSets[i].SetNumber == setNumber)
                    return ColorSets[i];
            return null;
        }

        public WmoColorSet ColorSet1 { get { return GetColorSet(1); } }
        public WmoColorSet ColorSet2 { get { return GetColorSet(2); } }

        /// <summary>First MOGP sub-chunk with this tag.</summary>
        public bool TryGetChunk(string tag, out WmoChunkInfo chunk)
        {
            return WmoChunks.TryFind(Chunks, tag, out chunk);
        }

        /// <summary>A copy of a chunk's payload.</summary>
        public byte[] CopyChunkBytes(WmoChunkInfo chunk)
        {
            return WmoChunks.Copy(Data, chunk);
        }

        /// <summary>Bounds of the vertices actually referenced by batches (WoW space). False when
        /// nothing is drawn. The MOGP header bounds contain these but are not always tight.</summary>
        public bool TryGetBatchBounds(out WowVec3 min, out WowVec3 max)
        {
            min = new WowVec3(float.MaxValue, float.MaxValue, float.MaxValue);
            max = new WowVec3(-float.MaxValue, -float.MaxValue, -float.MaxValue);
            bool any = false;
            for (int b = 0; b < Batches.Length; b++)
            {
                int start = (int)Batches[b].StartIndex, end = start + Batches[b].TriangleIndexCount;
                for (int i = start; i < end; i++)
                {
                    WowVec3 p = Positions[Indices[i]];
                    if (p.X < min.X) min.X = p.X;
                    if (p.Y < min.Y) min.Y = p.Y;
                    if (p.Z < min.Z) min.Z = p.Z;
                    if (p.X > max.X) max.X = p.X;
                    if (p.Y > max.Y) max.Y = p.Y;
                    if (p.Z > max.Z) max.Z = p.Z;
                    any = true;
                }
            }
            return any;
        }
    }

    /// <summary>Chunk-list helpers shared by root and group.</summary>
    public static class WmoChunks
    {
        public static bool TryFind(WmoChunkInfo[] chunks, string tag, out WmoChunkInfo chunk)
        {
            if (chunks != null)
                for (int i = 0; i < chunks.Length; i++)
                    if (chunks[i].Tag == tag)
                    {
                        chunk = chunks[i];
                        return true;
                    }
            chunk = default(WmoChunkInfo);
            return false;
        }

        public static byte[] Copy(byte[] data, WmoChunkInfo chunk)
        {
            if (data == null || chunk.Size < 0 || chunk.DataOffset < 0 || chunk.DataOffset > data.Length ||
                chunk.Size > data.Length - chunk.DataOffset)
                return null;
            var r = new byte[chunk.Size];
            System.Buffer.BlockCopy(data, chunk.DataOffset, r, 0, chunk.Size);
            return r;
        }
    }
}
