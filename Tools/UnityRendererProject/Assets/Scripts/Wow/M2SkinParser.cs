// M2SkinParser.cs
//
// Runtime parser for a modern .skin profile (magic "SKIN"), fetched over IPC by the FileDataID
// the M2's SFID chunk carries.
//
// TOPOLOGY -- the part that is easy to get wrong: renderable triangles come from TWO levels of
// indirection, not one.
//
//     triangle index  ->  skin VertexLookup[]  ->  M2 vertex array
//
// The skin's "triangles" array holds indices into the skin's own vertex-lookup table, and that
// table holds indices into the model's vertex array. A submesh's IndexStart/IndexCount are
// positions in the *triangle* array, and its VertexStart/VertexCount are positions in the
// *lookup* array. Resolving one level and stopping produces a scrambled mesh, so both levels
// are validated here and resolved in BuildTriangles.

namespace Wmv.Wow
{
    public static class M2SkinParser
    {
        const int SubmeshStride = 48;
        const int BatchStride = 24;
        const int HeaderSize = 0x30;

        public static M2ParsedSkin Parse(byte[] file)
        {
            if (file == null || file.Length < HeaderSize)
                throw new WowParseException("skin: asset is empty or smaller than the header");

            var c = new ByteCursor(file, "skin");
            string magic = c.ReadMagic();
            if (magic != "SKIN")
                throw new WowParseException("skin: expected a SKIN header, found '" + magic + "'");

            M2Array vertices = c.ReadArray();     // 0x04
            M2Array triangles = c.ReadArray();    // 0x0C
            c.ReadArray();                        // 0x14 bones (not needed for a static mesh)
            M2Array submeshes = c.ReadArray();    // 0x1C
            M2Array batches = c.ReadArray();      // 0x24

            c.RequireArray(vertices, 2, "skin vertex lookup");
            c.RequireArray(triangles, 2, "skin triangles");
            c.RequireArray(submeshes, SubmeshStride, "skin submeshes");
            c.RequireArray(batches, BatchStride, "skin batches");

            if (triangles.Count % 3 != 0)
                throw new WowParseException(string.Format(
                    "skin: triangle index count {0} is not a multiple of 3", triangles.Count));

            var skin = new M2ParsedSkin();

            skin.VertexLookup = new ushort[vertices.Count];
            for (int i = 0; i < vertices.Count; i++)
            {
                c.Seek(vertices.Offset + i * 2);
                skin.VertexLookup[i] = c.ReadUInt16();
            }

            skin.Triangles = new ushort[triangles.Count];
            for (int i = 0; i < triangles.Count; i++)
            {
                c.Seek(triangles.Offset + i * 2);
                ushort idx = c.ReadUInt16();
                if (idx >= skin.VertexLookup.Length)
                    throw new WowParseException(string.Format(
                        "skin: triangle index {0} at position {1} is outside the {2}-entry vertex lookup",
                        idx, i, skin.VertexLookup.Length));
                skin.Triangles[i] = idx;
            }

            skin.Submeshes = new M2Submesh[submeshes.Count];
            for (int i = 0; i < submeshes.Count; i++)
            {
                c.Seek(submeshes.Offset + i * SubmeshStride);
                M2Submesh s;
                s.Id = c.ReadUInt16();
                s.Level = c.ReadUInt16();
                s.VertexStart = c.ReadUInt16();
                s.VertexCount = c.ReadUInt16();
                s.IndexStart = c.ReadUInt16();
                s.IndexCount = c.ReadUInt16();
                // remaining fields (bone counts, centre/sort positions, radius) are not needed here

                if (s.IndexCount % 3 != 0)
                    throw new WowParseException(string.Format(
                        "skin: submesh {0} has {1} indices, not a multiple of 3", i, s.IndexCount));
                if ((long)s.IndexStart + s.IndexCount > skin.Triangles.Length)
                    throw new WowParseException(string.Format(
                        "skin: submesh {0} spans indices [{1},{2}) beyond the {3} available",
                        i, s.IndexStart, s.IndexStart + s.IndexCount, skin.Triangles.Length));
                if ((long)s.VertexStart + s.VertexCount > skin.VertexLookup.Length)
                    throw new WowParseException(string.Format(
                        "skin: submesh {0} spans vertices [{1},{2}) beyond the {3} available",
                        i, s.VertexStart, s.VertexStart + s.VertexCount, skin.VertexLookup.Length));
                skin.Submeshes[i] = s;
            }

            skin.Batches = new M2Batch[batches.Count];
            for (int i = 0; i < batches.Count; i++)
            {
                c.Seek(batches.Offset + i * BatchStride);
                M2Batch b;
                b.Flags = c.ReadByte();
                b.PriorityPlane = c.ReadSByte();
                b.ShaderId = c.ReadUInt16();
                b.SubmeshIndex = c.ReadUInt16();
                b.GeosetIndex = c.ReadUInt16();
                b.ColorIndex = c.ReadUInt16();
                b.MaterialIndex = c.ReadUInt16();
                b.MaterialLayer = c.ReadUInt16();
                b.TextureCount = c.ReadUInt16();
                b.TextureComboIndex = c.ReadUInt16();
                if (b.SubmeshIndex >= skin.Submeshes.Length)
                    throw new WowParseException(string.Format(
                        "skin: batch {0} references submesh {1} of {2}", i, b.SubmeshIndex, skin.Submeshes.Length));
                skin.Batches[i] = b;
            }

            return skin;
        }

        /// <summary>
        /// Resolve one submesh's triangles into indices in the MODEL's vertex array, validating
        /// every hop. Returns indices in WoW winding order; the coordinate converter is what
        /// flips them for Unity.
        /// </summary>
        public static int[] BuildTriangles(M2ParsedSkin skin, M2Submesh submesh, int modelVertexCount)
        {
            var indices = new int[submesh.IndexCount];
            for (int i = 0; i < submesh.IndexCount; i++)
            {
                ushort skinIndex = skin.Triangles[submesh.IndexStart + i];
                int modelIndex = skin.VertexLookup[skinIndex];
                if (modelIndex >= modelVertexCount)
                    throw new WowParseException(string.Format(
                        "skin: vertex lookup entry {0} points at model vertex {1} of {2}",
                        skinIndex, modelIndex, modelVertexCount));
                indices[i] = modelIndex;
            }
            return indices;
        }
    }
}
