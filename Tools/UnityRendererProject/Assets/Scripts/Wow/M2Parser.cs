// M2Parser.cs
//
// Runtime parser for the M2 bytes WMV serves over IPC. No files, no disk: byte[] in, parsed
// model out.
//
// SUPPORTED FORMAT
//   Modern retail M2 ("MD21"-chunked, MD20 payload). Verified against War Within / Midnight
//   retail data: creature/chicken/chicken.m2 is MD20 version 272. A bare (unchunked) MD20 file
//   is also accepted, which covers older data, but nothing older than the MD20 header layout
//   below is supported -- this milestone deliberately targets the version current retail ships.
//
// CHUNKED WRAPPER
//   A modern .m2 is a sequence of {char magic[4]; uint32 size; byte payload[size]} chunks. The
//   model itself lives in the MD21 chunk and *every offset inside it is relative to the start
//   of that chunk's payload*, not to the start of the file. Sibling chunks carry the
//   FileDataIDs of related assets: SFID (skin profiles), TXID (textures), SKID (a separate
//   skeleton file, completed by ApplySkeleton) and AFID (external .anim files). BFID/PFID are
//   further file-reference chunks this parser does not use.
//
// The MD20 header layout below matches the one WMV's own loader uses (Source/games/wow/
// modelheaders.h): count/offset pairs, with nViews being a lone uint32 because view (LOD) data
// moved out into the .skin files.

using System;
using System.Collections.Generic;
using System.Text;

namespace Wmv.Wow
{
    public static class M2Parser
    {
        public const int VertexStride = 48;
        const int TextureStride = 16;
        const int MaterialStride = 4;

        // One M2 bone on disk: keyBoneId(4) flags(4) parent(2) submeshId(2) boneNameCRC(4),
        // three animation tracks of 20 bytes each, then the pivot (12). 88 bytes in total.
        const int BoneStride = 88;
        const int OfsBoneTranslation = 16;
        const int OfsBoneRotation = 36;
        const int OfsBoneScale = 56;
        const int OfsBonePivot = 76;

        // A bone track's header is interpolation(2) globalSeq(2) then two nested M2Arrays -- the
        // timestamps and the values, each an array with ONE ENTRY PER ANIMATION SEQUENCE. It is
        // the same 20 bytes as TrackStride, declared below for the colour and weight tracks.
        const int SequenceStride = 64;      // one entry of the animation sequence table
        const int OfsSequenceAliasNext = 62; // its last int16: the alias target, with flag 0x40
        const int PackedQuatStride = 8;     // a rotation key: four int16
        const int Vec3Stride = 12;          // a translation or scale key

        /// <summary>The AnimId the legacy viewport looks for when picking a model's default
        /// animation ("Stand"). See ResolveIdleSequence.</summary>
        const int AnimIdStand = 0;

        // MD20 header offsets (relative to the MD21 payload)
        const int OfsName = 0x08;
        const int OfsGlobalFlags = 0x10;
        const int OfsGlobalSequences = 0x14;
        const int OfsSequences = 0x1C;
        const int OfsBones = 0x2C;
        const int OfsVertices = 0x3C;
        const int OfsNumSkinProfiles = 0x44;
        const int OfsTextures = 0x50;
        const int OfsColors = 0x48;        // M2Color[]: an RGB track + an alpha track each
        const int OfsMaterials = 0x70;     // "texFlags": render flags + blend mode
        const int OfsTextureWeights = 0x58;
        const int OfsTextureLookup = 0x80;
        const int OfsTextureWeightLookup = 0x90;
        const int OfsTextureTransforms = 0x60;        // M2TextureTransform[]: three M2Tracks each
        const int OfsTextureTransformLookup = 0x98;   // ushort[]: batch combo index + unit -> transform
        // The lookups a character needs, by the same field-order arithmetic as the emitter offsets
        // below: animation lookup and key-bone lookup follow the sequence and bone arrays; the
        // attachments come after the two 28-byte bounding spheres (0xA0, 0xBC) and the three
        // bounding arrays (0xD8..0xEC).
        const int OfsAnimationLookup = 0x24;          // int16[]: animation id -> sequence
        const int OfsKeyBoneLookup = 0x34;            // int16[]: key bone -> bone
        const int OfsAttachments = 0xF0;              // M2AttachmentDef[], 40 bytes each
        const int OfsAttachmentLookup = 0xF8;         // int16[]: attachment id -> attachment
        const int AttachmentStride = 40;              // id(4) bone(4) pos(12) track(20)
        const int MinHeaderSize = 0x84 + 8;
        const int TextureTransformStride = 60;        // three M2Tracks
        const int Fixed16Stride = 2;                  // a colour-alpha or texture-weight key
        const int FloatQuatStride = 16;               // a texture-transform rotation key: four floats

        const int ColorStride = 40;        // two M2Tracks
        const int TrackStride = 20;        // interpolation + globalSeq + two nested M2Arrays

        // ---- emitters -------------------------------------------------------------------------
        //
        // These two arrays sit past every offset above, at the tail of the MD20 header. Their
        // positions were computed from this repo's own ModelHeader (Source/games/wow/
        // modelheaders.h) by summing the fields ahead of them, and the same arithmetic reproduces
        // every offset already known here -- 0x14, 0x1C, 0x2C, 0x3C, 0x48, 0x50, 0x70 -- so the
        // method is checked before it is trusted. They were then confirmed against real files:
        // the counts and offsets land inside the MD21 chunk and decode to sane emitters.
        const int OfsRibbonEmitters = 0x120;
        const int OfsParticleEmitters = 0x128;

        /// <summary>
        /// The full MD20 header. The emitter arrays are the last two entries in it, so anything
        /// reaching this far has them -- and anything not reaching it has no emitters, which is
        /// not an error.
        ///
        /// The payload being long enough is NOT the same as the HEADER being long enough: a file
        /// whose header stops earlier still has bytes at 0x120, they are just the next array's
        /// contents. Nothing in the format records the header's length, so the emitters are
        /// validated on their own contents instead -- see ParticleEmitterLooksReal.
        /// </summary>
        const int HeaderSize = 0x138;
        const int EmitterHeaderSize = OfsParticleEmitters + 8;

        // Both strides are MEASURED. See M2ParticleEmitterDef / M2RibbonEmitterDef for the
        // sample sizes and for what the competing candidates did.
        const int ParticleEmitterStride = 492;
        const int RibbonEmitterStride = 176;

        // Field offsets inside M2ParticleDef.
        const int OfsParticlePos = 8;
        const int OfsParticleBone = 20;          // int16 bone, int16 texture
        const int OfsParticleBlend = 40;         // uint8 blend, uint8 emitterType, uint16 colorIndex
        const int OfsParticleTileRotation = 46;  // int16 tileRotation, uint16 rows, uint16 cols
        const int OfsParticleParams = 260;       // ModelParticleParams
        const int OfsParticleEnabledIn = 456;    // the track after ModelParticleParams

        /// <summary>
        /// Where each of the ten float tracks starts inside M2ParticleDef. Not a simple stride:
        /// the struct interleaves two int32s the header calls "unknown", one after Lifespan and
        /// one after EmissionRate. Getting that wrong reads a track header out of the middle of
        /// another track, which still parses and still produces plausible garbage -- so the shape
        /// is written out rather than computed.
        /// </summary>
        static readonly int[] ParticleTrackOffsets =
        {
            52,      // EmissionSpeed
            72,      // SpeedVariation
            92,      // VerticalRange
            112,     // HorizontalRange
            132,     // Gravity
            152,     // Lifespan
            176,     // EmissionRate       (after int32 unknown at 172)
            200,     // EmissionAreaLength (after int32 unknown2 at 196)
            220,     // EmissionAreaWidth
            240,     // zSource
        };

        // Field offsets inside ModelParticleParams (relative to OfsParticleParams). Each of the
        // first three is a FakeAnimationBlock: {nTimes, ofsTimes, nKeys, ofsKeys}, sixteen bytes,
        // FLAT -- no per-sequence indirection, because a ramp is indexed by a particle's own age
        // rather than by the clock. The KEYS are at +8/+12 and the TIMES at +0/+4; reading the
        // pair at +0 as the keys gives the times array and produces convincing garbage.
        const int OfsParamsColor = 0;      // keys: WowVec3, stored 0..255
        const int OfsParamsAlpha = 16;     // keys: int16, fixed16
        const int OfsParamsSize = 32;      // keys: WowVec2
        const int OfsParamsScales = 100;         // see M2ParticleEmitterDef.ParticleScale
        const int OfsParamsTailLength = 88;
        const int OfsParamsSlowdown = 112;
        const int OfsParamsBaseSpin = 116;        // then BaseSpinVariation at +120
        const int OfsParamsRotation = 124;

        // Field offsets inside ModelRibbonEmitterDef.
        const int OfsRibbonPos = 8;
        const int OfsRibbonTextures = 20;
        const int OfsRibbonMaterials = 28;
        const int OfsRibbonColor = 36;           // then opacity, above, below at +20 each
        const int OfsRibbonRes = 116;            // float res, float length, float emissionAngle

        /// <summary>
        /// Ceiling on a ramp's stops. The client's longest is 13 (of 17,202 tracks measured), so
        /// 16 truncates nothing real while bounding a corrupt count.
        /// </summary>
        const int MaxRampStops = 16;

        /// <summary>
        /// Parse an .m2 asset. Throws WowParseException on anything malformed.
        ///
        /// wantedSequence names which animation's bone tracks to read; -1 asks for the model's
        /// default idle. Only one sequence is ever read (a track on disk is an array of
        /// per-sequence keyframe arrays, and a boss has hundreds), so playing a different one
        /// means reading the bone tracks again with a different answer here.
        ///
        /// Changing the animation does NOT come back through here: it needs the bones and nothing
        /// else, so it goes to ReadAnimationInto, which re-reads that one array and leaves the
        /// vertices, textures, materials and lookups exactly as this call left them. See there for
        /// why that distinction is worth having.
        /// </summary>
        public static M2ParsedModel Parse(byte[] file, int wantedSequence = -1,
                                          byte[] externalAnim = null)
        {
            if (file == null || file.Length < 8)
                throw new WowParseException("m2: asset is empty or too small to hold a header");

            var model = new M2ParsedModel();
            var chunks = ReadChunks(file);
            byte[] keyBuffer = AnimKeyframeBuffer(externalAnim);

            int md21Offset = 0, md21Size = file.Length;
            if (chunks.Count > 0)
            {
                if (!chunks.ContainsKey("MD21"))
                    throw new WowParseException("m2: chunked asset without an MD21 chunk");
                md21Offset = chunks["MD21"].Offset;
                md21Size = chunks["MD21"].Count;

                model.AnimFileIds = ReadAnimFileIds(file, chunks);
                model.SkinFileDataIDs = ReadIdChunk(file, chunks, "SFID");
                model.TextureFileDataIDs = ReadIdChunk(file, chunks, "TXID");
                int[] skeleton = ReadIdChunk(file, chunks, "SKID");
                model.SkeletonFileDataID = skeleton.Length > 0 ? skeleton[0] : 0;
            }

            // Work on the MD21 payload as its own address space -- offsets inside are relative
            // to it, so a slice keeps every later read honest about its bounds.
            if (md21Offset + md21Size > file.Length)
                throw new WowParseException("m2: MD21 chunk extends past the end of the asset");
            byte[] payload;
            if (md21Offset == 0 && md21Size == file.Length)
            {
                payload = file;                     // unchunked: the file already IS the payload
            }
            else
            {
                payload = new byte[md21Size];
                Buffer.BlockCopy(file, md21Offset, payload, 0, md21Size);
            }
            // Kept for ReadAnimationInto; see M2ParsedModel.Md21Payload for why.
            model.Md21Payload = payload;

            var c = new ByteCursor(payload, "m2");
            if (payload.Length < MinHeaderSize)
                throw new WowParseException(string.Format(
                    "m2: header is truncated ({0} bytes, need at least {1})", payload.Length, MinHeaderSize));

            string magic = c.ReadMagic();
            if (magic != "MD20")
                throw new WowParseException("m2: expected an MD20 header, found '" + magic + "'");
            model.Version = c.ReadUInt32();

            c.Seek(OfsName);
            M2Array name = c.ReadArray();
            model.Name = ReadString(c, name);

            c.Seek(OfsGlobalFlags);
            model.GlobalFlags = c.ReadUInt32();

            c.Seek(OfsGlobalSequences);
            M2Array globalSeqs = c.ReadArray();
            c.RequireArray(globalSeqs, 4, "global sequences");
            model.GlobalSequences = new uint[globalSeqs.Count];
            for (int i = 0; i < globalSeqs.Count; i++)
            {
                c.Seek(globalSeqs.Offset + i * 4);
                model.GlobalSequences[i] = c.ReadUInt32();
            }

            c.Seek(OfsSequences);
            M2Array sequences = c.ReadArray();
            c.RequireArray(sequences, SequenceStride, "sequences");
            model.Sequences = ReadSequences(c, sequences);

            c.Seek(OfsBones);
            M2Array bones = c.ReadArray();
            model.BoneCount = bones.Count;
            // A skeleton file overrides the header's bone array wholesale, so reading that array
            // anyway would hand the renderer a rig the vertices are not indexed against. Leave
            // Bones empty and let the caller decide what to do about it.
            if (model.SkeletonFileDataID == 0)
            {
                c.RequireArray(bones, BoneStride, "bones");
                model.AnimatedSequence = ResolveSequence(model, keyBuffer, wantedSequence,
                                                        out model.AnimationSkipReason);
                model.Bones = ReadBones(c, bones, model, ExternalFor(model, keyBuffer));
                model.RequiredAnimFileId = ExternalAnimFileId(model, model.AnimatedSequence);
                ReadHeaderLookups(c, model);
            }
            else
            {
                model.AnimationSkipReason = "its bones and animations live in a separate skeleton file";
            }

            c.Seek(OfsNumSkinProfiles);
            model.SkinProfileCount = (int)c.ReadUInt32();

            c.Seek(OfsVertices);
            M2Array verts = c.ReadArray();
            c.RequireArray(verts, VertexStride, "vertices");
            model.Vertices = ReadVertices(c, verts, model);

            c.Seek(OfsTextures);
            M2Array textures = c.ReadArray();
            c.RequireArray(textures, TextureStride, "textures");
            model.Textures = ReadTextures(c, textures, model);

            c.Seek(OfsMaterials);
            M2Array materials = c.ReadArray();
            c.RequireArray(materials, MaterialStride, "materials");
            model.Materials = new M2MaterialDef[materials.Count];
            for (int i = 0; i < materials.Count; i++)
            {
                c.Seek(materials.Offset + i * MaterialStride);
                model.Materials[i].Flags = c.ReadUInt16();
                model.Materials[i].BlendMode = c.ReadUInt16();
            }

            c.Seek(OfsTextureLookup);
            M2Array texLookup = c.ReadArray();
            c.RequireArray(texLookup, 2, "textureLookup");
            model.TextureLookup = new ushort[texLookup.Count];
            for (int i = 0; i < texLookup.Count; i++)
            {
                c.Seek(texLookup.Offset + i * 2);
                model.TextureLookup[i] = c.ReadUInt16();
            }

            ReadVisibilityTracks(c, model);
            ReadMaterialTracks(c, model, keyBuffer);
            ReadEmittersForSequence(c, model);

            return model;
        }

