#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/// @brief Standalone utility for parsing WoW chunk-based file formats.
///
/// All WoW chunked files (M2, .skel, .bone, .anim, WMO, ADT, etc.) share
/// a common envelope: sequential [4-char magic][uint32 size][data] blocks.
/// This reader walks the buffer and returns the chunk table.  Detection of
/// chunked files uses a whitelist of known first-chunk IDs to avoid false
/// positives on non-chunked formats (.skin, .db2, .blp, etc.) that also
/// happen to start with printable ASCII.
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

	/// Known first-chunk magic values for WoW chunked file formats.
	/// Only the magic that can appear as the *first* chunk in a file
	/// needs to be listed here — inner chunks are parsed generically.
	inline const std::vector<std::string>& knownFirstChunks()
	{
		static const std::vector<std::string> chunks =
		{
			// M2 model chunks
			"PFID", "SFID", "AFID", "BFID", "MD21",
			"TXAC", "EXPT", "EXP2", "PABC", "PADC",
			"PSBC", "PEDC", "SKID", "TXID", "LDV1",
			// Animation / skeleton chunks
			"AFM2", "AFSA", "AFSB",
			"SKL1", "SKA1", "SKB1", "SKS1", "SKPD",
		};
		return chunks;
	}

	/// @brief Determine whether a buffer begins with a known chunked-file header.
	///
	/// Checks the first 4 bytes against the whitelist of known WoW chunk IDs
	/// and verifies the declared size fits within the buffer.
	/// @param data       Pointer to the start of the buffer.
	/// @param dataSize   Total size of the buffer in bytes.
	/// @return true if the buffer appears to be chunk-structured.
	inline bool isChunked(const unsigned char* data, size_t dataSize)
	{
		if (!data || dataSize < sizeof(ChunkHeader))
			return false;

		ChunkHeader header{};
		std::memcpy(&header, data, sizeof(ChunkHeader));

		const std::string magic(header.magic, 4);
		const auto& known = knownFirstChunks();
		if (std::find(known.begin(), known.end(), magic) == known.end())
			return false;

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
