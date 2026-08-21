// M2Model.cs
//
// Plain result types for the runtime M2/skin parsers. No UnityEngine types: the parsing layer
// stays testable outside the editor, and the Unity-side builder converts these into Mesh /
// Material / Texture2D.

namespace Wmv.Wow
{
    public struct WowVec2
    {
        public float X, Y;
        public WowVec2(float x, float y) { X = x; Y = y; }
    }

    public struct WowVec3
    {
        public float X, Y, Z;
        public WowVec3(float x, float y, float z) { X = x; Y = y; Z = z; }
        public override string ToString() { return string.Format("({0:F3}, {1:F3}, {2:F3})", X, Y, Z); }
    }

    /// <summary>One M2 vertex (48 bytes on disk).</summary>
    public struct M2Vertex
    {
        public WowVec3 Position;
        public WowVec3 Normal;
        public WowVec2 TexCoord0;
        public WowVec2 TexCoord1;
        public byte BoneWeight0, BoneWeight1, BoneWeight2, BoneWeight3;   // preserved, unused in the static milestone
        public byte BoneIndex0, BoneIndex1, BoneIndex2, BoneIndex3;
    }

    /// <summary>
    /// An M2 texture slot. Type 0 carries a real filename in the file; any other type is a
    /// "replaceable" texture (creature skin, item skin, ...) whose actual asset lives in the
    /// client database -- for those the renderer asks WMV (getModelTextures).
    /// </summary>
    public struct M2TextureDef
    {
        public uint Type;
        public uint Flags;
        public string FileName;      // empty for replaceable textures
        public int FileDataID;       // from the TXID chunk when present; 0 when replaceable

        public bool IsReplaceable { get { return Type != 0; } }
        public bool WrapX { get { return (Flags & 0x1) != 0; } }
        public bool WrapY { get { return (Flags & 0x2) != 0; } }
    }

    /// <summary>M2 render flags + blend mode (the "texFlags"/materials array).</summary>
    public struct M2MaterialDef
    {
        public ushort Flags;
        public ushort BlendMode;

        public bool Unlit { get { return (Flags & 0x01) != 0; } }
        public bool NoFog { get { return (Flags & 0x02) != 0; } }
        public bool TwoSided { get { return (Flags & 0x04) != 0; } }
        public bool DepthTestDisabled { get { return (Flags & 0x08) != 0; } }
        public bool DepthWriteDisabled { get { return (Flags & 0x10) != 0; } }
    }

    public enum M2BlendMode
    {
        Opaque = 0,
        AlphaKey = 1,      // cutout
        Alpha = 2,
        NoAlphaAdd = 3,
        Add = 4,
        Mod = 5,
        Mod2x = 6,
        BlendAdd = 7,
    }

    /// <summary>A parsed M2 (the parts this milestone needs).</summary>
    public class M2ParsedModel
    {
        public string Name = "";
        public uint Version;
        public uint GlobalFlags;
        public M2Vertex[] Vertices = new M2Vertex[0];
        public M2TextureDef[] Textures = new M2TextureDef[0];
        public M2MaterialDef[] Materials = new M2MaterialDef[0];
        public ushort[] TextureLookup = new ushort[0];   // batch.textureComboIndex -> texture index
        public int SkinProfileCount;
        public int[] SkinFileDataIDs = new int[0];       // SFID chunk
        public int[] TextureFileDataIDs = new int[0];    // TXID chunk (0 where replaceable)
        public int BoneCount;

        /// <summary>Model-space bounds of the vertex positions (WoW axes).</summary>
        public WowVec3 BoundsMin, BoundsMax;
    }

    /// <summary>One renderable section of the skin profile (a "geoset"/submesh).</summary>
    public struct M2Submesh
    {
        public ushort Id;
        public ushort Level;
        public ushort VertexStart, VertexCount;
        public ushort IndexStart, IndexCount;   // into the skin's triangle-index array
    }

    /// <summary>A draw call: which submesh with which material/texture.</summary>
    public struct M2Batch
    {
        public byte Flags;
        public sbyte PriorityPlane;
        public ushort ShaderId;
        public ushort SubmeshIndex;
        public ushort GeosetIndex;
        public ushort ColorIndex;
        public ushort MaterialIndex;
        public ushort MaterialLayer;
        public ushort TextureCount;
        public ushort TextureComboIndex;
    }

    /// <summary>A parsed .skin profile.</summary>
    public class M2ParsedSkin
    {
        public ushort[] VertexLookup = new ushort[0];   // skin vertex -> M2 vertex index
        public ushort[] Triangles = new ushort[0];      // indices into VertexLookup
        public M2Submesh[] Submeshes = new M2Submesh[0];
        public M2Batch[] Batches = new M2Batch[0];

        public int TriangleCount { get { return Triangles.Length / 3; } }
    }
}
