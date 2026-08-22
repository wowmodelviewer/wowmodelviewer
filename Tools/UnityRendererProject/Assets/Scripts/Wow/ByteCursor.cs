// ByteCursor.cs
//
// Bounds-checked little-endian reader for the raw WoW asset bytes WMV serves over IPC.
// Every read validates against the buffer length, so a truncated or malformed asset raises
// WowParseException with a useful message instead of throwing IndexOutOfRange somewhere deep
// in a parser -- or, worse, silently reading neighbouring memory.
//
// Deliberately free of UnityEngine references: the whole parsing layer is plain C# so it can
// be unit-tested outside the editor.

using System;

namespace Wmv.Wow
{
    /// <summary>A malformed, truncated or unsupported WoW asset. Always carries context.</summary>
    public class WowParseException : Exception
    {
        public WowParseException(string message) : base(message) { }
    }

    public struct ByteCursor
    {
        readonly byte[] data;
        readonly string what;   // asset description, for error messages
        int pos;

        public ByteCursor(byte[] data, string what, int start = 0)
        {
            if (data == null) throw new WowParseException((what ?? "asset") + ": no data");
            this.data = data;
            this.what = what ?? "asset";
            this.pos = start;
        }

        public int Position { get { return pos; } set { Require(value, 0); pos = value; } }
        public int Length { get { return data.Length; } }
        public byte[] Data { get { return data; } }

        /// <summary>Throws unless [offset, offset+count) lies inside the buffer.</summary>
        public void Require(int offset, int count)
        {
            if (offset < 0 || count < 0 || offset > data.Length || count > data.Length - offset)
                throw new WowParseException(string.Format(
                    "{0}: read of {1} byte(s) at offset {2} is outside the {3}-byte asset",
                    what, count, offset, data.Length));
        }

        public void Seek(int offset)
        {
            Require(offset, 0);
            pos = offset;
        }

        public byte ReadByte()
        {
            Require(pos, 1);
            return data[pos++];
        }

        public sbyte ReadSByte() { return (sbyte)ReadByte(); }

        public ushort ReadUInt16()
        {
            Require(pos, 2);
            ushort v = (ushort)(data[pos] | (data[pos + 1] << 8));
            pos += 2;
            return v;
        }

        public short ReadInt16() { return (short)ReadUInt16(); }

        public uint ReadUInt32()
        {
            Require(pos, 4);
            uint v = (uint)(data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24));
            pos += 4;
            return v;
        }

        public int ReadInt32() { return (int)ReadUInt32(); }

        public float ReadSingle()
        {
            uint bits = ReadUInt32();
            float f = BitConverter.ToSingle(BitConverter.GetBytes(bits), 0);
            if (float.IsNaN(f) || float.IsInfinity(f))
                throw new WowParseException(string.Format("{0}: non-finite float at offset {1}", what, pos - 4));
            return f;
        }

        public string ReadMagic()
        {
            Require(pos, 4);
            string s = string.Format("{0}{1}{2}{3}", (char)data[pos], (char)data[pos + 1],
                                                     (char)data[pos + 2], (char)data[pos + 3]);
            pos += 4;
            return s;
        }

        /// <summary>
        /// An M2Array: a (count, offset) pair. The offset is relative to the start of the
        /// structure the array belongs to (for an M2 that is the MD21 payload, not the file),
        /// which is why the caller passes the base it should be resolved against.
        /// </summary>
        public M2Array ReadArray()
        {
            uint count = ReadUInt32();
            uint offset = ReadUInt32();
            return new M2Array((int)count, (int)offset);
        }

        /// <summary>Validate that an array of fixed-size elements fits inside the buffer.</summary>
        public void RequireArray(M2Array arr, int elementSize, string name)
        {
            if (arr.Count == 0)
                return;
            if (arr.Count < 0 || arr.Offset < 0)
                throw new WowParseException(string.Format("{0}: {1} has a negative count/offset ({2}/{3})",
                                                          what, name, arr.Count, arr.Offset));
            long bytes = (long)arr.Count * elementSize;
            if (bytes > int.MaxValue)
                throw new WowParseException(string.Format("{0}: {1} claims {2} elements -- implausible",
                                                          what, name, arr.Count));
            Require(arr.Offset, (int)bytes);
        }
    }

    /// <summary>(count, offset) pair as stored in M2/skin headers.</summary>
    public struct M2Array
    {
        public readonly int Count;
        public readonly int Offset;
        public M2Array(int count, int offset) { Count = count; Offset = offset; }
        public override string ToString() { return string.Format("count={0} offset={1}", Count, Offset); }
    }
}