        /// <summary>
        /// Re-read ONLY the bone animation for a different sequence, writing it into a model that
        /// has already been parsed.
        ///
        /// Changing the displayed animation needs exactly four things: which sequence resolved,
        /// the sequence table, the global sequences, and the bone tracks for that sequence. It
        /// does not need the vertices, the textures, the materials, the texture lookups or the
        /// visibility tracks -- none of those depend on which animation is playing, and on a model
        /// of any size reading them again is most of the cost of a full parse. Doing that work on
        /// every selection change is what made the viewport stutter when the user picked a new
        /// animation from the dropdown.
        ///
        /// The model is updated IN PLACE rather than replaced, because the rest of it is still
        /// live: the skin path reads model.Textures to decide which slots a variant's texture
        /// feeds, and handing it a half-populated model would break selecting a skin after
        /// selecting an animation.
        ///
        /// Throws WowParseException like Parse does; the caller keeps the previous animation.
        /// </summary>
        public static void ReadAnimationInto(byte[] file, int wantedSequence, M2ParsedModel model,
                                             byte[] externalAnim = null)
        {
            if (model == null)
                throw new WowParseException("m2: no parsed model to read an animation into");
            if (file == null || file.Length < 8)
                throw new WowParseException("m2: asset is empty or too small to hold a header");

            // A skeleton-file model has no bone array of its own to re-read. Until its skeleton
            // has been applied it was never animated and still is not; once it has, the bone
            // tracks come from the skeleton bytes ApplySkeleton kept.
            if (model.SkeletonFileDataID != 0 && !model.SkeletonApplied)
            {
                model.AnimationSkipReason = "its bones and animations live in a separate skeleton file";
                model.AnimatedSequence = -1;
                return;
            }
            byte[] keyBuffer = AnimKeyframeBuffer(externalAnim);
            if (model.SkeletonApplied)
            {
                ReadSkeletonAnimation(model, wantedSequence, keyBuffer);
                return;
            }

            var chunks = ReadChunks(file);

            // Reuse the slice the load already cut. Cutting it again is the single biggest
            // allocation an animation change could make -- megabytes on a large model, every
            // time the selection changes -- and none of it is needed: the bytes have not moved.
            byte[] payload = model.Md21Payload;
            if (payload == null)
            {
                int md21Offset = 0, md21Size = file.Length;
                if (chunks.Count > 0)
                {
                    if (!chunks.ContainsKey("MD21"))
                        throw new WowParseException("m2: chunked asset without an MD21 chunk");
                    md21Offset = chunks["MD21"].Offset;
                    md21Size = chunks["MD21"].Count;
                }
                if (md21Offset + md21Size > file.Length)
                    throw new WowParseException("m2: MD21 chunk extends past the end of the asset");
                payload = new byte[md21Size];
                Buffer.BlockCopy(file, md21Offset, payload, 0, md21Size);
                model.Md21Payload = payload;
            }
            var c = new ByteCursor(payload, "m2");
            if (payload.Length < MinHeaderSize)
                throw new WowParseException(string.Format(
                    "m2: header is truncated ({0} bytes, need at least {1})", payload.Length, MinHeaderSize));

            c.Seek(OfsBones);
            M2Array bones = c.ReadArray();
            c.RequireArray(bones, BoneStride, "bones");

            // Resolve against the sequence table the model already holds -- it does not change
            // with the selection, and ResolveSequence reads it to decide whether the wanted
            // sequence's keyframes are in this file at all.
            model.AnimatedSequence = ResolveSequence(model, keyBuffer, wantedSequence,
                                                     out model.AnimationSkipReason);
            model.Bones = ReadBones(c, bones, model, ExternalFor(model, keyBuffer));
            model.RequiredAnimFileId = ExternalAnimFileId(model, model.AnimatedSequence);

            // The material tracks that follow the playing sequence -- colour alpha and the texture
            // transforms -- have to follow it here too, or a sequence change would leave them on
            // the previous animation's keys while the bones moved on.
            ReadMaterialTracks(c, model, keyBuffer);

            // Emitters likewise, and for them it is not a refinement: an emitter whose
            // EmissionRate has no keys in the new sequence must STOP, and one that gains keys
            // must start. See M2ParsedModel.ParticleEmitters.
            ReadEmittersForSequence(c, model);
        }

        /// <summary>
        /// Resolve which buffer this sequence's keyframes live in, then read both emitter arrays.
        /// Shared by Parse and ReadAnimationInto so the two can never disagree about it.
        /// </summary>
        static void ReadEmittersForSequence(ByteCursor c, M2ParsedModel model)
        {
            int seq = model.AnimatedSequence >= 0 ? model.AnimatedSequence : 0;
            ReadEmitters(c, model, seq);
        }

        /// <summary>
        /// The external bytes apply only when the sequence that actually resolved is the external
        /// one. A request that fell back to the in-file idle must read the .m2, not the .anim that
        /// was fetched for the sequence it could not play. keyBuffer is what AnimKeyframeBuffer
        /// made of the .anim file, not the raw file. An alias is judged by its target: one that
        /// aliases an in-file sequence reads the .m2 too.
        /// </summary>
        static byte[] ExternalFor(M2ParsedModel model, byte[] keyBuffer)
        {
            if (keyBuffer == null || model.AnimatedSequence < 0)
                return null;
            return KeySequenceOf(model, model.AnimatedSequence).PrimarySequence ? null : keyBuffer;
        }

        // -----------------------------------------------------------------------------------
        // .anim files, skeleton files and the character lookups
        // -----------------------------------------------------------------------------------

        static readonly string[] AnimChunkMagics = { "AFM2", "AFSA", "AFSB" };
        static readonly string[] SkelChunkMagics = { "SKL1", "SKA1", "SKB1", "SKS1", "SKPD", "AFID", "BFID" };

        /// <summary>
        /// The bytes of an .anim file that key offsets address, by the host's rule. A modern .anim
        /// is chunked (AFM2, then AFSB) and the host selects AFSB when it exists
        /// (WoWModel.cpp:982); a chunked file with exactly one chunk has that chunk selected on
        /// open (CASCFile::doPostOpenOperation); anything else -- an older unchunked .anim, or a
        /// chunked one it cannot narrow -- is read whole. The file counts as chunked when it
        /// starts with an anim chunk magic whose first chunk fits, as the host tests it.
        /// Null in, null out; returns the same array when there is nothing to narrow.
        /// </summary>
        public static byte[] AnimKeyframeBuffer(byte[] animFile)
        {
            if (animFile == null)
                return null;
            List<KeyValuePair<string, M2Array>> chunks = WalkChunks(animFile, AnimChunkMagics, false);
            if (chunks == null)
                return animFile;
            foreach (KeyValuePair<string, M2Array> ch in chunks)
                if (ch.Key == "AFSB")
                    return Slice(animFile, ch.Value);
            if (chunks.Count == 1)
                return Slice(animFile, chunks[0].Value);
            return animFile;
        }

        /// <summary>
        /// Walk a chunked file, or return null when it is not one: fewer than 8 bytes, a first
        /// magic outside knownFirst, or a first chunk that does not fit. A later chunk that claims
        /// more bytes than remain throws in strict mode and ends the walk otherwise.
        /// </summary>
        static List<KeyValuePair<string, M2Array>> WalkChunks(byte[] file, string[] knownFirst,
                                                              bool strict)
        {
            if (file == null || file.Length < 8)
                return null;
            string first = MagicAt(file, 0);
            if (Array.IndexOf(knownFirst, first) < 0 || ReadUInt32At(file, 4) > (uint)(file.Length - 8))
                return null;
            var list = new List<KeyValuePair<string, M2Array>>();
            int offset = 0;
            while (offset + 8 <= file.Length)
            {
                string magic = MagicAt(file, offset);
                uint size = ReadUInt32At(file, offset + 4);
                offset += 8;
                if (size > (uint)(file.Length - offset))
                {
                    if (strict)
                        throw new WowParseException(string.Format(
                            "skel: chunk '{0}' claims {1} bytes but only {2} remain", magic, size, file.Length - offset));
                    break;
                }
                list.Add(new KeyValuePair<string, M2Array>(magic, new M2Array((int)size, offset)));
                offset += (int)size;
            }
            return list;
        }

        static string MagicAt(byte[] b, int o)
        {
            return string.Format("{0}{1}{2}{3}", (char)b[o], (char)b[o + 1], (char)b[o + 2], (char)b[o + 3]);
        }

        static byte[] Slice(byte[] file, M2Array chunk)
        {
            var b = new byte[chunk.Count];
            Buffer.BlockCopy(file, chunk.Offset, b, 0, chunk.Count);
            return b;
        }

        /// <summary>A skeleton's chunks by magic (the first of each), or null when not a skel.</summary>
        static Dictionary<string, M2Array> SkelChunks(byte[] skel, bool strict)
        {
            List<KeyValuePair<string, M2Array>> list = WalkChunks(skel, SkelChunkMagics, strict);
            if (list == null)
                return null;
            var map = new Dictionary<string, M2Array>();
            foreach (KeyValuePair<string, M2Array> ch in list)
                if (!map.ContainsKey(ch.Key))
                    map[ch.Key] = ch.Value;
            return map;
        }

        /// <summary>
        /// The SKPD chunk's parent skeleton FileDataID (uint8[8] then uint32), or 0 when the bytes
        /// are not a skeleton or carry no SKPD. Never throws.
        /// </summary>
        public static int ReadSkeletonParentId(byte[] skel)
        {
            Dictionary<string, M2Array> chunks = SkelChunks(skel, false);
            M2Array skpd;
            if (chunks == null || !chunks.TryGetValue("SKPD", out skpd) || skpd.Count < 12)
                return 0;
            return (int)ReadUInt32At(skel, skpd.Offset + 8);
        }

