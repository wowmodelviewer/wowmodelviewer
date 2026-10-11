// WorldChunks.cs
//
// Shared plumbing for the world-file parsers (WDT, split ADT): the chunk walk, a reader bounded to
// one chunk, the exception a malformed required structure raises, and the issue list that keeps
// a malformed or unsupported OPTIONAL structure from failing the whole file.
//
// Specification: docs/world-viewer/00-audit.md (the audit). Every WDT/ADT/WDL in the four audited
// clients is a flat list of chunks -- a 4-byte tag stored byte-reversed ("REVM" for MVER), a
// little-endian u32 payload size, then the payload -- and walks without a single error (audit
// 4.2, 5.1). Root MCNKs hold sub-chunks after a 128-byte header; tex0/obj0 MCNKs from their
// first byte (audit 5.1).
//
// Pure C#, no UnityEngine: Tests/WorldParserTests.cs runs it outside the editor.
//
// Ownership: nothing here keeps a reference to the caller's byte[] past the call that reads it.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>A required world structure that is malformed: the file cannot be used as the
    /// structure it claims to be. Carries the file context, the chunk tag, the absolute byte
    /// offset and the reason.</summary>
    public sealed class WorldParseException : WowParseException
    {
        public readonly string File;
        public readonly string Chunk;
        public readonly long Offset;
        public readonly string Reason;

        public WorldParseException(string file, string chunk, long offset, string reason)
            : base(string.Format("{0}: {1} at offset 0x{2:X}: {3}", file ?? "world file", chunk ?? "?", offset, reason))
        {
            File = file;
            Chunk = chunk;
            Offset = offset;
            Reason = reason;
        }
    }

    /// <summary>How a parsed structure came out.</summary>
    public enum WorldDataStatus
    {
        /// <summary>Not in the file (or no bytes were supplied for it).</summary>
        Absent = 0,
        /// <summary>Decoded with the layout the audit proved.</summary>
        Decoded,
        /// <summary>Its boundaries are proven and checked, some fields are kept raw (meaning not
        /// established); nothing is invented.</summary>
        PartiallyDecoded,
        /// <summary>Present, but its layout or meaning is not established: listed (tag, offset,
        /// size), not decoded.</summary>
        Unsupported,
        /// <summary>Present but inconsistent with the proven layout; see the issue list.</summary>
        Malformed,
    }

    public enum WorldIssueKind
    {
        /// <summary>An optional structure is inconsistent with its proven layout; it was not
        /// used, the rest of the file was.</summary>
        Malformed,
        /// <summary>A structure whose layout or meaning is not established (kept undecoded).</summary>
        Unsupported,
        /// <summary>Allowed by the layout but never seen in the audited clients (e.g. a rule that
        /// held in 100% of the corpus does not hold here): the data is kept, the claim is flagged.</summary>
        Unexpected,
    }

    /// <summary>A non-fatal finding about one structure, with enough context to cite the bytes.</summary>
    public sealed class WorldIssue
    {
        public readonly WorldIssueKind Kind;
        public readonly string File;
        public readonly string Chunk;
        public readonly long Offset;
        public readonly string Reason;

        public WorldIssue(WorldIssueKind kind, string file, string chunk, long offset, string reason)
        {
            Kind = kind;
            File = file;
            Chunk = chunk;
            Offset = offset;
            Reason = reason;
        }

        public override string ToString()
        {
            return string.Format("{0}: {1}: {2} at offset 0x{3:X}: {4}", Kind, File, Chunk, Offset, Reason);
        }
    }

    /// <summary>One chunk as found: its tag in reading order ("MVER"), where its 8-byte header
    /// starts, where its payload starts, and the payload size -- all absolute offsets into the
    /// buffer it was walked in. Every chunk is listed, decoded or not.</summary>
    public struct WorldChunk
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

    public static class WorldChunkWalker
    {
        /// <summary>
        /// Walk [start, end) of `data` as chunks. Throws WorldParseException for a header that
        /// does not fit, a size that runs past `end`, or a range outside the buffer -- never
        /// reads past `end`. `context` names the container in messages ("file", "MCNK 17").
        /// </summary>
        public static List<WorldChunk> Walk(byte[] data, int start, int end, string file, string context)
        {
            if (data == null)
                throw new WorldParseException(file, context, 0, "no data");
            if (start < 0 || end < start || end > data.Length)
                throw new WorldParseException(file, context, start, string.Format(
                    "range [{0}, {1}) is outside the {2}-byte buffer", start, end, data.Length));
            var list = new List<WorldChunk>();
            long p = start;
            while (p < end)
            {
                if (end - p < 8)
                    throw new WorldParseException(file, context, p, string.Format(
                        "{0} trailing byte(s) cannot hold a chunk header", end - p));
                int i = (int)p;
                string tag = TagAt(data, i);
                uint size = (uint)(data[i + 4] | (data[i + 5] << 8) | (data[i + 6] << 16) | (data[i + 7] << 24));
                long payload = p + 8;
                if (size > end - payload)
                    throw new WorldParseException(file, Printable(tag), p, string.Format(
                        "chunk claims {0} byte(s), only {1} remain in the {2}", size, end - payload, context));
                list.Add(new WorldChunk { Tag = tag, HeaderOffset = i, DataOffset = (int)payload, Size = (int)size });
                p = payload + size;
            }
            return list;
        }

        /// <summary>The tag at `offset`, stored byte-reversed: bytes "REVM" read as "MVER".</summary>
        public static string TagAt(byte[] data, int offset)
        {
            if (data == null || offset < 0 || offset > data.Length - 4)
                throw new WorldParseException(null, "chunk", offset, "a chunk tag at this offset is outside the buffer");
            return new string(new[] { (char)data[offset + 3], (char)data[offset + 2], (char)data[offset + 1], (char)data[offset] });
        }

        /// <summary>The tag, or "chunk" when it holds non-printable bytes (keeps messages readable).</summary>
        public static string Printable(string tag)
        {
            foreach (char ch in tag)
                if (ch < 0x20 || ch > 0x7E)
                    return "chunk";
            return tag;
        }

        public static int Count(List<WorldChunk> chunks, string tag)
        {
            int n = 0;
            for (int i = 0; i < chunks.Count; i++)
                if (chunks[i].Tag == tag)
                    n++;
            return n;
        }

        /// <summary>The first chunk with `tag`, or false, for OPTIONAL chunks: a repeat is reported
        /// as a Malformed issue and ignored (none of the audited files repeats these tags), so it
        /// cannot fail the rest of the file.</summary>
        public static bool FindFirst(List<WorldChunk> chunks, string tag, string file, List<WorldIssue> issues, out WorldChunk found)
        {
            found = default(WorldChunk);
            bool have = false;
            for (int i = 0; i < chunks.Count; i++)
            {
                if (chunks[i].Tag != tag)
                    continue;
                if (have)
                {
                    issues.Add(new WorldIssue(WorldIssueKind.Malformed, file, tag, chunks[i].HeaderOffset,
                                              "appears more than once (never in the audited files); the first copy is used"));
                    continue;
                }
                found = chunks[i];
                have = true;
            }
            return have;
        }

        /// <summary>The only chunk with `tag`, or false, for REQUIRED chunks. A second one is a
        /// WorldParseException: none of the audited structures repeats these tags, so a repeat is
        /// not guessed at.</summary>
        public static bool FindSingle(List<WorldChunk> chunks, string tag, string file, out WorldChunk found)
        {
            found = default(WorldChunk);
            bool have = false;
            for (int i = 0; i < chunks.Count; i++)
            {
                if (chunks[i].Tag != tag)
                    continue;
                if (have)
                    throw new WorldParseException(file, tag, chunks[i].HeaderOffset, "appears more than once");
                found = chunks[i];
                have = true;
            }
            return have;
        }
    }

    /// <summary>Little-endian reads bounded to one chunk's payload. Offsets are relative to the
    /// payload; every read is checked against the chunk (not just the buffer), and a failure names
    /// the file, the chunk, and the absolute offset.</summary>
    public struct WorldSpan
    {
        readonly byte[] d;
        readonly int start;
        readonly int size;
        readonly string file;
        readonly string tag;

        public WorldSpan(byte[] data, WorldChunk c, string file)
            : this(data, c.DataOffset, c.Size, file, c.Tag)
        {
        }

        public WorldSpan(byte[] data, int start, int size, string file, string tag)
        {
            if (data == null || start < 0 || size < 0 || start > data.Length || size > data.Length - start)
                throw new WorldParseException(file, tag, start, "chunk extends past the end of the buffer");
            d = data;
            this.start = start;
            this.size = size;
            this.file = file;
            this.tag = tag;
        }

        public int Size { get { return size; } }
        public int Start { get { return start; } }
        public string Tag { get { return tag; } }

        public void Require(int offset, int count)
        {
            if (offset < 0 || count < 0 || offset > size || count > size - offset)
                throw new WorldParseException(file, tag, (long)start + offset, string.Format(
                    "read of {0} byte(s) at +0x{1:X} is outside the {2}-byte chunk", count, offset, size));
        }

        /// <summary>Number of whole `recordSize`-byte records; throws when the size is not a
        /// multiple (an inconsistent array size).</summary>
        public int RecordCount(int recordSize, string name)
        {
            if (recordSize <= 0 || size % recordSize != 0)
                throw new WorldParseException(file, tag, start, string.Format(
                    "{0} byte(s) is not a whole number of {1}-byte {2} records", size, recordSize, name));
            return size / recordSize;
        }

        public byte U8(int o) { Require(o, 1); return d[start + o]; }

        public ushort U16(int o)
        {
            Require(o, 2);
            int p = start + o;
            return (ushort)(d[p] | (d[p + 1] << 8));
        }

        public uint U32(int o)
        {
            Require(o, 4);
            int p = start + o;
            return (uint)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (d[p + 3] << 24));
        }

        public int I32(int o) { return (int)U32(o); }

        public ulong U64(int o)
        {
            Require(o, 8);
            return U32(o) | ((ulong)U32(o + 4) << 32);
        }

        /// <summary>The float32 as stored (non-finite values are returned, not rejected).</summary>
        public float F32Raw(int o)
        {
            return BitConverter.Int32BitsToSingle((int)U32(o));
        }

        /// <summary>A float32 that must be finite (positions, heights, extents).</summary>
        public float F32(int o)
        {
            float f = F32Raw(o);
            if (float.IsNaN(f) || float.IsInfinity(f))
                throw new WorldParseException(file, tag, (long)start + o, "non-finite float");
            return f;
        }

        public byte[] Bytes(int o, int count)
        {
            Require(o, count);
            var r = new byte[count];
            Buffer.BlockCopy(d, start + o, r, 0, count);
            return r;
        }

        public uint[] U32Array()
        {
            int n = RecordCount(4, "u32");
            var r = new uint[n];
            for (int i = 0; i < n; i++)
                r[i] = U32(4 * i);
            return r;
        }

        public long AbsoluteOffset(int o) { return (long)start + o; }
    }

    /// <summary>A chunk that is present but not decoded (layout or meaning not established).</summary>
    public struct WorldUnsupportedChunk
    {
        public string Tag;
        public int DataOffset;
        public int Size;
        public string Reason;

        public override string ToString()
        {
            return string.Format("{0}@0x{1:X}+{2} ({3})", Tag, DataOffset, Size, Reason);
        }
    }
}
