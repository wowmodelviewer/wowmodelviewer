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

    /// <summary>A quaternion in the file's own component order: x, y, z, then w.</summary>
    public struct WowQuat
    {
        public float X, Y, Z, W;
        public WowQuat(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
        public static WowQuat Identity { get { return new WowQuat(0f, 0f, 0f, 1f); } }
    }

    /// <summary>How a track's values are read between keyframes.</summary>
    public enum M2Interpolation { None = 0, Linear = 1, Hermite = 2, Bezier = 3 }

    /// <summary>
    /// One animation track, already narrowed to the single sequence the renderer will play.
    ///
    /// On disk a track is an array of per-sequence keyframe arrays; carrying all of them would
    /// mean parsing every animation a model has (hundreds, on a boss) to play one. This holds the
    /// keys for the sequence that was asked for -- or, when the track is driven by a GLOBAL
    /// SEQUENCE, the keys at index 0, because that is the entry the legacy evaluator reads for
    /// those regardless of which sequence is playing.
    /// </summary>
    public struct M2Track<T>
    {
        public M2Interpolation Interpolation;

        /// <summary>Index into the model's GlobalSequences, or -1 for an ordinary track. A global
        /// sequence runs on its own clock, looping over its own duration, independently of the
        /// animation being played.</summary>
        public int GlobalSequence;

        public uint[] Times;     // milliseconds, ascending
        public T[] Values;

        public bool HasData { get { return Values != null && Values.Length > 0; } }
        public bool IsGlobal { get { return GlobalSequence >= 0; } }
    }

    /// <summary>One entry of the animation sequence table (64 bytes on disk).</summary>
    public struct M2Sequence
    {
        public short AnimId;      // AnimationData.db2 id -- 0 is "Stand"
        public short SubAnimId;
        public uint Length;       // milliseconds
        public uint Flags;

        /// <summary>
        /// Bit 0x20: this sequence's keyframes are stored in the .m2 itself rather than in a
        /// separate .anim file. The legacy viewport's header calls the bit "looped", which is a
        /// misnomer -- and taking it at that word is expensive, because the track headers of a
        /// sequence WITHOUT the bit still sit in the .m2 and still look reasonable: their offsets
        /// address the .anim file, land inside this buffer by coincidence, and read as noise.
        /// chicken2's sequence 14 is exactly that, and it is not even named by an AFID entry, so
        /// no amount of looking for the keys distinguishes it. The flag does.
        /// </summary>
        public bool PrimarySequence { get { return (Flags & 0x20) != 0; } }
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

        /// <summary>The three tracks that move this bone, narrowed to the sequence that was
        /// parsed. Empty tracks are the common case: most bones hold still in any one animation,
        /// and a bone with no track at all keeps its rest transform.</summary>
        public M2Track<WowVec3> Translation;
        public M2Track<WowQuat> Rotation;
        public M2Track<WowVec3> Scale;

        /// <summary>Does anything move this bone in the parsed sequence?</summary>
        public bool IsAnimated
        {
            get { return Translation.HasData || Rotation.HasData || Scale.HasData; }
        }

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
    /// <summary>One AFID entry: which file holds animation (animId, subAnimId)'s keyframes.</summary>
    public struct AfidEntry
    {
        public int AnimId;
        public int SubAnimId;
        public int FileDataID;
    }

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

        /// <summary>The model's animation sequence table.</summary>
        public M2Sequence[] Sequences = new M2Sequence[0];

        /// <summary>
        /// Durations, in milliseconds, of the model's global sequences. A track bound to one loops
        /// over that duration on a clock of its own, independent of the animation being played --
        /// which is how a torch flickers at its own rate whatever the creature is doing.
        /// </summary>
        public uint[] GlobalSequences = new uint[0];

        /// <summary>
        /// Which sequence the bone tracks above were parsed for, or -1 when none was. This is the
        /// idle the legacy viewport itself would select: the first sequence whose AnimId is 0
        /// ("Stand"), falling back to sequence 0 when the model has no such entry.
        /// </summary>
        public int AnimatedSequence = -1;

        /// <summary>Why no sequence was parsed, for the log. Null when one was.</summary>
        public string AnimationSkipReason;

        /// <summary>
        /// AFID: which external .anim file holds each animation's keyframes.
        ///
        /// A sequence without the 0x20 flag keeps its track HEADERS in the .m2 -- counts and
        /// offsets, per sequence, exactly where an in-file sequence keeps them -- but those
        /// offsets address the .anim file's bytes instead. So playing one needs nothing more than
        /// the right buffer to read the entries out of, which is the same thing the legacy
        /// viewport does (WoWModel::readAnimsFromFile fills an animfiles map keyed by animID, and
        /// the track reader picks the buffer from it).
        /// </summary>
        public AfidEntry[] AnimFileIds = new AfidEntry[0];

        /// <summary>
        /// The external .anim FileDataID the CURRENTLY selected sequence needs, or 0 when its
        /// keyframes are in the .m2. The renderer fetches this over the asset channel and hands
        /// the bytes back to the parser.
        /// </summary>
        public int RequiredAnimFileId;

        /// <summary>
        /// The MD21 payload this model was parsed from, kept so that changing the animation can
        /// re-read the bone tracks without copying it out of the file again.
        ///
        /// Offsets inside an .m2 are relative to the MD21 chunk, so reading it means working on
        /// that slice as its own address space. Slicing it per animation change meant allocating
        /// megabytes each time the user picked a different animation -- several MB per switch on a
        /// boss -- and it is that garbage, not the reading, that the viewport showed as a stutter.
        /// Holding the slice for as long as the model is displayed costs one buffer and makes the
        /// switch allocate essentially nothing. Null on a model parsed before this was kept.
        /// </summary>
        public byte[] Md21Payload;

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

        /// <summary>
        /// The HIGH HALF of this submesh's first triangle index.
        ///
        /// The skin format stores that start in a 16-bit field, which cannot address a skin
        /// holding more than 65535 indices, so the bits above 65535 live here and the real start
        /// is RawIndexStart + (Level &lt;&lt; 16). The legacy viewport does not read this word at
        /// all -- it reads the two together as one 32-bit id and masks the whole thing to 15 bits
        /// (Source/games/wow/modelheaders.h:226, :252) -- and sidesteps the problem by recomputing
        /// each start as a running sum of the counts before it instead
        /// (Source/games/wow/WoWModel.cpp:1601-1606). Its own comment says why that class of file
        /// exists: "to handle index &gt; 65535 (present in HD models)"
        /// (Source/games/wow/modelheaders.h:239).
        /// </summary>
        public ushort Level;

        public ushort VertexStart, VertexCount;

        /// <summary>The first triangle index AS STORED -- the low 16 bits alone. Kept so the
        /// diagnostic can show what the file said; never use it to address the triangle array.
        /// IndexStart is the whole value.</summary>
        public ushort RawIndexStart;

        /// <summary>The first triangle index, whole: RawIndexStart + (Level &lt;&lt; 16). This is
        /// the one to index with. It is an int, not a ushort, precisely because the value it
        /// carries does not fit in one.</summary>
        public int IndexStart;

        public ushort IndexCount;
    }

    /// <summary>
    /// What a skin's index starts look like, gathered while parsing so that the renderer can
    /// report them and a run can be believed rather than assumed.
    ///
    /// It records TWO independent readings of the same quantity: the format's own (the stored
    /// start plus the Level word) and the legacy viewport's (a running sum of the index counts,
    /// which is how it reaches a 32-bit start without reading Level -- see M2Submesh.Level). If
    /// the two ever disagree, one of the renderers is drawing the wrong triangles and no amount
    /// of looking at the picture will say which, so the disagreement is recorded rather than
    /// resolved by preference.
    /// </summary>
    public struct M2SkinIndexSurvey
    {
        public int Submeshes;
        public int NonZeroLevel;        // submeshes whose Level word is set
        public int MaxIndexStart;       // the largest expanded start in this skin
        public int TotalIndices;        // entries in the triangle array
        public int GeosetZero;          // submeshes whose Id is 0, the always-drawn geoset

        /// <summary>Whether the legacy's running-sum reading was computable for every submesh.
        /// It always is; the flag exists so a false reading cannot be mistaken for agreement.</summary>
        public bool CumulativeChecked;
        public bool CumulativeAgrees;

        /// <summary>The first submesh where the two readings differ, or -1.</summary>
        public int DisagreeAt;
        public int DisagreeExpanded, DisagreeCumulative;

        /// <summary>True when any start needed more than 16 bits -- i.e. when this model is one
        /// of the files the fix exists for.</summary>
        public bool ExercisesWideStarts { get { return MaxIndexStart > 0xFFFF || NonZeroLevel > 0; } }
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

        /// <summary>How this skin's index starts read, and whether the two independent readings
        /// of them agree. See M2SkinIndexSurvey.</summary>
        public M2SkinIndexSurvey IndexSurvey;
        public M2Batch[] Batches = new M2Batch[0];

        public int TriangleCount { get { return Triangles.Length / 3; } }
    }
}