        /// <summary>
        /// Complete an SKID model from its skeleton file, the way WoWModel::initAnimated and its
        /// neighbours do it:
        ///
        ///   global sequences  the skel's SKS1, then the parent's SKS1 appended (WoWModel.cpp:505-543)
        ///   sequences, animation lookup, AFID
        ///                     the PARENT's SKS1 / AFID when there is one, else the skel's (:1022-1057)
        ///   bones, key-bone lookup
        ///                     the PARENT's SKB1 when there is one, else the skel's (:1062-1095)
        ///   attachments       the skel's SKA1 only -- never the parent's (:726-763)
        ///
        /// then resolves wantedSequence exactly as Parse does, reads the bone tracks from the SKB1
        /// payload (keys from the .anim for a sequence stored outside the skeleton), and re-reads
        /// the .m2's own material tracks and emitters for the sequence that resolved, because
        /// their per-sequence arrays index the skeleton's sequence table rather than the header's.
        ///
        /// externalAnim is the raw .anim file for wantedSequence, as for Parse. Throws
        /// WowParseException, leaving the model untouched, when either skeleton is not chunked or
        /// lacks SKS1 or SKB1, or when its sequence or bone array does not fit; a missing SKA1
        /// only means no attachments.
        /// </summary>
        public static void ApplySkeleton(M2ParsedModel model, byte[] skel, byte[] parentSkel,
                                         int wantedSequence, byte[] externalAnim = null)
        {
            if (model == null)
                throw new WowParseException("skel: no parsed model to apply a skeleton to");
            if (model.Md21Payload == null || model.Md21Payload.Length < MinHeaderSize)
                throw new WowParseException("skel: the model has no MD21 payload to re-read its tracks from");

            Dictionary<string, M2Array> own = RequireSkel(skel, "skel");
            Dictionary<string, M2Array> parent = parentSkel != null ? RequireSkel(parentSkel, "parent skel") : null;
            byte[] source = parent != null ? parentSkel : skel;
            Dictionary<string, M2Array> sourceChunks = parent ?? own;

            // ---- read everything first; the model changes only once nothing can throw ----
            byte[] ownSks1 = Slice(skel, own["SKS1"]);
            List<uint> globals = ReadSks1Globals(new ByteCursor(ownSks1, "skel:SKS1"));
            byte[] sks1 = ownSks1;
            if (parent != null)
            {
                sks1 = Slice(parentSkel, parent["SKS1"]);
                globals.AddRange(ReadSks1Globals(new ByteCursor(sks1, "parent skel:SKS1")));
            }
            var sc = new ByteCursor(sks1, "skel:SKS1");
            sc.Seek(8);
            M2Array seqArr = sc.ReadArray();
            M2Array animLookupArr = sc.ReadArray();
            sc.RequireArray(seqArr, SequenceStride, "sequences");
            M2Sequence[] sequences = ReadSequences(sc, seqArr);
            short[] animLookup = ReadInt16Array(sc, animLookupArr);
            AfidEntry[] afids = ReadAnimFileIds(source, sourceChunks);

            byte[] skb1 = Slice(source, sourceChunks["SKB1"]);
            var bc = new ByteCursor(skb1, "skel:SKB1");
            if (skb1.Length < 16)
                throw new WowParseException("skel: SKB1 is too small to hold its header");
            bc.Seek(0);
            M2Array boneArr = bc.ReadArray();
            M2Array keyBoneArr = bc.ReadArray();
            bc.RequireArray(boneArr, BoneStride, "bones");
            M2BoneDef[] bones = ReadBoneHierarchy(bc, boneArr);
            short[] keyBones = SanitiseKeyBones(ReadInt16Array(bc, keyBoneArr), bones.Length);

            M2AttachmentDef[] attachments = new M2AttachmentDef[0];
            short[] attachmentLookup = new short[0];
            M2Array ska1Chunk;
            if (own.TryGetValue("SKA1", out ska1Chunk) && ska1Chunk.Count >= 16)
            {
                var ac = new ByteCursor(Slice(skel, ska1Chunk), "skel:SKA1");
                ac.Seek(0);
                M2Array attArr = ac.ReadArray();
                M2Array attLookupArr = ac.ReadArray();
                attachments = ReadAttachments(ac, attArr);
                attachmentLookup = ReadInt16Array(ac, attLookupArr);
            }

            // ---- commit ----
            byte[] keyBuffer = AnimKeyframeBuffer(externalAnim);
            model.GlobalSequences = globals.ToArray();
            model.Sequences = sequences;
            model.AnimationLookup = animLookup;
            model.AnimFileIds = afids;
            model.Bones = bones;
            model.BoneCount = bones.Length;
            model.KeyBoneLookup = keyBones;
            model.Attachments = attachments;
            model.AttachmentLookup = attachmentLookup;
            model.SkeletonBonesPayload = skb1;
            model.ParentSkeletonFileDataID = ReadSkeletonParentId(skel);
            model.SkeletonApplied = true;
            ReadSkeletonAnimation(model, wantedSequence, keyBuffer);
        }

        static Dictionary<string, M2Array> RequireSkel(byte[] skel, string what)
        {
            Dictionary<string, M2Array> chunks = SkelChunks(skel, true);
            if (chunks == null)
                throw new WowParseException(what + ": not a chunked skeleton file");
            if (!chunks.ContainsKey("SKS1"))
                throw new WowParseException(what + ": no SKS1 chunk (sequences)");
            if (!chunks.ContainsKey("SKB1"))
                throw new WowParseException(what + ": no SKB1 chunk (bones)");
            if (chunks["SKS1"].Count < 24)
                throw new WowParseException(what + ": SKS1 is too small to hold its header");
            return chunks;
        }

        static List<uint> ReadSks1Globals(ByteCursor c)
        {
            c.Seek(0);
            M2Array arr = c.ReadArray();
            c.RequireArray(arr, 4, "global sequences");
            var list = new List<uint>(arr.Count);
            for (int i = 0; i < arr.Count; i++)
            {
                c.Seek(arr.Offset + i * 4);
                list.Add(c.ReadUInt32());
            }
            return list;
        }

        /// <summary>
        /// Resolve the sequence and read everything that follows it for a model whose skeleton
        /// has been applied: the bone tracks from the kept SKB1 payload, then the .m2's material
        /// tracks and emitters from the kept MD21 payload. Shared by ApplySkeleton and
        /// ReadAnimationInto so the two can never disagree.
        /// </summary>
        static void ReadSkeletonAnimation(M2ParsedModel model, int wantedSequence, byte[] keyBuffer)
        {
            if (model.SkeletonBonesPayload == null || model.Md21Payload == null)
                throw new WowParseException("skel: the model does not hold the payloads its skeleton was read from");
            model.AnimatedSequence = ResolveSequence(model, keyBuffer, wantedSequence,
                                                     out model.AnimationSkipReason);
            var bc = new ByteCursor(model.SkeletonBonesPayload, "skel:SKB1");
            bc.Seek(0);
            M2Array boneArr = bc.ReadArray();
            var bones = (M2BoneDef[])model.Bones.Clone();
            ReadBoneTracks(bc, boneArr, bones, model.AnimatedSequence, ExternalFor(model, keyBuffer));
            model.Bones = bones;
            model.RequiredAnimFileId = ExternalAnimFileId(model, model.AnimatedSequence);

            var c = new ByteCursor(model.Md21Payload, "m2");
            ReadMaterialTracks(c, model, keyBuffer);
            ReadEmittersForSequence(c, model);
        }

        /// <summary>
        /// The bone tracks for any sequence, as a NEW array: the model's hierarchy, pivots, flags
        /// and name hashes with that sequence's tracks. The model is not modified. This is what a
        /// second clock over part of the skeleton needs -- the host closes a fist by evaluating
        /// the hand's key bones in HandsClosed while the body plays something else.
        ///
        /// externalAnim is the raw .anim file for that sequence when it is stored outside the
        /// model or skeleton. Every track comes back empty, rather than an exception, when the
        /// sequence is out of range, its keys are external and no bytes were given, or the model
        /// holds no payload to read from (an SKID model whose skeleton was not applied).
        /// </summary>
        public static M2BoneDef[] ReadBoneTracksForSequence(M2ParsedModel model, int sequence,
                                                            byte[] externalAnim)
        {
            if (model == null)
                return new M2BoneDef[0];
            var bones = (M2BoneDef[])model.Bones.Clone();

            byte[] payload = model.SkeletonApplied ? model.SkeletonBonesPayload
                           : model.SkeletonFileDataID == 0 ? model.Md21Payload : null;
            M2Array arr = new M2Array(0, 0);
            byte[] keys = null;
            int seq = -1;
            if (payload != null && sequence >= 0 && sequence < model.Sequences.Length)
            {
                int at = model.SkeletonApplied ? 0 : OfsBones;
                if (payload.Length >= at + 8)
                {
                    arr = new M2Array((int)ReadUInt32At(payload, at), (int)ReadUInt32At(payload, at + 4));
                    // Judged by the alias target, as ExternalAnimFileId (which names the bytes
                    // the caller passes) judges it.
                    if (KeySequenceOf(model, sequence).PrimarySequence)
                        seq = sequence;
                    else if (externalAnim != null)
                    {
                        keys = AnimKeyframeBuffer(externalAnim);
                        seq = sequence;
                    }
                }
            }
            ReadBoneTracks(new ByteCursor(payload ?? new byte[0], "bones"), arr, bones, seq, keys);
            return bones;
        }

        /// <summary>
        /// Read the header's character lookups for an in-file skeleton. Each one is optional: a
        /// header too short to hold its entry, or an array that does not fit, leaves it empty.
        /// </summary>
        static void ReadHeaderLookups(ByteCursor c, M2ParsedModel model)
        {
            model.AnimationLookup = ReadInt16ArrayAt(c, OfsAnimationLookup);
            model.KeyBoneLookup = SanitiseKeyBones(ReadInt16ArrayAt(c, OfsKeyBoneLookup), model.Bones.Length);
            model.Attachments = new M2AttachmentDef[0];
            if (c.Length >= OfsAttachments + 8)
            {
                c.Seek(OfsAttachments);
                M2Array atts = c.ReadArray();
                model.Attachments = ReadAttachments(c, atts);
            }
            model.AttachmentLookup = ReadInt16ArrayAt(c, OfsAttachmentLookup);
        }

        static short[] ReadInt16ArrayAt(ByteCursor c, int headerOffset)
        {
            if (c.Length < headerOffset + 8)
                return new short[0];
            c.Seek(headerOffset);
            return ReadInt16Array(c, c.ReadArray());
        }

        /// <summary>An int16 array, or an empty one when it does not fit.</summary>
        static short[] ReadInt16Array(ByteCursor c, M2Array arr)
        {
            if (arr.Count <= 0 || !Fits(c, arr.Offset, arr.Count, 2))
                return new short[0];
            var v = new short[arr.Count];
            for (int i = 0; i < arr.Count; i++)
            {
                c.Seek(arr.Offset + i * 2);
                v[i] = c.ReadInt16();
            }
            return v;
        }

        static M2AttachmentDef[] ReadAttachments(ByteCursor c, M2Array arr)
        {
            if (arr.Count <= 0 || !Fits(c, arr.Offset, arr.Count, AttachmentStride))
                return new M2AttachmentDef[0];
            var atts = new M2AttachmentDef[arr.Count];
            try
            {
                for (int i = 0; i < arr.Count; i++)
                {
                    c.Seek(arr.Offset + i * AttachmentStride);
                    atts[i].Id = c.ReadInt32();
                    atts[i].Bone = c.ReadUInt16();
                    c.ReadUInt16();
                    atts[i].Position = ReadVec3(c);
                }
            }
            catch (WowParseException)
            {
                return new M2AttachmentDef[0];      // a non-finite position: not attachments
            }
            return atts;
        }

        /// <summary>
        /// The host's rule (WoWModel.cpp:1143-1145): an entry below -1 or past the bone array is
        /// garbage from a bogus count and becomes -1, so it is skipped rather than indexed.
        /// </summary>
        static short[] SanitiseKeyBones(short[] lookup, int boneCount)
        {
            for (int i = 0; i < lookup.Length; i++)
                if (lookup[i] < -1 || lookup[i] >= boneCount)
                    lookup[i] = -1;
            return lookup;
        }

