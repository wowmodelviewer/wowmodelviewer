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

        /// <summary>
        /// The RGB track, as the legacy renderer evaluates it: ALWAYS animation 0's keys, whatever
        /// is playing -- ModelRenderPass::init passes the literal index 0 to getValue for this
        /// track (Source/games/wow/ModelRenderPass.cpp:400). Three floats, 0..1.
        /// </summary>
        public M2Track<WowVec3> Color;

        /// <summary>
        /// The alpha track, at the sequence that was parsed. This one the legacy renderer DOES
        /// evaluate at the playing animation (ModelRenderPass.cpp:403). Stored as fixed16 (32767 is
        /// 1.0, Animated.h ShortToFloat); held here as 0..1.
        /// </summary>
        public M2Track<float> Opacity;

        /// <summary>
        /// This entry's alpha is constant zero in the parsed sequence, but ANOTHER sequence of the
        /// model can show it: that sequence either has alpha keys above zero, or has no alpha keys
        /// at all (the legacy then keeps ocol.w at 1, ModelRenderPass.cpp:401-404). A batch gated
        /// by such an entry must be built and withheld, or a sequence change could never show it.
        /// Sequences whose keys live in a .anim file that was not read are counted in
        /// OpacityOtherUnknown and treated as able to show it.
        /// </summary>
        public bool OpacityMayOpenElsewhere;
        public int OpacityOtherVisible, OpacityOtherUnknown;
    }

    /// <summary>
    /// One entry of the M2 "texture_transforms" array: the three tracks a texture unit's stored
    /// coordinates are run through. The legacy renderer composes them as
    ///
    ///     glLoadIdentity; glTranslatef(t); glRotatef(rot.x, 0,0,1); glScalef(s)
    ///
    /// (Source/games/wow/TextureAnim.cpp:24-36), i.e. uv' = T(R(S(uv))) with no pivot -- and it
    /// reads the rotation track, which the format stores as four floats, as three, then rotates
    /// by the first component in degrees. Its own comment says "this is wrong". The rotation is
    /// therefore parsed as the file stores it and NOT taken as a reference for anything.
    /// </summary>
    public struct M2TextureTransform
    {
        public M2Track<WowVec3> Translation;
        public M2Track<WowQuat> Rotation;     // four floats per key, exactly as stored
        public M2Track<WowVec3> Scale;

        /// <summary>Does anything move this transform in the parsed sequence (or on its global
        /// sequence)?</summary>
        public bool IsAnimated
        {
            get { return Translation.HasData || Rotation.HasData || Scale.HasData; }
        }
    }

    /// <summary>
    /// What the material-track read found -- counted, so that a decision taken on the data (which
    /// tracks are worth evaluating, whether the legacy's index-0 transparency rule costs anything)
    /// rests on a number rather than on an impression.
    /// </summary>
    public struct M2MaterialTrackSurvey
    {
        public int TextureTransforms, TransformsAnimated, RotationTracksWithData;
        public int Colors, ColorRgbTracks, ColorOpacityTracks;
        public int TextureWeightTracks, WeightsAnimated;

        /// <summary>
        /// Transparency tracks whose keys for the PARSED sequence differ from animation 0's. The
        /// legacy renderer evaluates every transparency track at index 0 (ModelRenderPass.cpp:443),
        /// so this is exactly the data it never reads; a non-zero count is the cost of mirroring
        /// that rule, measured.
        /// </summary>
        public int WeightPerSequenceDiffers, WeightPerSequenceChecked;

        /// <summary>Arrays whose offset or count did not fit the payload and were left empty --
        /// never silently replaced by an invented value.</summary>
        public int Rejected;
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

    /// <summary>
    /// A particle emitter's shape. Measured across 34,873 emitters in the current client:
    /// Plane 64.7 %, Sphere 34.5 %, Spline 0.8 %, and three emitters declaring 4.
    /// </summary>
    public enum M2EmitterType { Plane = 1, Sphere = 2, Spline = 3 }

    /// <summary>
    /// One M2 particle emitter. 492 bytes on disk -- MEASURED, not assumed: of the eight
    /// candidate strides tried against real files, 492 validated every emitter of all 978
    /// multi-emitter models in a 6,000-model sample and every other candidate failed on some
    /// model (see M2Parser.ParticleEmitterStride). Both M2 versions this client ships, 272 and
    /// 274, use it.
    ///
    /// The three "ramp" arrays below are the emitter's start / middle / end stops. On disk they
    /// are FakeAnimationBlocks -- the same 16-byte shape as a track header, but the offsets point
    /// straight at the values with no per-sequence indirection and no timestamps, because the
    /// ramp is indexed by a particle's own age rather than by the clock.
    /// </summary>
    public struct M2ParticleEmitterDef
    {
        public int Flags;
        public WowVec3 Position;        // model space, relative to Bone
        public short Bone;
        public short TextureId;         // a DIRECT index into Textures[], not through TextureLookup
        public byte BlendMode;
        public byte EmitterType;
        public ushort ParticleColorIndex;   // 11, 12 or 13 select a ParticleColor override
        public short TextureTileRotation;
        public ushort Rows, Cols;

        public M2Track<float> EmissionSpeed;
        public M2Track<float> SpeedVariation;
        public M2Track<float> VerticalRange;
        public M2Track<float> HorizontalRange;
        public M2Track<float> Gravity;
        public M2Track<float> Lifespan;
        public M2Track<float> EmissionRate;
        public M2Track<float> EmissionAreaLength;
        public M2Track<float> EmissionAreaWidth;
        public M2Track<float> ZSource;
        /// <summary>
        /// Whether the emitter is emitting at all, as a track. ONE BYTE per key on disk, despite
        /// modelheaders.h:506 declaring uint16 and particle.h:92 declaring Animated&lt;uint16&gt;:
        /// read as uint16 the array yields 256 and 257 on 310 of 1,753 reads, which is exactly
        /// what adjacent one-byte keys of 0 and 1 look like when paired. The byte values that
        /// actually occur are 0 and 1 and nothing else.
        /// </summary>
        public M2Track<float> EnabledIn;

        /// <summary>
        /// The colour, opacity and size ramps, indexed by a particle's normalised AGE.
        ///
        /// NOT three stops at 0 / 0.5 / 1, which is what the legacy runtime assumes: it memcpys
        /// exactly three keys whatever the count says and hardcodes the midpoint at 0.5
        /// (particle.cpp:47-57, with its author's own "mid can't be 0 or 1, TODO" beside it).
        /// Measured over 17,202 ramp tracks in 1,500 client models, neither half of that holds:
        /// 42 % of ramps do not have three keys (2, 4, 5, 6, 7, 8 and up to 13 all occur), and
        /// among those that do the middle stop sits at 0.5 only 55 % of the time -- 0.2, 0.25,
        /// 0.3, 0.35, 0.6 and 0.75 all appear. A two-key ramp read the legacy's way takes its
        /// third stop from whatever bytes follow the authored data.
        ///
        /// The times are stored as uint16 normalised by 32767, not as milliseconds. Over those
        /// same 17,202 tracks the first key is 0 and the last is 32767 in 100.0 % of them, and
        /// every one is monotonic; read as uint32 milliseconds, 0.0 % are either. Each of the
        /// three ramps carries its own times, so they are kept separately rather than zipped.
        /// </summary>
        public float[] ColorTimes;
        public WowVec3[] ColorKeys;      // 0..1 (stored 0..255)
        public float[] AlphaTimes;
        public float[] AlphaKeys;        // 0..1 (stored fixed16)
        public float[] SizeTimes;
        public WowVec2[] SizeKeys;       // already multiplied by ParticleScale

        /// <summary>
        /// ModelParticleParams.scales, read as the two-component quantity it measures as.
        ///
        /// The legacy runtime indexes this by RAMP STOP -- sizes[i] = key[i] * scales[i]
        /// (particle.cpp:55) -- which is a category error: it is a per-AXIS scale applied to
        /// every stop. The data settles it. Its x and y are equal on every emitter examined
        /// (1.0/1.0 on Val'anyr, 0.1/0.1 and 1.3/1.3 on the Celestial cape) while the third
        /// float is 519.0, -1.0 or 1.0 on different models -- not a scale at all. Under the
        /// legacy's reading a 0.0556 particle on Val'anyr becomes 28.85 units across, and
        /// another gets a NEGATIVE size.
        /// </summary>
        public WowVec2 ParticleScale;

        public float Slowdown;
        public float SpriteRotation;

        public bool WorldSpace { get { return (Flags & 0x8) != 0; } }
        public bool DoNotTrail { get { return (Flags & 0x10) != 0; } }
        public bool ModelSpace { get { return (Flags & 0x80) != 0; } }
        public bool Pinned { get { return (Flags & 0x400) != 0; } }
        public bool DoNotBillboard { get { return (Flags & 0x1000) != 0; } }
        public bool RandomTexture { get { return (Flags & 0x10000) != 0; } }
        public bool Outward { get { return (Flags & 0x20000) != 0; } }
        public bool RandomStart { get { return (Flags & 0x200000) != 0; } }

        /// <summary>
        /// Bit 0x800000. Set on 82.9 % of the client's emitters, and it changes how a Gravity KEY
        /// is encoded -- not how big it is. See M2Parser.ReadCompressedGravity: reading these keys
        /// as float32, which the legacy runtime does unconditionally (particle.cpp:38), produces
        /// NaN or infinity on 47 % of them and |v| &gt; 1e6 on 65 %.
        /// </summary>
        public bool CompressedGravity { get { return (Flags & 0x800000) != 0; } }

        public bool MultiTexture { get { return (Flags & 0x10000000) != 0; } }

        /// <summary>Can this renderer draw it? Plane and Sphere, which is 99.2 % of the client.
        /// The legacy runtime builds no emitter object for anything else either
        /// (particle.cpp:109-121), so a Spline emitter has never been drawn by WMV.</summary>
        public bool Supported
        {
            get { return EmitterType == (byte)M2EmitterType.Plane || EmitterType == (byte)M2EmitterType.Sphere; }
        }
    }

    /// <summary>
    /// One M2 ribbon emitter. 176 bytes on disk, measured the same way as the particle stride:
    /// of four candidates it was the only one that validated, on 45 of the 94 multi-ribbon
    /// models in a 12,000-model sample, and no other candidate scored at all.
    /// </summary>
    public struct M2RibbonEmitterDef
    {
        public int Bone;
        public WowVec3 Position;        // model space, relative to Bone
        public int[] TextureIds;        // direct indices into Textures[]

        public M2Track<WowVec3> Color;
        public M2Track<float> Opacity;  // fixed16 on disk
        public M2Track<float> Above;
        public M2Track<float> Below;

        /// <summary>
        /// Edges emitted per second, and how long an edge lives. NOT the legacy's reading -- see
        /// WmvEmitterRuntime.RibbonState for why the data forces this one: across the client
        /// these measure 30..100 and 0.1..2.0, which are an emission rate and a lifetime in
        /// seconds, and give 5..200 edges.
        /// </summary>
        public float EdgesPerSecond;
        public float EdgeLifetimeSeconds;

        public float EmissionAngle;

        /// <summary>
        /// The ribbon's material, indexed into the model's materials ("texFlags") array at header
        /// offset 0x70 -- NOT into the textures array, and not parallel to TextureIds. The count
        /// is 1 on every ribbon measured (525 of 525). This is where a ribbon's blend mode comes
        /// from; the legacy hardcodes SRC_ALPHA/ONE instead (particle.cpp:847) and so never reads
        /// it. -1 when the array is absent or does not resolve.
        /// </summary>
        public int MaterialIndex;
    }

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

        /// <summary>
        /// The "texture_weights" tracks themselves, parallel to TextureWeights, at animation 0 --
        /// which is where the legacy renderer reads them from whatever is playing (see
        /// M2MaterialTrackSurvey.WeightPerSequenceDiffers for what that rule leaves out).
        /// </summary>
        public M2Track<float>[] TextureWeightTracks = new M2Track<float>[0];

        /// <summary>The "texture_transforms" array, with each track narrowed to the parsed
        /// sequence. Empty when the header is too short to carry it or the array does not fit.</summary>
        public M2TextureTransform[] TextureTransforms = new M2TextureTransform[0];

        /// <summary>
        /// batch.TextureTransformComboIndex + unit -> index into TextureTransforms. 0xFFFF, or any
        /// value past the array, means the unit has no transform -- the legacy viewport tests
        /// "a0 &lt; nAnims" and leaves texanim at -1 otherwise (WoWModel.cpp:1856-1858).
        /// </summary>
        public ushort[] TextureTransformLookup = new ushort[0];

        public M2MaterialTrackSurvey MaterialSurvey;

        /// <summary>
        /// The model's particle emitters, with every track narrowed to the parsed sequence.
        ///
        /// THE SEQUENCE MATTERS HERE MORE THAN IT DOES FOR BONES. An emitter whose EmissionRate
        /// has no keys in the playing sequence emits nothing at all, and that is authored, not a
        /// fault: Val'anyr (253423) ships two sub-animations of Stand, and eleven of its thirteen
        /// emitters carry EmissionRate keys only in the second. So these are re-read whenever the
        /// sequence changes, exactly like the bone tracks.
        /// </summary>
        public M2ParticleEmitterDef[] ParticleEmitters = new M2ParticleEmitterDef[0];

        /// <summary>The model's ribbon emitters, tracks narrowed to the parsed sequence.</summary>
        public M2RibbonEmitterDef[] RibbonEmitters = new M2RibbonEmitterDef[0];

        /// <summary>Counts as the header declares them, before anything was dropped as
        /// unsupported or out of bounds. Reported so "we drew none" and "there were none" stay
        /// distinguishable in the log.</summary>
        public int ParticleEmitterCount, RibbonEmitterCount;

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

        /// <summary>
        /// Bit 0x10 of the batch flags. The legacy viewport tests exactly this bit -- and only
        /// this bit -- before assigning a texture transform to a pass (WoWModel.cpp:1848,
        /// TEXTUREUNIT_STATIC); the colour and opacity indices are assigned regardless of it
        /// (WoWModel.cpp:1805-1807). So STATIC suppresses UV animation and nothing else.
        /// </summary>
        public bool Static { get { return (Flags & 0x10) != 0; } }
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
