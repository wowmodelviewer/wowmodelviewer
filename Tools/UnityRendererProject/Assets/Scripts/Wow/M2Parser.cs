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
//   FileDataIDs of related assets: SFID (skin profiles), TXID (textures) and SKID (a separate
//   skeleton file). AFID/BFID/PFID are further file-reference chunks this milestone does not use.
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
        const int MinHeaderSize = 0x84 + 8;

        const int ColorStride = 40;        // two M2Tracks
        const int TrackStride = 20;        // interpolation + globalSeq + two nested M2Arrays

        /// <summary>
        /// Parse an .m2 asset. Throws WowParseException on anything malformed.
        ///
        /// wantedSequence names which animation's bone tracks to read; -1 asks for the model's
        /// default idle. Only one sequence is ever read (a track on disk is an array of
        /// per-sequence keyframe arrays, and a boss has hundreds), so playing a different one
        /// means parsing again with a different answer here -- which is what the renderer does
        /// when the app's animation selection changes.
        /// </summary>
        public static M2ParsedModel Parse(byte[] file, int wantedSequence = -1)
        {
            if (file == null || file.Length < 8)
                throw new WowParseException("m2: asset is empty or too small to hold a header");

            var model = new M2ParsedModel();
            var chunks = ReadChunks(file);

            int md21Offset = 0, md21Size = file.Length;
            if (chunks.Count > 0)
            {
                if (!chunks.ContainsKey("MD21"))
                    throw new WowParseException("m2: chunked asset without an MD21 chunk");
                md21Offset = chunks["MD21"].Offset;
                md21Size = chunks["MD21"].Count;

                model.SkinFileDataIDs = ReadIdChunk(file, chunks, "SFID");
                model.TextureFileDataIDs = ReadIdChunk(file, chunks, "TXID");
                int[] skeleton = ReadIdChunk(file, chunks, "SKID");
                model.SkeletonFileDataID = skeleton.Length > 0 ? skeleton[0] : 0;
            }

            // Work on the MD21 payload as its own address space -- offsets inside are relative
            // to it, so a slice keeps every later read honest about its bounds.
            var payload = new byte[md21Size];
            if (md21Offset + md21Size > file.Length)
                throw new WowParseException("m2: MD21 chunk extends past the end of the asset");
            Buffer.BlockCopy(file, md21Offset, payload, 0, md21Size);

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
                model.AnimatedSequence = ResolveSequence(model, file, chunks, wantedSequence,
                                                        out model.AnimationSkipReason);
                model.Bones = ReadBones(c, bones, model);
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

            return model;
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
        static M2BoneDef[] ReadBones(ByteCursor c, M2Array arr, M2ParsedModel model)
        {
            int seq = model.AnimatedSequence;
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
                c.Seek(bone + OfsBonePivot);
                bones[i].Pivot = new WowVec3(c.ReadSingle(), c.ReadSingle(), c.ReadSingle());

                if (seq >= 0)
                {
                    bones[i].Translation = ReadTrack<WowVec3>(c, bone + OfsBoneTranslation, seq,
                                                              Vec3Stride, ReadVec3);
                    bones[i].Rotation = ReadTrack<WowQuat>(c, bone + OfsBoneRotation, seq,
                                                           PackedQuatStride, ReadPackedQuat);
                    bones[i].Scale = ReadTrack<WowVec3>(c, bone + OfsBoneScale, seq,
                                                        Vec3Stride, ReadVec3);
                }
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
                                       ReadValue<T> readValue)
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

            int n = keyTimes.Count < keyValues.Count ? keyTimes.Count : keyValues.Count;
            if (n <= 0 || !Fits(c, keyTimes.Offset, n, 4) || !Fits(c, keyValues.Offset, n, valueStride))
                return track;

            // Hermite and Bezier store three values per key (the value and two tangents); this
            // milestone reads the value and interpolates it linearly, which is what those degrade
            // to without their tangents. No bone track in the validation data uses either.
            int stride = (track.Interpolation == M2Interpolation.Hermite ||
                          track.Interpolation == M2Interpolation.Bezier) ? valueStride * 3 : valueStride;
            if (!Fits(c, keyValues.Offset, n, stride))
                return track;

            var t = new uint[n];
            var v = new T[n];
            try
            {
                for (int i = 0; i < n; i++)
                {
                    c.Seek(keyTimes.Offset + i * 4);
                    t[i] = c.ReadUInt32();
                    c.Seek(keyValues.Offset + i * stride);
                    v[i] = readValue(c);
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
        static int ResolveSequence(M2ParsedModel model, byte[] file,
                                   Dictionary<string, M2Array> chunks, int wanted,
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
                if (wanted < model.Sequences.Length && Playable(model.Sequences[wanted], file, chunks, out why))
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
            if (!Playable(model.Sequences[idle], file, chunks, out idleWhy))
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
        static bool Playable(M2Sequence seq, byte[] file, Dictionary<string, M2Array> chunks,
                             out string why)
        {
            if (seq.Length == 0)
            {
                why = "it has zero length";
                return false;
            }
            if (!seq.PrimarySequence)
            {
                why = HasExternalAnimFile(file, chunks, seq)
                    ? "its keyframes are in a separate .anim file, which this milestone does not fetch"
                    : "its keyframes are not stored in the .m2 (no 0x20 flag)";
                return false;
            }
            why = null;
            return true;
        }

        /// <summary>Does the AFID chunk name an .anim file for this sequence?</summary>
        static bool HasExternalAnimFile(byte[] file, Dictionary<string, M2Array> chunks, M2Sequence seq)
        {
            M2Array afid;
            if (!chunks.TryGetValue("AFID", out afid))
                return false;
            // Each entry is animId(2) subAnimId(2) fileId(4).
            for (int i = 0; i + 8 <= afid.Count; i += 8)
            {
                int o = afid.Offset + i;
                if (o + 4 > file.Length)
                    break;
                if (ReadUInt16At(file, o) == (ushort)seq.AnimId &&
                    ReadUInt16At(file, o + 2) == (ushort)seq.SubAnimId)
                    return true;
            }
            return false;
        }

        static ushort ReadUInt16At(byte[] file, int offset)
        {
            return (ushort)(file[offset] | (file[offset + 1] << 8));
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