        /// <summary>
        /// The sequence that plays animation animId: through the animation lookup when it names a
        /// sequence that exists, else the first sequence with that AnimId, else -1.
        /// </summary>
        public static int SequenceForAnimId(M2ParsedModel model, int animId)
        {
            if (model == null || animId < 0)
                return -1;
            if (animId < model.AnimationLookup.Length)
            {
                int s = model.AnimationLookup[animId];
                if (s >= 0 && s < model.Sequences.Length)
                    return s;
            }
            for (int i = 0; i < model.Sequences.Length; i++)
                if (model.Sequences[i].AnimId == animId)
                    return i;
            return -1;
        }

        /// <summary>The bone a key bone (wow_enums.h KeyBoneTable) maps to, or -1.</summary>
        public static int BoneForKeyBone(M2ParsedModel model, int keyBone)
        {
            if (model == null || keyBone < 0 || keyBone >= model.KeyBoneLookup.Length)
                return -1;
            int b = model.KeyBoneLookup[keyBone];
            return b >= 0 && b < model.Bones.Length ? b : -1;
        }

        /// <summary>The attachment point an attachment id resolves to through the lookup.</summary>
        public static bool AttachmentFor(M2ParsedModel model, int attachmentId, out M2AttachmentDef def)
        {
            def = new M2AttachmentDef();
            if (model == null || attachmentId < 0 || attachmentId >= model.AttachmentLookup.Length)
                return false;
            int i = model.AttachmentLookup[attachmentId];
            if (i < 0 || i >= model.Attachments.Length)
                return false;
            def = model.Attachments[i];
            return true;
        }

        /// <summary>
        /// Read the two animated inputs that decide whether a draw batch is drawn at all: the
        /// colors[] array (an RGB track plus an alpha track, selected by batch.ColorIndex) and
        /// texture_weights[] (selected through texture_weight_combos). A model hides geometry it
        /// does not currently want -- an eye overlay, a glow, a blink -- by keying one of these to
        /// zero, and the legacy OpenGL renderer skips such a batch entirely rather than drawing it
        /// transparent. See WmvModelBuilder.BatchVisibility.
        ///
        /// These arrays sit past the header offsets the rest of this parser needs, so a shorter
        /// header simply yields empty arrays -- which the builder reads as "everything visible",
        /// the behaviour before this was parsed.
        /// </summary>
        static void ReadVisibilityTracks(ByteCursor c, M2ParsedModel model)
        {
            if (c.Length < OfsTextureWeightLookup + 8)
                return;

            c.Seek(OfsColors);
            M2Array colors = c.ReadArray();
            if (colors.Count > 0 && FitsArray(c, colors, ColorStride))
            {
                model.Colors = new M2ColorDef[colors.Count];
                for (int i = 0; i < colors.Count; i++)
                {
                    int rec = colors.Offset + i * ColorStride;
                    float ignored;
                    model.Colors[i].HasColorTrack = ReadTrackFirstValue(c, rec, false, out ignored);
                    float alpha;
                    model.Colors[i].Alpha = ReadTrackFirstValue(c, rec + TrackStride, true, out alpha)
                                            ? alpha : 1f;
                }
            }

            c.Seek(OfsTextureWeights);
            M2Array weights = c.ReadArray();
            if (weights.Count > 0 && FitsArray(c, weights, TrackStride))
            {
                model.TextureWeights = new float[weights.Count];
                for (int i = 0; i < weights.Count; i++)
                {
                    float w;
                    model.TextureWeights[i] =
                        ReadTrackFirstValue(c, weights.Offset + i * TrackStride, true, out w) ? w : 1f;
                }
            }

            c.Seek(OfsTextureWeightLookup);
            M2Array weightLookup = c.ReadArray();
            if (weightLookup.Count > 0 && FitsArray(c, weightLookup, 2))
            {
                model.TextureWeightLookup = new ushort[weightLookup.Count];
                for (int i = 0; i < weightLookup.Count; i++)
                {
                    c.Seek(weightLookup.Offset + i * 2);
                    model.TextureWeightLookup[i] = c.ReadUInt16();
                }
            }
        }

        static bool FitsArray(ByteCursor c, M2Array arr, int stride)
        {
            return arr.Offset >= 0 && arr.Count >= 0 &&
                   (long)arr.Offset + (long)arr.Count * stride <= c.Length;
        }

        /// <summary>
        /// Can any sequence other than the parsed one show a batch gated by this colour entry?
        /// Reads the alpha track's per-sequence key arrays straight from the file: a sequence with
        /// no keys shows it (the legacy default is alpha 1), a sequence stored in this file with a
        /// key above zero shows it, a sequence stored in a .anim file is unknown and counted as
        /// able to. A global-sequence track has one key set for every sequence and is never
        /// "elsewhere". Measured across the client, 744 of 7596 readable models carry at least
        /// one batch this decides.
        /// </summary>
        static void ScanOpacityAcrossSequences(ByteCursor c, int trackOffset, M2ParsedModel model,
                                               int parsedSequence, ref M2ColorDef color)
        {
            color.OpacityMayOpenElsewhere = false;
            color.OpacityOtherVisible = 0;
            color.OpacityOtherUnknown = 0;
            if (!Fits(c, trackOffset, 1, TrackStride))
                return;
            c.Seek(trackOffset + 2);
            short globalSeq = c.ReadInt16();
            if (globalSeq >= 0)
                return;                                   // one key set for all sequences
            c.Seek(trackOffset + 4);
            M2Array times = c.ReadArray();
            M2Array values = c.ReadArray();
            const int NestedStride = 8;
            if (!Fits(c, times.Offset, times.Count, NestedStride) ||
                !Fits(c, values.Offset, values.Count, NestedStride))
                return;
            int nSeq = model.Sequences.Length;
            for (int s = 0; s < nSeq; s++)
            {
                if (s == parsedSequence)
                    continue;
                if (s >= times.Count || s >= values.Count)
                {
                    color.OpacityOtherVisible++;          // no entry: no keys: alpha stays 1
                    continue;
                }
                c.Seek(values.Offset + s * NestedStride);
                M2Array keys = c.ReadArray();
                if (keys.Count <= 0)
                {
                    color.OpacityOtherVisible++;          // no keys: alpha stays 1
                    continue;
                }
                if (!KeySequenceOf(model, s).PrimarySequence)
                {
                    color.OpacityOtherUnknown++;          // keys in a .anim file not read here
                    continue;
                }
                if (!Fits(c, keys.Offset, keys.Count, Fixed16Stride))
                    continue;                             // malformed: cannot show anything
                bool above = false;
                for (int k = 0; k < keys.Count && !above; k++)
                {
                    c.Seek(keys.Offset + k * Fixed16Stride);
                    if (c.ReadInt16() > 0) above = true;
                }
                if (above) color.OpacityOtherVisible++;
            }
            color.OpacityMayOpenElsewhere = color.OpacityOtherVisible + color.OpacityOtherUnknown > 0;
        }

        /// <summary>
        /// Read the material-animation tracks: each colour entry's RGB and alpha, each texture
        /// weight, each texture transform, and the transform lookup. Runs in Parse and again in
        /// ReadAnimationInto, because two of these follow the playing sequence.
        ///
        /// WHICH SEQUENCE EACH TRACK IS READ AT mirrors the legacy renderer exactly, index for
        /// index, rather than the format's intent:
        ///
        ///   colour RGB        animation 0, always     ModelRenderPass.cpp:400  getValue(0, ...)
        ///   colour alpha      the playing sequence    ModelRenderPass.cpp:403  getValue(model-&gt;anim, ...)
        ///   texture weight    animation 0, always     ModelRenderPass.cpp:443  getValue(0, ...)
        ///   texture transform the playing sequence    WoWModel.cpp:2302        calc(Anim, t)
        ///
        /// A track on a global sequence reads entry 0 whatever the index (ReadTrack does that).
        /// The transparency rule is a quirk -- it means per-sequence transparency data is never
        /// evaluated -- and rather than argue about it, the survey counts how many tracks carry
        /// per-sequence keys that differ from animation 0's, so the cost of the rule is a number.
        ///
        /// Anything that does not fit the payload is left EMPTY and counted in the survey, never
        /// replaced by a default: an empty track means "does not animate", which is a safe reading
        /// of a broken array and the same policy the bone tracks use.
        /// </summary>
        static void ReadMaterialTracks(ByteCursor c, M2ParsedModel model, byte[] externalAnim)
        {
            var survey = new M2MaterialTrackSurvey();
            model.TextureWeightTracks = new M2Track<float>[0];
            model.TextureTransforms = new M2TextureTransform[0];
            model.TextureTransformLookup = new ushort[0];
            if (c.Length < OfsTextureTransformLookup + 8)
            {
                model.MaterialSurvey = survey;
                return;
            }

            int seq = model.AnimatedSequence;
            byte[] extBytes = ExternalFor(model, externalAnim);
            ByteCursor ext = extBytes != null ? new ByteCursor(extBytes, "anim") : default(ByteCursor);
            bool hasExt = extBytes != null;
            // Animation 0's keys are read from this file: the external buffer belongs to the
            // sequence that resolved and to no other. Only when THAT is sequence 0 does entry 0
            // live in the .anim.
            bool extAt0 = hasExt && seq == 0;

            // ---- colours ------------------------------------------------------------------
            c.Seek(OfsColors);
            M2Array colors = c.ReadArray();
            if (colors.Count > 0 && FitsArray(c, colors, ColorStride))
            {
                // ReadVisibilityTracks has already sized this array and filled the animation-0
                // summary fields; only the tracks are added here.
                if (model.Colors.Length != colors.Count)
                    model.Colors = new M2ColorDef[colors.Count];
                survey.Colors = colors.Count;
                for (int i = 0; i < colors.Count; i++)
                {
                    int rec = colors.Offset + i * ColorStride;
                    model.Colors[i].Color = ReadTrack<WowVec3>(c, rec, 0, Vec3Stride, ReadVec3, ext, extAt0);
                    model.Colors[i].Opacity = seq >= 0
                        ? ReadTrack<float>(c, rec + TrackStride, seq, Fixed16Stride, ReadFixed16, NoExternalKeys, false)
                        : EmptyTrack<float>();
                    if (model.Colors[i].Color.HasData) survey.ColorRgbTracks++;
                    if (model.Colors[i].Opacity.HasData) survey.ColorOpacityTracks++;
                    ScanOpacityAcrossSequences(c, rec + TrackStride, model, seq, ref model.Colors[i]);
                }
            }
            else if (colors.Count > 0)
                survey.Rejected++;

            // ---- texture weights ----------------------------------------------------------
            c.Seek(OfsTextureWeights);
            M2Array weights = c.ReadArray();
            if (weights.Count > 0 && FitsArray(c, weights, TrackStride))
            {
                var tracks = new M2Track<float>[weights.Count];
                survey.TextureWeightTracks = weights.Count;
                for (int i = 0; i < weights.Count; i++)
                {
                    int rec = weights.Offset + i * TrackStride;
                    tracks[i] = ReadTrack<float>(c, rec, 0, Fixed16Stride, ReadFixed16, ext, extAt0);
                    if (tracks[i].Values.Length > 1) survey.WeightsAnimated++;
                    // The keys the legacy rule never reads. Counted, not used.
                    if (seq > 0 && !tracks[i].IsGlobal)
                    {
                        M2Track<float> atSeq = ReadTrack<float>(c, rec, seq, Fixed16Stride, ReadFixed16, NoExternalKeys, false);
                        survey.WeightPerSequenceChecked++;
                        if (!SameKeys(tracks[i], atSeq)) survey.WeightPerSequenceDiffers++;
                    }
                }
                model.TextureWeightTracks = tracks;
            }
            else if (weights.Count > 0)
                survey.Rejected++;

            // ---- texture transforms -------------------------------------------------------
            c.Seek(OfsTextureTransforms);
            M2Array xforms = c.ReadArray();
            if (xforms.Count > 0 && FitsArray(c, xforms, TextureTransformStride))
            {
                var arr = new M2TextureTransform[xforms.Count];
                survey.TextureTransforms = xforms.Count;
                for (int i = 0; i < xforms.Count; i++)
                {
                    int rec = xforms.Offset + i * TextureTransformStride;
                    if (seq >= 0)
                    {
                        arr[i].Translation = ReadTrack<WowVec3>(c, rec, seq, Vec3Stride, ReadVec3, NoExternalKeys, false);
                        arr[i].Rotation = ReadTrack<WowQuat>(c, rec + TrackStride, seq, FloatQuatStride, ReadFloatQuat, NoExternalKeys, false);
                        arr[i].Scale = ReadTrack<WowVec3>(c, rec + 2 * TrackStride, seq, Vec3Stride, ReadVec3, NoExternalKeys, false);
                    }
                    else
                    {
                        arr[i].Translation = EmptyTrack<WowVec3>();
                        arr[i].Rotation = EmptyTrack<WowQuat>();
                        arr[i].Scale = EmptyTrack<WowVec3>();
                    }
                    if (arr[i].IsAnimated) survey.TransformsAnimated++;
                    if (arr[i].Rotation.HasData) survey.RotationTracksWithData++;
                }
                model.TextureTransforms = arr;
            }
            else if (xforms.Count > 0)
                survey.Rejected++;

            c.Seek(OfsTextureTransformLookup);
            M2Array lookup = c.ReadArray();
            if (lookup.Count > 0 && FitsArray(c, lookup, 2))
            {
                var l = new ushort[lookup.Count];
                for (int i = 0; i < lookup.Count; i++)
                {
                    c.Seek(lookup.Offset + i * 2);
                    l[i] = c.ReadUInt16();
                }
                model.TextureTransformLookup = l;
            }
            else if (lookup.Count > 0)
                survey.Rejected++;

            model.MaterialSurvey = survey;
        }

