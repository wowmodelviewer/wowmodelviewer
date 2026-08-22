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
        // three animation tracks of 20 bytes each, then the pivot (12). 88 bytes in total, and
        // the pivot -- the only part a bind pose needs -- sits at the end of it.
        const int BoneStride = 88;
        const int OfsBonePivot = 76;

        // MD20 header offsets (relative to the MD21 payload)
        const int OfsName = 0x08;
        const int OfsGlobalFlags = 0x10;
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

        /// <summary>Parse an .m2 asset. Throws WowParseException on anything malformed.</summary>
        public static M2ParsedModel Parse(byte[] file)
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

            c.Seek(OfsBones);
            M2Array bones = c.ReadArray();
            model.BoneCount = bones.Count;
            // A skeleton file overrides the header's bone array wholesale, so reading that array
            // anyway would hand the renderer a rig the vertices are not indexed against. Leave
            // Bones empty and let the caller decide what to do about it.
            if (model.SkeletonFileDataID == 0)
            {
                c.RequireArray(bones, BoneStride, "bones");
                model.Bones = ReadBones(c, bones);
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
        static M2BoneDef[] ReadBones(ByteCursor c, M2Array arr)
        {
            var bones = new M2BoneDef[arr.Count];
            for (int i = 0; i < arr.Count; i++)
            {
                c.Seek(arr.Offset + i * BoneStride);
                bones[i].KeyBoneId = c.ReadInt32();
                bones[i].Flags = c.ReadUInt32();
                short parent = c.ReadInt16();
                bones[i].Parent = (parent >= 0 && parent < arr.Count && parent != i) ? parent : (short)-1;
                bones[i].SubmeshId = c.ReadUInt16();
                c.Seek(arr.Offset + i * BoneStride + OfsBonePivot);
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
