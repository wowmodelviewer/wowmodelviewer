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
//   FileDataIDs of related assets: SFID (skin profiles), TXID (textures), SKID/AFID/BFID/PFID
//   (skeleton/animations/bones/physics -- not used in this milestone).
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

        // MD20 header offsets (relative to the MD21 payload)
        const int OfsName = 0x08;
        const int OfsGlobalFlags = 0x10;
        const int OfsBones = 0x2C;
        const int OfsVertices = 0x3C;
        const int OfsNumSkinProfiles = 0x44;
        const int OfsTextures = 0x50;
        const int OfsMaterials = 0x70;     // "texFlags": render flags + blend mode
        const int OfsTextureLookup = 0x80;
        const int MinHeaderSize = 0x84 + 8;

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
            model.BoneCount = c.ReadArray().Count;

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

            return model;
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