        static M2Track<T> EmptyTrack<T>()
        {
            M2Track<T> t = new M2Track<T>();
            t.GlobalSequence = -1;
            t.Times = EmptyTimes;
            t.Values = new T[0];
            return t;
        }

        static bool SameKeys(M2Track<float> a, M2Track<float> b)
        {
            if (a.Times.Length != b.Times.Length || a.Values.Length != b.Values.Length)
                return false;
            for (int i = 0; i < a.Times.Length; i++)
                if (a.Times[i] != b.Times[i]) return false;
            for (int i = 0; i < a.Values.Length; i++)
                if (a.Values[i] != b.Values[i]) return false;
            return true;
        }

        /// <summary>A colour-alpha or texture-weight key: int16, 32767 = 1.0 (Animated.h ShortToFloat).</summary>
        static float ReadFixed16(ByteCursor c)
        {
            return c.ReadInt16() / 32767f;
        }

        /// <summary>A texture-transform rotation key: four floats, x y z w, as the file stores them.</summary>
        static WowQuat ReadFloatQuat(ByteCursor c)
        {
            return new WowQuat(c.ReadSingle(), c.ReadSingle(), c.ReadSingle(), c.ReadSingle());
        }

        /// <summary>
        /// Read one M2Track and report whether animation 0 carries any data, plus that animation's
        /// first value. Layout:
        ///
        ///     uint16 interpolationType; uint16 globalSequence;
        ///     M2Array&lt;M2Array&lt;uint32&gt;&gt; timestamps;
        ///     M2Array&lt;M2Array&lt;T&gt;&gt;      values;
        ///
        /// i.e. one nested array per animation. Only animation 0 at time 0 is read: this milestone
        /// renders a static pose, exactly as the legacy renderer does with animtime 0. Values are
        /// fixed16 (32767 == 1.0) for the alpha and weight tracks; asFixed16 is false when the
        /// caller only wants to know whether the track has data at all (the RGB track).
        /// </summary>
        static bool ReadTrackFirstValue(ByteCursor c, int trackOffset, bool asFixed16, out float value)
        {
            value = 0f;
            if (trackOffset < 0 || trackOffset + TrackStride > c.Length)
                return false;

            c.Seek(trackOffset + 12);            // skip interpolation + globalSequence + timestamps
            M2Array values = c.ReadArray();
            if (values.Count <= 0 || values.Offset < 0 || values.Offset + 8 > c.Length)
                return false;

            c.Seek(values.Offset);               // animation 0's own array
            M2Array anim0 = c.ReadArray();
            if (anim0.Count <= 0)
                return false;
            if (!asFixed16)
                return true;

            if (anim0.Offset < 0 || anim0.Offset + 2 > c.Length)
                return false;
            c.Seek(anim0.Offset);
            value = c.ReadInt16() / 32767f;
            return true;
        }

        /// <summary>Walk the top-level chunk table. Empty when the asset is a bare MD20.</summary>
        public static Dictionary<string, M2Array> ReadChunks(byte[] file)
        {
            var chunks = new Dictionary<string, M2Array>();
            if (file.Length < 8)
                return chunks;
            // A bare MD20 starts with its own magic; only treat the file as chunked otherwise.
            if (file[0] == 'M' && file[1] == 'D' && file[2] == '2' && file[3] == '0')
                return chunks;

            int offset = 0;
            while (offset + 8 <= file.Length)
            {
                string magic = string.Format("{0}{1}{2}{3}", (char)file[offset], (char)file[offset + 1],
                                                             (char)file[offset + 2], (char)file[offset + 3]);
                uint size = (uint)(file[offset + 4] | (file[offset + 5] << 8) |
                                  (file[offset + 6] << 16) | (file[offset + 7] << 24));
                offset += 8;
                if (size > (uint)(file.Length - offset))
                    throw new WowParseException(string.Format(
                        "m2: chunk '{0}' claims {1} bytes but only {2} remain", magic, size, file.Length - offset));
                chunks[magic] = new M2Array((int)size, offset);   // Count = size, Offset = payload
                offset += (int)size;
            }
            return chunks;
        }

        static int[] ReadIdChunk(byte[] file, Dictionary<string, M2Array> chunks, string magic)
        {
            M2Array chunk;
            if (!chunks.TryGetValue(magic, out chunk))
                return new int[0];
            int n = chunk.Count / 4;
            var ids = new int[n];
            var c = new ByteCursor(file, "m2:" + magic);
            for (int i = 0; i < n; i++)
            {
                c.Seek(chunk.Offset + i * 4);
                ids[i] = c.ReadInt32();
            }
            return ids;
        }

        static string ReadString(ByteCursor c, M2Array arr)
        {
            if (arr.Count <= 1)
                return "";
            c.Require(arr.Offset, arr.Count);
            var sb = new StringBuilder(arr.Count);
            for (int i = 0; i < arr.Count; i++)
            {
                byte b = c.Data[arr.Offset + i];
                if (b == 0) break;
                sb.Append((char)b);
            }
            return sb.ToString();
        }

        static M2Vertex[] ReadVertices(ByteCursor c, M2Array arr, M2ParsedModel model)
        {
            var verts = new M2Vertex[arr.Count];
            float minX = float.MaxValue, minY = float.MaxValue, minZ = float.MaxValue;
            float maxX = float.MinValue, maxY = float.MinValue, maxZ = float.MinValue;

            for (int i = 0; i < arr.Count; i++)
            {
                c.Seek(arr.Offset + i * VertexStride);
                M2Vertex v;
                v.Position = new WowVec3(c.ReadSingle(), c.ReadSingle(), c.ReadSingle());
                v.BoneWeight0 = c.ReadByte(); v.BoneWeight1 = c.ReadByte();
                v.BoneWeight2 = c.ReadByte(); v.BoneWeight3 = c.ReadByte();
                v.BoneIndex0 = c.ReadByte(); v.BoneIndex1 = c.ReadByte();
                v.BoneIndex2 = c.ReadByte(); v.BoneIndex3 = c.ReadByte();
                v.Normal = new WowVec3(c.ReadSingle(), c.ReadSingle(), c.ReadSingle());
                v.TexCoord0 = new WowVec2(c.ReadSingle(), c.ReadSingle());
                v.TexCoord1 = new WowVec2(c.ReadSingle(), c.ReadSingle());
                verts[i] = v;

                if (v.Position.X < minX) minX = v.Position.X;
                if (v.Position.Y < minY) minY = v.Position.Y;
                if (v.Position.Z < minZ) minZ = v.Position.Z;
                if (v.Position.X > maxX) maxX = v.Position.X;
                if (v.Position.Y > maxY) maxY = v.Position.Y;
                if (v.Position.Z > maxZ) maxZ = v.Position.Z;
            }

            if (arr.Count > 0)
            {
                model.BoundsMin = new WowVec3(minX, minY, minZ);
                model.BoundsMax = new WowVec3(maxX, maxY, maxZ);
            }
            return verts;
        }

        /// <summary>
        /// Read the bone array. Only the hierarchy and the pivots are taken: those are what a
        /// bind pose is made of, and the three animation tracks each bone carries belong to a
        /// later milestone. A parent index outside the array is normalised to "root" here rather
        /// than left to blow up downstream -- the legacy viewport sanitises the same field for the
        /// same reason, since a bad parent otherwise recurses off the end of its bone vector.
        /// </summary>
        /// <summary>
        /// keyBuffer, when given, holds the .anim keyframe bytes this sequence's keys live in
        /// (already narrowed by AnimKeyframeBuffer). The track HEADERS are still read from c
        /// either way -- only the entries they point at come from the other buffer. That is
        /// exactly the split the legacy viewport makes (animated.h, the modelAnimData overload),
        /// and unlike the emitters, bone tracks DO take that overload: Bone::initV3 passes the
        /// animfiles map through.
        /// </summary>
        static M2BoneDef[] ReadBones(ByteCursor c, M2Array arr, M2ParsedModel model,
                                     byte[] keyBuffer)
        {
            M2BoneDef[] bones = ReadBoneHierarchy(c, arr);
            ReadBoneTracks(c, arr, bones, model.AnimatedSequence, keyBuffer);
            return bones;
        }

        /// <summary>
        /// Everything about each bone except its tracks, which are left empty. Shared by the
        /// .m2 bone array and a skeleton's SKB1, which store the same 88-byte record.
        /// </summary>
        static M2BoneDef[] ReadBoneHierarchy(ByteCursor c, M2Array arr)
        {
            var bones = new M2BoneDef[arr.Count];
            for (int i = 0; i < arr.Count; i++)
            {
                int bone = arr.Offset + i * BoneStride;
                c.Seek(bone);
                bones[i].KeyBoneId = c.ReadInt32();
                bones[i].Flags = c.ReadUInt32();
                short parent = c.ReadInt16();
                bones[i].Parent = (parent >= 0 && parent < arr.Count && parent != i) ? parent : (short)-1;
                bones[i].SubmeshId = c.ReadUInt16();
                bones[i].NameCrc = c.ReadUInt32();
                c.Seek(bone + OfsBonePivot);
                bones[i].Pivot = new WowVec3(c.ReadSingle(), c.ReadSingle(), c.ReadSingle());
            }

            // A parent chain that loops is a forest no more, and a renderer asked to parent one
            // transform inside its own descendants throws. Any bone whose chain does not reach a
            // root within the length of the array is part of a cycle, so it becomes a root.
            for (int i = 0; i < bones.Length; i++)
            {
                int p = bones[i].Parent, steps = 0;
                while (p >= 0 && steps++ <= bones.Length)
                    p = bones[p].Parent;
                if (steps > bones.Length)
                    bones[i].Parent = -1;
            }
            return bones;
        }

        /// <summary>
        /// Fill each bone's three tracks for one sequence, headers from c (the buffer the bone
        /// array lives in), keys from keyBuffer when given. A negative sequence, or a bone array
        /// that does not match, leaves the tracks empty. Never throws: ReadTrack turns anything
        /// malformed into an empty track.
        /// </summary>
        static void ReadBoneTracks(ByteCursor c, M2Array arr, M2BoneDef[] bones, int seq,
                                   byte[] keyBuffer)
        {
            if (seq < 0 || arr.Count != bones.Length || !Fits(c, arr.Offset, arr.Count, BoneStride))
            {
                for (int i = 0; i < bones.Length; i++)
                    ClearBoneTracks(ref bones[i]);
                return;
            }
            ByteCursor ext = keyBuffer != null ? new ByteCursor(keyBuffer, "anim") : NoExternalKeys;
            bool hasExt = keyBuffer != null;
            for (int i = 0; i < bones.Length; i++)
            {
                int bone = arr.Offset + i * BoneStride;
                bones[i].Translation = ReadTrack<WowVec3>(c, bone + OfsBoneTranslation, seq,
                                                          Vec3Stride, ReadVec3, ext, hasExt);
                bones[i].Rotation = ReadTrack<WowQuat>(c, bone + OfsBoneRotation, seq,
                                                       PackedQuatStride, ReadPackedQuat, ext, hasExt);
                bones[i].Scale = ReadTrack<WowVec3>(c, bone + OfsBoneScale, seq,
                                                    Vec3Stride, ReadVec3, ext, hasExt);
            }
        }

