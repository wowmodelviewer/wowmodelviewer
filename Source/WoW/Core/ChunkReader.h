#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/// @brief Standalone utility for parsing WoW chunk-based file formats.
///
/// All WoW chunked files (M2, .skel, .bone, .anim, WMO, ADT, etc.) share
/// a common envelope: sequential [4-char magic][uint32 size][data] blocks.
/// This reader walks the buffer and returns the chunk table without
/// requiring a whitelist.
namespace ChunkReader
{
	/// @brief On-disk chunk header: 4-byte magic + 4-byte size.
#pragma pack(push, 1)
	struct ChunkHeader
	{
		char     magic[4];
		uint32_t size;
	};
#pragma pack(pop)

	/// @brief Runtime representation of a single parsed chunk.
	struct ChunkInfo
	{
		std::string  magic;    ///< Four-character chunk identifier.
		uint32_t     start;    ///< Byte offset of the chunk data (after the header).
		uint32_t     size;     ///< Size of the chunk data in bytes.
	};

	/// @brief Determine whether a buffer begins with a valid chunk header.
	///
	/// Checks that the first 4 bytes are printable ASCII and the declared
	/// size fits within the buffer.  This replaces the old KNOWN_CHUNKS
	/// whitelist with a format-agnostic heuristic.
	/// @param data       Pointer to the start of the buffer.
	/// @param dataSize   Total size of the buffer in bytes.
	/// @return true if the buffer appears to be chunk-structured.
	inline bool isChunked(const unsigned char* data, size_t dataSize)
	{
		if (!data || dataSize < sizeof(ChunkHeader))
			return false;

		ChunkHeader header{};
		std::memcpy(&header, data, sizeof(ChunkHeader));

		// Magic bytes should be printable ASCII (0x20–0x7E) — this is true
		// for all known WoW chunk IDs (MD21, MOHD, MVER, SKL1, etc.)
		for (char c : header.magic)
		{
			if (c < 0x20 || c > 0x7E)
				return false;
		}

		// The first chunk's data must fit within the file.
		return (sizeof(ChunkHeader) + header.size) <= dataSize;
	}

	/// @brief Parse all top-level chunks from a buffer.
	///
	/// Walks sequential [magic][size][data] blocks until the end of the
	/// buffer is reached.  No whitelist is required — any valid header
	/// that fits within bounds is accepted.
	/// @param data       Pointer to the start of the buffer.
	/// @param dataSize   Total size of the buffer in bytes.
	/// @return Ordered vector of parsed chunks.
	inline std::vector<ChunkInfo> parse(const unsigned char* data, size_t dataSize)
	{
		std::vector<ChunkInfo> result;

		if (!data || dataSize < sizeof(ChunkHeader))
			return result;

		uint32_t offset = 0;
		while (offset + sizeof(ChunkHeader) <= dataSize)
		{
			ChunkHeader header{};
			std::memcpy(&header, data + offset, sizeof(ChunkHeader));
			offset += sizeof(ChunkHeader);

			// If the chunk data would exceed the buffer, stop.
			if (offset + header.size > dataSize)
				break;

			ChunkInfo info;
			info.magic = std::string(header.magic, 4);
			info.start = offset;
			info.size  = header.size;
			result.push_back(std::move(info));

			offset += header.size;
		}

		return result;
	}
}
