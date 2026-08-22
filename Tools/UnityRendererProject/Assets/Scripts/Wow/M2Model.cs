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
        /// <summary>Four influences, weight/255. The indices are DIRECT indices into the
        /// model's bone array -- not through any lookup table. See M2BoneDef.</summary>
        public byte BoneWeight0, BoneWeight1, BoneWeight2, BoneWeight3;
        public byte BoneIndex0, BoneIndex1, BoneIndex2, BoneIndex3;
    }

    /// <summary>
    /// One M2 bone, reduced to what a bind pose needs: where it sits, and whose child it is.
    ///
    /// The animation tracks are deliberately NOT parsed here. The reason is the shape of the
    /// format: a bone's matrix is built as T(pivot) * T(translation) * R(rotation) * S(scale) *
    /// T(-pivot) and then composed with its parent's, so with every track left at rest the matrix
    /// is the IDENTITY -- and the vertex positions in the file are already in that pose. The bind
    /// pose therefore needs the pivot and the parent, nothing else, and animation is a later
    /// milestone that fills in the three tracks around the same pivot.
    /// </summary>
    public struct M2BoneDef
    {
        public int KeyBoneId;        // index into the key-bone lookup, -1 when this is not one
        public uint Flags;
        public short Parent;         // -1 for a root bone
        public ushort SubmeshId;
        public WowVec3 Pivot;        // model space, the point this bone rotates about

        /// <summary>Bit 0x08. A billboarded bone is re-oriented towards the viewer every frame by
        /// the legacy viewport; at rest it is an ordinary bone, which is all this milestone
        /// needs.</summary>
        public bool Billboard { get { return (Flags & 0x08) != 0; } }
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
        /// <summary>Bit 0x08 is BILLBOARD in the legacy viewport, not a depth-test flag: it only
        /// feeds that renderer's environment-mapping decision, and nothing there ever disables the
        /// depth test. Kept for completeness, deliberately unused.</summary>
        public bool Billboard { get { return (Flags & 0x08) != 0; } }
        public bool DepthWriteDisabled { get { return (Flags & 0x10) != 0; } }
    }

    /// <summary>
    /// One entry of the M2 "colors" array, reduced to what a static render needs.
    ///
    /// Each entry holds two animation tracks -- an RGB track and an alpha track -- and a draw
    /// batch points at one of them through its ColorIndex. The alpha track is how a model hides
    /// geometry it does not currently want drawn: an eye overlay, a glow, a blink. The legacy
    /// OpenGL renderer refuses to draw a batch whose entry resolves to zero alpha, which is why
    /// a model can ship geometry that is never visible at rest.
    ///
    /// Only the value at the start of animation 0 is kept: this milestone renders a static pose.
    /// </summary>
    public struct M2ColorDef
    {
        /// <summary>The RGB track carries data for animation 0. When it does not, the legacy
        /// renderer treats the batch as invisible rather than as untinted.</summary>
        public bool HasColorTrack;

        /// <summary>Alpha at the start of animation 0; 1 when the track carries no data.</summary>
        public float Alpha;
    }

    /// <summary>
    /// Where a texture unit takes its texture coordinates from. Matches the codes the OpenGL
    /// renderer uses in ModelRenderPass::uvSource, which are derived from the material's vertex
    /// shader name (Diffuse_T1_Env -> unit 0 = T1, unit 1 = Env).
    /// </summary>
    public enum M2UvSource
    {
        TexCoord0 = 0,
        TexCoord1 = 1,
        Environment = 2,   // sphere map generated from the view-space normal, not a stored UV set
        TexCoord2 = 3,
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

        /// <summary>The "colors" array: batch.ColorIndex selects one. Empty when the header is
        /// too short to carry it, in which case every batch is treated as visible.</summary>
        public M2ColorDef[] Colors = new M2ColorDef[0];

        /// <summary>The "texture_weights" array, resolved to the value at animation 0 time 0.
        /// A batch reaches one through TextureWeightLookup[batch.TextureWeightComboIndex].</summary>
        public float[] TextureWeights = new float[0];

        public ushort[] TextureWeightLookup = new ushort[0];
        public int SkinProfileCount;
        public int[] SkinFileDataIDs = new int[0];       // SFID chunk
        public int[] TextureFileDataIDs = new int[0];    // TXID chunk (0 where replaceable)

        /// <summary>
        /// The model's bones. Empty when the model declares none, and ALSO empty when the bones
        /// live in a separate skeleton file -- see SkeletonFileDataID.
        /// </summary>
        public M2BoneDef[] Bones = new M2BoneDef[0];

        /// <summary>Number of bones the header declares. Equal to Bones.Length whenever the bones
        /// are in the .m2 itself.</summary>
        public int BoneCount;

        /// <summary>
        /// The SKID chunk: the FileDataID of a separate skeleton (.skel) file. Non-zero means the
        /// bone array in the header is not the one the renderer must use -- the real one lives in
        /// that file's SKB1 chunk (or, when it carries an SKPD, in its parent skeleton's). This
        /// milestone does not fetch it; a model that has one is drawn unskinned and says so.
        /// </summary>
        public int SkeletonFileDataID;

        /// <summary>Model-space bounds of the vertex positions (WoW axes).</summary>
        public WowVec3 BoundsMin, BoundsMax;
    }

    /// <summary>One renderable section of the skin profile (a "geoset"/submesh).</summary>
    public struct M2Submesh
    {
        /// <summary>
        /// The geoset number this submesh belongs to, group * 100 + variant, masked to 15 bits.
        /// 0 means "always drawn"; anything else is drawn only when the displayed creature variant
        /// switches that number on. See WmvModelBuilder.GeosetVisible.
        /// </summary>
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

        /// <summary>Indexes the model's texture-coord-combo table, one entry per texture unit.
        /// Parsed for completeness; modern creature M2s ship an empty table and take their
        /// per-unit UV routing from the shader id instead. 0xFFFF means "none".</summary>
        public ushort TextureCoordComboIndex;

        /// <summary>Indexes the model's texture-weight-combo table -- the second animated input
        /// to the legacy renderer's visibility gate.</summary>
        public ushort TextureWeightComboIndex;

        public ushort TextureTransformComboIndex;

        /// <summary>No colour entry: the legacy renderer's gate ignores the colour track.</summary>
        public bool HasColor { get { return ColorIndex != 0xFFFF; } }
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