        static void ClearBoneTracks(ref M2BoneDef bone)
        {
            bone.Translation = EmptyTrack<WowVec3>();
            bone.Rotation = EmptyTrack<WowQuat>();
            bone.Scale = EmptyTrack<WowVec3>();
        }

        delegate T ReadValue<T>(ByteCursor c);

        static WowVec3 ReadVec3(ByteCursor c)
        {
            return new WowVec3(c.ReadSingle(), c.ReadSingle(), c.ReadSingle());
        }

        /// <summary>
        /// A rotation as the file stores it: four int16 in x, y, z, w order, each mapping the
        /// 16-bit range onto [-1, 1]. The halves are offset by one either side of zero, which is
        /// how the legacy viewport unpacks them too (Quat16ToQuat32).
        /// </summary>
        static WowQuat ReadPackedQuat(ByteCursor c)
        {
            float x = UnpackQuatComponent(c.ReadInt16());
            float y = UnpackQuatComponent(c.ReadInt16());
            float z = UnpackQuatComponent(c.ReadInt16());
            float w = UnpackQuatComponent(c.ReadInt16());
            return new WowQuat(x, y, z, w);
        }

        static float UnpackQuatComponent(short v)
        {
            return (v < 0 ? v + 32768 : v - 32767) / 32767f;
        }

        /// <summary>
        /// Read one animation track, narrowed to a single sequence.
        ///
        /// The header is interpolation(2), globalSequence(2), then two M2Arrays: timestamps and
        /// values. Each of those is an array of ARRAYS -- one per animation sequence -- so the
        /// keys for sequence n are found by taking entry n of both. A track bound to a global
        /// sequence is read at entry 0 instead, whatever is playing, which is what the legacy
        /// evaluator does with it (Animated::getValue forces the index to 0 for those).
        ///
        /// Anything malformed produces an empty track rather than an exception: a bone that will
        /// not move is a far better outcome than a model that will not load, and the caller
        /// already treats an empty track as "hold the rest pose".
        /// </summary>
        static M2Track<T> ReadTrack<T>(ByteCursor c, int offset, int sequence, int valueStride,
                                       ReadValue<T> readValue, ByteCursor ext, bool hasExt)
        {
            M2Track<T> track = new M2Track<T>();
            track.Times = EmptyTimes;
            track.Values = new T[0];

            c.Seek(offset);
            track.Interpolation = (M2Interpolation)c.ReadInt16();
            track.GlobalSequence = c.ReadInt16();
            M2Array times = c.ReadArray();
            M2Array values = c.ReadArray();

            // A global-sequence track keeps its keys at entry 0, whichever animation is playing.
            int entry = track.IsGlobal ? 0 : sequence;
            if (entry >= times.Count || entry >= values.Count)
                return track;

            // Each entry is itself an M2Array: count then offset.
            const int NestedStride = 8;
            if (!Fits(c, times.Offset, times.Count, NestedStride) ||
                !Fits(c, values.Offset, values.Count, NestedStride))
                return track;

            c.Seek(times.Offset + entry * NestedStride);
            M2Array keyTimes = c.ReadArray();
            c.Seek(values.Offset + entry * NestedStride);
            M2Array keyValues = c.ReadArray();

            // From here the offsets address the KEYFRAME buffer, which is the .anim file when
            // this sequence's keys live outside the .m2, and the .m2 itself otherwise. Everything
            // above -- the track header and the per-sequence arrays -- came from the .m2 in both
            // cases, because that is where they are stored in both cases.
            //
            // The external buffer belongs to the requested sequence and to no other, so a global
            // track -- which reads entry 0 instead -- takes its keys from c unless the request
            // WAS sequence 0.
            ByteCursor k = (hasExt && entry == sequence) ? ext : c;

            int n = keyTimes.Count < keyValues.Count ? keyTimes.Count : keyValues.Count;
            if (n <= 0 || !Fits(k, keyTimes.Offset, n, 4) || !Fits(k, keyValues.Offset, n, valueStride))
                return track;

            // Hermite and Bezier store three values per key (the value and two tangents); this
            // milestone reads the value and interpolates it linearly, which is what those degrade
            // to without their tangents. No bone track in the validation data uses either.
            int stride = (track.Interpolation == M2Interpolation.Hermite ||
                          track.Interpolation == M2Interpolation.Bezier) ? valueStride * 3 : valueStride;
            if (!Fits(k, keyValues.Offset, n, stride))
                return track;

            var t = new uint[n];
            var v = new T[n];
            try
            {
                for (int i = 0; i < n; i++)
                {
                    k.Seek(keyTimes.Offset + i * 4);
                    t[i] = k.ReadUInt32();
                    k.Seek(keyValues.Offset + i * stride);
                    v[i] = readValue(k);
                }
            }
            catch (WowParseException)
            {
                // Bounds were fine and the contents were not -- a sequence whose keys really live
                // in another file, read here anyway. One bone that will not move beats a model
                // that will not load; the sequence-level check above is what should have caught
                // this, and the caller reports when it did.
                return track;
            }
            track.Times = t;
            track.Values = v;
            return track;
        }

        static readonly uint[] EmptyTimes = new uint[0];

        // -----------------------------------------------------------------------------------
        // Particle and ribbon emitters
        // -----------------------------------------------------------------------------------

        /// <summary>
        /// Read both emitter arrays, with every track narrowed to <paramref name="sequence"/>.
        ///
        /// Called from Parse AND from ReadAnimationInto, because an emitter's tracks are stored
        /// per animation sequence exactly like a bone's. See M2ParsedModel.ParticleEmitters for
        /// the model that makes the difference visible.
        ///
        /// Nothing in here throws. A header too short to carry the arrays, a count that does not
        /// fit, an emitter whose bone points off the end -- each yields fewer emitters, never a
        /// model that refuses to load. Emitters are decoration; geometry is not.
        /// </summary>
        /// <summary>
        /// EMITTER TRACKS ARE NEVER IN A .anim FILE, so no external cursor is threaded through
        /// any of this. The legacy binds them to the GameFile* overload of Animated::init
        /// (particle.cpp:33-43 and :744-747, animated.h:239), which reads the model buffer and
        /// only the model buffer; the .anim-redirecting overload (animated.h:298) has no emitter
        /// call site anywhere. Pointing an M2-relative offset at the .anim buffer would read
        /// whatever happened to be at that offset in the other file.
        /// </summary>
        static readonly ByteCursor NoExternalKeys = default(ByteCursor);

        static void ReadEmitters(ByteCursor c, M2ParsedModel model, int sequence)
        {
            model.ParticleEmitters = new M2ParticleEmitterDef[0];
            model.RibbonEmitters = new M2RibbonEmitterDef[0];
            model.ParticleEmitterCount = 0;
            model.RibbonEmitterCount = 0;
            if (c.Length < EmitterHeaderSize)
                return;

            c.Seek(OfsParticleEmitters);
            M2Array particles = c.ReadArray();
            model.ParticleEmitterCount = particles.Count;
            // An array cannot begin inside the header, so an offset that does is a sign these
            // bytes are not the emitter entry at all -- the header stops earlier and something
            // else lives at 0x128. Only meaningful for a NON-EMPTY array: an M2 with no particle
            // emitters stores count 0 AND offset 0, which is the common case and says nothing
            // about the ribbons that may still follow.
            if (particles.Count > 0 && particles.Offset < HeaderSize)
                particles = new M2Array(0, 0);
            model.ParticleEmitterCount = particles.Count;
            if (particles.Count > 0 && Fits(c, particles.Offset, particles.Count, ParticleEmitterStride))
            {
                var kept = new List<M2ParticleEmitterDef>(particles.Count);
                for (int i = 0; i < particles.Count; i++)
                {
                    M2ParticleEmitterDef def;
                    if (ReadParticleEmitter(c, particles.Offset + i * ParticleEmitterStride, model,
                                            sequence, out def))
                        kept.Add(def);
                }
                model.ParticleEmitters = kept.ToArray();
            }

            c.Seek(OfsRibbonEmitters);
            M2Array ribbons = c.ReadArray();
            model.RibbonEmitterCount = ribbons.Count;
            if (ribbons.Count > 0 && ribbons.Offset < HeaderSize)
                ribbons = new M2Array(0, 0);
            model.RibbonEmitterCount = ribbons.Count;
            if (ribbons.Count > 0 && Fits(c, ribbons.Offset, ribbons.Count, RibbonEmitterStride))
            {
                var kept = new List<M2RibbonEmitterDef>(ribbons.Count);
                for (int i = 0; i < ribbons.Count; i++)
                {
                    M2RibbonEmitterDef def;
                    if (ReadRibbonEmitter(c, ribbons.Offset + i * RibbonEmitterStride, model,
                                          sequence, out def))
                        kept.Add(def);
                }
                model.RibbonEmitters = kept.ToArray();
            }
        }

        static bool ReadParticleEmitter(ByteCursor c, int at, M2ParsedModel model, int sequence,
                                        out M2ParticleEmitterDef def)
        {
            def = new M2ParticleEmitterDef();
            try
            {
                c.Seek(at + 4);
                def.Flags = c.ReadInt32();

                c.Seek(at + OfsParticlePos);
                def.Position = ReadVec3(c);

                c.Seek(at + OfsParticleBone);
                def.Bone = c.ReadInt16();
                def.TextureId = c.ReadInt16();

                c.Seek(at + OfsParticleBlend);
                def.BlendMode = c.ReadByte();
                def.EmitterType = c.ReadByte();
                def.ParticleColorIndex = c.ReadUInt16();

                c.Seek(at + OfsParticleTileRotation);
                def.TextureTileRotation = c.ReadInt16();
                def.Rows = c.ReadUInt16();
                def.Cols = c.ReadUInt16();

                // rows/cols of 0 would make the flipbook a division by zero. The legacy runtime
                // promotes both to 1 (particle.cpp:66-70); so does this.
                if (def.Rows == 0) def.Rows = 1;
                if (def.Cols == 0) def.Cols = 1;

                // 0xFFFF appears on three emitters in the client and is not one of the three
                // override slots. Treat it as "no override" rather than letting it index.
                if (def.ParticleColorIndex < 11 || def.ParticleColorIndex > 13)
                    def.ParticleColorIndex = 0;
            }
            catch (WowParseException)
            {
                return false;
            }

            if (!ParticleEmitterLooksReal(def, model))
                return false;

            // ---- WHICH SEQUENCE THE EMITTER'S OWN TRACKS ARE READ AT --------------------
            //
            // ANIMATION 0, always -- not the sequence that is playing. That is the app's shipped
            // behaviour, not an invention: GLOBALSETTINGS.bZeroParticle defaults to true
            // (app.cpp:969, modelviewer.cpp:812, and it is a checkbox in the general settings),
            // and the legacy runtime then forces the animation index to 0 for every one of these
            // ten tracks and for a particle's lifespan at spawn (particle.cpp:194-196, :615-617,
            // :724-726). EnabledIn is the one exception -- it is read at the PLAYING animation
            // even then (particle.cpp:233-234) -- and it is read that way below.
            //
            // It matters, and not rarely: 122 of the 806 emitters in a 900-model sample have no
            // EmissionRate keys in the sequence that resolves, and every one of them would emit
            // nothing at all if this followed the sequence.
            const int ParticleTrackSequence = 0;
            for (int k = 0; k < ParticleTrackOffsets.Length; k++)
            {
                int off = at + ParticleTrackOffsets[k];
                M2Track<float> t;
                if (k == 4)   // Gravity, whose key encoding depends on a flag
                    t = def.CompressedGravity
                        ? ReadTrack<float>(c, off, ParticleTrackSequence, 4, ReadCompressedGravity, NoExternalKeys, false)
                        : ReadTrack<float>(c, off, ParticleTrackSequence, 4, ReadFloat, NoExternalKeys, false);
                else
                    t = ReadTrack<float>(c, off, ParticleTrackSequence, 4, ReadFloat, NoExternalKeys, false);

                switch (k)
                {
                    case 0: def.EmissionSpeed = t; break;
                    case 1: def.SpeedVariation = t; break;
                    case 2: def.VerticalRange = t; break;
                    case 3: def.HorizontalRange = t; break;
                    case 4: def.Gravity = t; break;
                    case 5: def.Lifespan = t; break;
                    case 6: def.EmissionRate = t; break;
                    case 7: def.EmissionAreaLength = t; break;
                    case 8: def.EmissionAreaWidth = t; break;
                    default: def.ZSource = t; break;
                }
            }
            // ONE byte per key, not two -- see M2ParticleEmitterDef.EnabledIn. Read at the
            // PLAYING sequence, which is the one track above that is (particle.cpp:233-234).
            def.EnabledIn = ReadTrack<float>(c, at + OfsParticleEnabledIn, sequence, 1,
                                             ReadByteAsFloat, NoExternalKeys, false);

            ReadParticleRamps(c, at + OfsParticleParams, ref def);
            return true;
        }

        /// <summary>
        /// Are these bytes really an emitter?
        ///
        /// Every one of these is a field whose range the format fixes, and together they are what
        /// made the 492-byte stride measurable in the first place: of eight candidate strides,
        /// only 492 satisfied all of them on all 978 multi-emitter models of a 6,000-model sample.
        /// So they are not defensive noise -- they are the same test, kept, and they are what
        /// stands between a model whose header stops short of 0x138 and thirteen invented
        /// emitters welded to its origin.
        ///
        /// An emitter whose BONE points off the end would additionally be a wild read every
        /// frame. The legacy clamps that one to the root bone and logs (particle.cpp:78-87);
        /// dropping it is the better answer here, because a stray emitter at the model's origin
        /// is a visible artefact and this renderer would rather show nothing than something wrong.
        /// </summary>
        static bool ParticleEmitterLooksReal(M2ParticleEmitterDef def, M2ParsedModel model)
        {
            if (model.BoneCount > 0 && (def.Bone < 0 || def.Bone >= model.BoneCount))
                return false;
            if (def.EmitterType < 1 || def.EmitterType > 3)
                return false;
            if (def.BlendMode > 7)
                return false;
            if (def.Rows > 64 || def.Cols > 64)
                return false;
            return true;
        }

        /// <summary>
        /// The colour / alpha / size ramps, and the two scalars beside them.
        ///
        /// See M2ParticleEmitterDef.ColorTimes for why these are variable-length with real times
        /// rather than the legacy's three fixed stops at 0 / 0.5 / 1.
        /// </summary>
        static void ReadParticleRamps(ByteCursor c, int p, ref M2ParticleEmitterDef def)
        {
            def.ColorTimes = new float[0];
            def.ColorKeys = new WowVec3[0];
            def.AlphaTimes = new float[0];
            def.AlphaKeys = new float[0];
            def.SizeTimes = new float[0];
            def.SizeKeys = new WowVec2[0];
            def.ParticleScale = new WowVec2(1f, 1f);

            try
            {
                // scales: a per-AXIS multiplier applied to every stop, NOT one per stop. See
                // M2ParticleEmitterDef.ParticleScale for what the legacy's reading does to a real
                // emitter. A zero would erase the emitter, so it is treated as unset.
                c.Seek(p + OfsParamsScales);
                float sx = c.ReadSingle();
                float sy = c.ReadSingle();
                if (IsFinite(sx) && IsFinite(sy) && sx != 0f && sy != 0f)
                    def.ParticleScale = new WowVec2(sx, sy);

                c.Seek(p + OfsParamsSlowdown);
                float slow = c.ReadSingle();
                def.Slowdown = IsFinite(slow) ? slow : 0f;

                c.Seek(p + OfsParamsTailLength);
                float tail = c.ReadSingle();
                def.TailLength = IsFinite(tail) && tail > 0f ? tail : 0f;

                c.Seek(p + OfsParamsBaseSpin);
                float baseSpin = c.ReadSingle();
                float baseSpinVariation = c.ReadSingle();
                def.BaseSpin = IsFinite(baseSpin) ? baseSpin : 0f;
                def.BaseSpinVariation = IsFinite(baseSpinVariation) ? Math.Abs(baseSpinVariation) : 0f;

                c.Seek(p + OfsParamsRotation);
                float rot = c.ReadSingle();
                def.SpriteRotation = IsFinite(rot) ? rot : 0f;

                def.ColorKeys = ReadRamp<WowVec3>(c, p + OfsParamsColor, 12, ReadColor255,
                                                  out def.ColorTimes);
                def.AlphaKeys = ReadRamp<float>(c, p + OfsParamsAlpha, 2, ReadFixed16,
                                                out def.AlphaTimes);
                def.SizeKeys = ReadRamp<WowVec2>(c, p + OfsParamsSize, 8, ReadVec2,
                                                 out def.SizeTimes);
            }
            catch (WowParseException)
            {
                // Keep what was read. An empty ramp means "hold the neutral value", which the
                // runtime reads as white, opaque and unit-sized.
            }

            for (int i = 0; i < def.SizeKeys.Length; i++)
                def.SizeKeys[i] = new WowVec2(def.SizeKeys[i].X * def.ParticleScale.X,
                                              def.SizeKeys[i].Y * def.ParticleScale.Y);
        }

        /// <summary>
        /// Read one ramp: its normalised times and its values.
        ///
        /// The times are UINT16 over 32767, not milliseconds. Across 17,202 ramp tracks the first
        /// key is 0 and the last is 32767 in 100.0 % of them and every one is monotonic; read as
        /// uint32 milliseconds, 0.0 % are either. A times array that cannot be read, or that does
        /// not ascend, falls back to stops spread evenly over the life -- the best available
        /// reading of values whose positions are unknown, and exactly right for the two- and
        /// three-stop cases that are most of the client.
        /// </summary>
        static T[] ReadRamp<T>(ByteCursor c, int blockAt, int valueStride, ReadValue<T> read,
                               out float[] times)
        {
            times = new float[0];
            c.Seek(blockAt);
            int timeCount = (int)c.ReadUInt32();
            int timeOffset = (int)c.ReadUInt32();
            int count = (int)c.ReadUInt32();
            int offset = (int)c.ReadUInt32();
            if (count <= 0 || !Fits(c, offset, count, valueStride))
                return new T[0];
            int n = count < MaxRampStops ? count : MaxRampStops;

            var values = new T[n];
            for (int i = 0; i < n; i++)
            {
                c.Seek(offset + i * valueStride);
                values[i] = read(c);
            }

            var t = new float[n];
            bool haveTimes = timeCount >= count && Fits(c, timeOffset, timeCount, 2);
            if (haveTimes)
            {
                for (int i = 0; i < n; i++)
                {
                    c.Seek(timeOffset + i * 2);
                    t[i] = c.ReadUInt16() / 32767f;
                }
                for (int i = 1; i < n && haveTimes; i++)
                    if (t[i] < t[i - 1])
                        haveTimes = false;
            }
            if (!haveTimes)
                for (int i = 0; i < n; i++)
                    t[i] = n > 1 ? i / (float)(n - 1) : 0f;
            times = t;
            return values;
        }

        /// <summary>A ramp colour stop: three floats the file stores as 0..255.</summary>
        static WowVec3 ReadColor255(ByteCursor c)
        {
            float r = c.ReadSingle();
            float g = c.ReadSingle();
            float b = c.ReadSingle();
            return new WowVec3(r / 255f, g / 255f, b / 255f);
        }

        /// <summary>A ramp size stop: the x and y scale of the particle's quad.</summary>
        static WowVec2 ReadVec2(ByteCursor c)
        {
            float x = c.ReadSingle();
            float y = c.ReadSingle();
            return new WowVec2(IsFinite(x) ? x : 0f, IsFinite(y) ? y : 0f);
        }

        static float ReadFloat(ByteCursor c)
        {
            return Finite(c.ReadSingle());
        }

        static float Finite(float v) { return IsFinite(v) ? v : 0f; }

        static float ReadByteAsFloat(ByteCursor c) { return c.ReadByte(); }

        /// <summary>
        /// A Gravity key when the emitter sets flag 0x800000, which 82.9 % of the client's
        /// emitters do.
        ///
        /// WHAT IS MEASURED, AND WHAT IS NOT. The key is FOUR bytes either way -- the packing gap
        /// between consecutive key arrays comes out at 4 bytes per key, so the flag changes the
        /// ENCODING and not the stride, and every track offset around it is unaffected. Read as
        /// float32, which is what the legacy runtime does for every emitter unconditionally
        /// (particle.cpp:38), the keys decode to NaN or infinity 47 % of the time and to |v| &gt;
        /// 1e6 65 % of the time: that reading is definitively wrong, whatever the right one is.
        /// Read as two int16, the second divided by 32767 -- the fixed16 convention this format
        /// uses everywhere else -- the values come out as clean authored decimals (-0.017, 0.005,
        /// 0.010, -0.080) and span roughly [-0.08, +0.01] between the 1st and 99th percentiles.
        ///
        /// WHAT IS NOT SETTLED is the scale of that number against the PLAIN float gravity, which
        /// measures as N/36 for small integer N (0.694, 6.944, 41.667). The two are clean in
        /// different units and no file in this repository relates them. So this decodes the form
        /// it can defend and accepts that the magnitude may be conservative: a gentle drift is
        /// wrong by a factor, whereas the float32 reading is wrong by infinity.
        /// </summary>
        static float ReadCompressedGravity(ByteCursor c)
        {
            c.ReadInt16();                       // see above: zero on 94 % of keys, unidentified
            return c.ReadInt16() / 32767f;
        }

        static bool IsFinite(float v)
        {
            return !float.IsNaN(v) && !float.IsInfinity(v);
        }

        static bool ReadRibbonEmitter(ByteCursor c, int at, M2ParsedModel model, int sequence,
                                      out M2RibbonEmitterDef def)
        {
            def = new M2RibbonEmitterDef();
            def.TextureIds = new int[0];
            try
            {
                c.Seek(at + 4);
                def.Bone = c.ReadInt32();

                c.Seek(at + OfsRibbonPos);
                def.Position = ReadVec3(c);

                // UINT16 per entry, not int32. Measured: read as uint16 every index is inside
                // the model's texture array on 322 of 322 multi-texture ribbons; read as uint32,
                // only 207 of 322. spells/11fx_infusiontether_aura.m2 stores 21 00 22 00 23 00 --
                // [33, 34, 35] against 36 textures. particle.cpp:753 makes the same four-byte
                // mistake ("int *texlist") and gets away with it only because a one-entry array
                // is padded with zeroes; 48.8 % of the client's ribbons have three textures.
                c.Seek(at + OfsRibbonTextures);
                M2Array textures = c.ReadArray();
                if (textures.Count > 0 && Fits(c, textures.Offset, textures.Count, 2))
                {
                    var ids = new List<int>(textures.Count);
                    for (int i = 0; i < textures.Count; i++)
                    {
                        c.Seek(textures.Offset + i * 2);
                        int id = c.ReadUInt16();
                        if (id >= 0 && id < model.Textures.Length)
                            ids.Add(id);
                    }
                    def.TextureIds = ids.ToArray();
                }

                // The ribbon's material, from the SECOND array -- uint16 indices into the
                // materials table at header offset 0x70. Not parallel to the textures above, and
                // one entry on every ribbon measured.
                def.MaterialIndex = -1;
                c.Seek(at + OfsRibbonMaterials);
                M2Array materials = c.ReadArray();
                if (materials.Count > 0 && Fits(c, materials.Offset, materials.Count, 2))
                {
                    c.Seek(materials.Offset);
                    int m = c.ReadUInt16();
                    if (m >= 0 && m < model.Materials.Length)
                        def.MaterialIndex = m;
                }

                // ByteCursor is a STRUCT. Reading through a helper that takes it by value
                // would leave this cursor where it was and read the same float three times, so
                // these go through the cursor itself and are sanitised afterwards.
                c.Seek(at + OfsRibbonRes);
                def.EdgesPerSecond = Finite(c.ReadSingle());
                def.EdgeLifetimeSeconds = Finite(c.ReadSingle());
                def.EmissionAngle = Finite(c.ReadSingle());
            }
            catch (WowParseException)
            {
                return false;
            }

            // Same reasoning as ParticleEmitterLooksReal: a ribbon with no usable texture, or
            // one bound to a bone that does not exist, is not a ribbon.
            if (model.BoneCount > 0 && (def.Bone < 0 || def.Bone >= model.BoneCount))
                return false;
            if (def.TextureIds.Length == 0)
                return false;

            def.Color = ReadTrack<WowVec3>(c, at + OfsRibbonColor, sequence, Vec3Stride,
                                           ReadVec3, NoExternalKeys, false);
            def.Opacity = ReadTrack<float>(c, at + OfsRibbonColor + TrackStride, sequence,
                                           Fixed16Stride, ReadFixed16, NoExternalKeys, false);
            def.Above = ReadTrack<float>(c, at + OfsRibbonColor + 2 * TrackStride, sequence,
                                         4, ReadFloat, NoExternalKeys, false);
            def.Below = ReadTrack<float>(c, at + OfsRibbonColor + 3 * TrackStride, sequence,
                                         4, ReadFloat, NoExternalKeys, false);
            return true;
        }

        /// <summary>Does an array of count*stride bytes at offset lie inside the payload?</summary>
        static bool Fits(ByteCursor c, int offset, int count, int stride)
        {
            if (offset < 0 || count < 0 || stride <= 0)
                return false;
            long end = (long)offset + (long)count * stride;
            return end <= c.Length;
        }

        static M2Sequence[] ReadSequences(ByteCursor c, M2Array arr)
        {
            var seqs = new M2Sequence[arr.Count];
            for (int i = 0; i < arr.Count; i++)
            {
                c.Seek(arr.Offset + i * SequenceStride);
                seqs[i].AnimId = c.ReadInt16();
                seqs[i].SubAnimId = c.ReadInt16();
                seqs[i].Length = c.ReadUInt32();
                c.ReadSingle();                     // moveSpeed
                seqs[i].Flags = c.ReadUInt32();
                c.Seek(arr.Offset + i * SequenceStride + OfsSequenceAliasNext);
                seqs[i].AliasNext = c.ReadInt16();
            }
            return seqs;
        }

        /// <summary>
        /// Which sequence to parse bone tracks for.
        ///
        /// With no request (-1) this is the model's default idle, by the legacy viewport's own
        /// rule (AnimControl::UpdateModel): the FIRST sequence whose AnimId is "Stand", falling
        /// back to sequence 0 when the model has none. It is not the same as "sequence 0" -- on
        /// chicken2 that is a run cycle and the idle is sequence 2.
        ///
        /// A requested sequence that cannot be played falls back to the idle rather than to
        /// nothing: the app is showing SOMETHING, and a still model is a worse answer than the
        /// wrong-but-moving one. The reason is reported either way.
        ///
        /// A sequence whose keyframes live in a separate .anim file, named by an AFID entry for
        /// its (AnimId, SubAnimId), is refused: the offsets inside its track headers address that
        /// file, not this one, and following them here would read whatever happens to sit at those
        /// bytes of the model. Fetching .anim files is a later milestone.
        /// </summary>
        static int ResolveSequence(M2ParsedModel model, byte[] externalAnim, int wanted,
                                   out string skipReason)
        {
            skipReason = null;
            if (model.Sequences.Length == 0)
            {
                skipReason = "it declares no animation sequences";
                return -1;
            }

            if (wanted >= 0)
            {
                string why = "there is no such sequence";
                if (wanted < model.Sequences.Length &&
                    Playable(model, wanted, externalAnim != null, out why))
                    return wanted;
                skipReason = "the selected sequence " + wanted + " cannot be played (" + why +
                             "); falling back to the default idle";
            }

            int idle = 0;
            for (int i = 0; i < model.Sequences.Length; i++)
            {
                if (model.Sequences[i].AnimId == AnimIdStand)
                {
                    idle = i;
                    break;
                }
            }

            string idleWhy;
            if (!Playable(model, idle, false, out idleWhy))
            {
                skipReason = (skipReason == null ? "" : skipReason + "; and ") +
                             "the default idle cannot be played either (" + idleWhy + ")";
                return -1;
            }
            return idle;
        }

        /// <summary>
        /// Can this renderer play this sequence at all, and if not, why not?
        ///
        /// The 0x20 flag is the test that matters: it says the keyframes are in this file. Without
        /// it the track headers still exist here, but their offsets address the sequence's .anim
        /// file -- and they routinely land inside this buffer by coincidence, so bounds-checking
        /// them proves nothing and reading them yields noise. The AFID lookup is kept as well
        /// because it can name the file the keys went to, which makes a better message.
        /// </summary>
        /// <summary>
        /// Can this sequence be played from what we hold? In-file sequences always can. One whose
        /// keys live in a .anim can too, but only once those bytes have been fetched -- which is
        /// why haveExternal is an argument rather than something guessed here.
        ///
        /// An alias (0x40) is judged by the sequence its chain ends on, because that is where its
        /// keys are: the flag and the .anim file that matter are the target's, while the length
        /// stays the alias's own.
        /// </summary>
        static bool Playable(M2ParsedModel model, int index, bool haveExternal, out string why)
        {
            M2Sequence seq = model.Sequences[index];
            if (seq.Length == 0)
            {
                why = "it has zero length";
                return false;
            }
            int keys = AliasTarget(model, index);
            M2Sequence keySeq = model.Sequences[keys >= 0 ? keys : index];
            if (!keySeq.PrimarySequence)
            {
                if (haveExternal)
                {
                    why = null;
                    return true;
                }
                if (keys < 0)
                    why = "it is an alias (0x40 flag) whose chain never reaches a sequence with keyframes " +
                          "(it loops, leaves the sequence table, or ends on a sequence with none)";
                else if (keys != index)
                    why = "it is an alias of sequence " + keys + ", whose keyframes are in a separate .anim " +
                          "file that has not been fetched yet";
                else
                    why = HasAfidFor(model.AnimFileIds, seq)
                        ? "its keyframes are in a separate .anim file that has not been fetched yet"
                        : "its keyframes are not stored in the .m2 (no 0x20 flag) and no .anim file is named for it";
                return false;
            }
            why = null;
            return true;
        }

        /// <summary>
        /// The sequence whose keyframes sequence `index` plays: the sequence itself when it is
        /// primary (0x20), has an AFID entry of its own, or is not an alias; otherwise the first
        /// sequence along its 0x40 alias chain that is primary or has an AFID entry. -1 when an
        /// alias's chain breaks -- an index outside the table, a loop, or a sequence that neither
        /// holds keys nor aliases further -- and index itself when index is out of range.
        ///
        /// Reading the ALIAS's per-sequence track entries against the target's keys is sound
        /// because the format stores them identical: in the retail skeletons 2137789 (humanfemale),
        /// 2138400 and 1685880 every alias of this kind has entries byte-identical to its target's
        /// in every bone track. So only the key BUFFER changes -- the target's .anim, or the
        /// .m2/.skel when the target is primary -- and the entries are still read at `index`.
        ///
        /// The legacy viewport has no alias handling (readAnimsFromFile keys its .anim map by the
        /// sequence's own animID, and animated.h finds nothing for an alias), so it reads these
        /// sequences' .anim offsets against the skeleton and does not play them either.
        /// </summary>
        static int AliasTarget(M2ParsedModel model, int index)
        {
            M2Sequence[] sequences = model.Sequences;
            if (index < 0 || index >= sequences.Length)
                return index;
            if (model.AliasTargets == null || !ReferenceEquals(model.AliasTargetsSequences, sequences) ||
                !ReferenceEquals(model.AliasTargetsAnimFileIds, model.AnimFileIds))
            {
                var withFile = new HashSet<long>();
                foreach (AfidEntry e in model.AnimFileIds)
                    withFile.Add(AfidKey(e.AnimId, e.SubAnimId));
                var targets = new int[sequences.Length];
                for (int i = 0; i < sequences.Length; i++)
                    targets[i] = WalkAliasChain(sequences, withFile, i);
                model.AliasTargets = targets;
                model.AliasTargetsSequences = sequences;
                model.AliasTargetsAnimFileIds = model.AnimFileIds;
            }
            return model.AliasTargets[index];
        }

        static long AfidKey(int animId, int subAnimId)
        {
            return ((long)animId << 32) ^ (uint)subAnimId;
        }

        static int WalkAliasChain(M2Sequence[] sequences, HashSet<long> withFile, int index)
        {
            int at = index;
            // A chain visits each sequence at most once, so more steps than the table has entries
            // can only mean it loops.
            for (int steps = 0; steps <= sequences.Length; steps++)
            {
                M2Sequence s = sequences[at];
                if (s.PrimarySequence || withFile.Contains(AfidKey(s.AnimId, s.SubAnimId)))
                    return at;
                if (!s.Alias)
                    return at == index ? index : -1;
                int next = s.AliasNext;
                if (next < 0 || next >= sequences.Length)
                    return -1;
                at = next;
            }
            return -1;
        }

        /// <summary>The sequence whose flag and AFID entry decide where index's keys are read from:
        /// its alias target, or itself when it is not an alias or its chain is broken.</summary>
        static M2Sequence KeySequenceOf(M2ParsedModel model, int index)
        {
            int keys = AliasTarget(model, index);
            return model.Sequences[keys >= 0 ? keys : index];
        }

        static bool HasAfidFor(AfidEntry[] afids, M2Sequence seq)
        {
            foreach (AfidEntry e in afids)
                if (e.AnimId == seq.AnimId && e.SubAnimId == seq.SubAnimId)
                    return true;
            return false;
        }

        /// <summary>Does the AFID chunk name an .anim file for this sequence?</summary>
        /// <summary>
        /// Read AFID: animId(2) subAnimId(2) fileId(4) per entry. Empty when the chunk is absent,
        /// which is every unchunked and most older models.
        ///
        /// An entry whose fileId is 0 names no file and is dropped, as the host's
        /// readAFIDSFromFile drops it. Retail creatures carry one for many alias sequences
        /// (ladyalexstrasa2's sequence 53 aliases 52, whose entry names 575107), and keeping it
        /// would make the alias look like it holds its own keys, so AliasTarget would stop there
        /// and no .anim would ever be fetched for it.
        /// </summary>
        static AfidEntry[] ReadAnimFileIds(byte[] file, Dictionary<string, M2Array> chunks)
        {
            M2Array afid;
            if (!chunks.TryGetValue("AFID", out afid))
                return EmptyAfid;
            int n = afid.Count / 8;
            if (n <= 0)
                return EmptyAfid;
            var list = new List<AfidEntry>(n);
            for (int i = 0; i < n; i++)
            {
                int o = afid.Offset + i * 8;
                if (o + 8 > file.Length)
                    break;
                AfidEntry e;
                e.AnimId = ReadUInt16At(file, o);
                e.SubAnimId = ReadUInt16At(file, o + 2);
                e.FileDataID = (int)ReadUInt32At(file, o + 4);
                if (e.FileDataID != 0)
                    list.Add(e);
            }
            return list.ToArray();
        }

        static readonly AfidEntry[] EmptyAfid = new AfidEntry[0];

        /// <summary>
        /// The external .anim FileDataID holding this sequence's keyframes, or 0 when they are in
        /// the .m2. Matched on animId AND subAnimId, the way the legacy viewport matches them --
        /// two sequences routinely share an animId as sub-animations of one action, and they have
        /// separate .anim files. For an alias (0x40) it is the file of the sequence its chain ends
        /// on (see AliasTarget), which is what makes a switch to one fetch the right bytes.
        /// </summary>
        public static int ExternalAnimFileId(M2ParsedModel model, int sequenceIndex)
        {
            if (model == null || model.AnimFileIds.Length == 0 ||
                sequenceIndex < 0 || sequenceIndex >= model.Sequences.Length)
                return 0;
            // An alias's keys are its target's, so the file is the target's too.
            M2Sequence seq = KeySequenceOf(model, sequenceIndex);
            if (seq.PrimarySequence)
                return 0;                       // its keyframes are in the .m2
            foreach (AfidEntry e in model.AnimFileIds)
                if (e.AnimId == seq.AnimId && e.SubAnimId == seq.SubAnimId)
                    return e.FileDataID;
            return 0;
        }

        static ushort ReadUInt16At(byte[] file, int offset)
        {
            return (ushort)(file[offset] | (file[offset + 1] << 8));
        }

        static uint ReadUInt32At(byte[] file, int offset)
        {
            return (uint)(file[offset] | (file[offset + 1] << 8) |
                          (file[offset + 2] << 16) | (file[offset + 3] << 24));
        }

        static M2TextureDef[] ReadTextures(ByteCursor c, M2Array arr, M2ParsedModel model)
        {
            var textures = new M2TextureDef[arr.Count];
            for (int i = 0; i < arr.Count; i++)
            {
                c.Seek(arr.Offset + i * TextureStride);
                textures[i].Type = c.ReadUInt32();
                textures[i].Flags = c.ReadUInt32();
                M2Array fileName = c.ReadArray();
                textures[i].FileName = ReadString(c, fileName);
                // TXID (when present) is parallel to this array; 0 means "replaceable, ask the host".
                textures[i].FileDataID = (i < model.TextureFileDataIDs.Length) ? model.TextureFileDataIDs[i] : 0;
            }
            return textures;
        }
    }
}
