#include "ZipExtract.h"
#include "Logger.h"

#include <cstring>
#include <fstream>
#include <vector>

#include <zlib.h>

// Minimal ZIP local file header reader.
// ZIP files consist of a sequence of local-file-header + data entries.
// We iterate through those entries and extract each file.

namespace
{
	// Read a little-endian uint16 from a byte pointer.
	uint16_t readU16(const unsigned char* p)
	{
		return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
	}

	// Read a little-endian uint32 from a byte pointer.
	uint32_t readU32(const unsigned char* p)
	{
		return static_cast<uint32_t>(p[0])
			| (static_cast<uint32_t>(p[1]) << 8)
			| (static_cast<uint32_t>(p[2]) << 16)
			| (static_cast<uint32_t>(p[3]) << 24);
	}

	static constexpr uint32_t LOCAL_FILE_HEADER_SIG = 0x04034b50;
}

bool extractZip(const std::string& zipData, const std::filesystem::path& destDir)
{
	namespace fs = std::filesystem;

	const auto* data = reinterpret_cast<const unsigned char*>(zipData.data());
	const size_t dataSize = zipData.size();
	size_t offset = 0;

	while (offset + 30 <= dataSize)
	{
		const uint32_t sig = readU32(data + offset);
		if (sig != LOCAL_FILE_HEADER_SIG)
			break; // reached central directory or end

		const uint16_t method = readU16(data + offset + 8);
		const uint32_t compressedSize = readU32(data + offset + 18);
		const uint32_t uncompressedSize = readU32(data + offset + 22);
		const uint16_t nameLen = readU16(data + offset + 26);
		const uint16_t extraLen = readU16(data + offset + 28);

		if (offset + 30 + nameLen + extraLen > dataSize)
		{
			LOG_ERROR << "ZIP: truncated local file header at offset" << offset;
			return false;
		}

		std::string fileName(reinterpret_cast<const char*>(data + offset + 30), nameLen);
		const size_t dataStart = offset + 30 + nameLen + extraLen;

		if (dataStart + compressedSize > dataSize)
		{
			LOG_ERROR << "ZIP: truncated file data for" << fileName;
			return false;
		}

		// Skip directory entries
		if (!fileName.empty() && fileName.back() != '/' && fileName.back() != '\\')
		{
			// Sanitise path separators
			std::replace(fileName.begin(), fileName.end(), '/', '\\');

			const fs::path outPath = destDir / fileName;

			// Create parent directories
			std::error_code ec;
			fs::create_directories(outPath.parent_path(), ec);

			std::vector<unsigned char> outBuf;

			if (method == 0) // stored
			{
				outBuf.assign(data + dataStart, data + dataStart + compressedSize);
			}
			else if (method == 8) // deflated
			{
				outBuf.resize(uncompressedSize);

				z_stream strm{};
				strm.next_in = const_cast<unsigned char*>(data + dataStart);
				strm.avail_in = compressedSize;
				strm.next_out = outBuf.data();
				strm.avail_out = uncompressedSize;

				// -MAX_WBITS = raw deflate (no zlib/gzip header)
				if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
				{
					LOG_ERROR << "ZIP: inflateInit2 failed for" << fileName;
					return false;
				}

				const int ret = inflate(&strm, Z_FINISH);
				inflateEnd(&strm);

				if (ret != Z_STREAM_END)
				{
					LOG_ERROR << "ZIP: inflate failed for" << fileName << "ret=" << ret;
					return false;
				}
			}
			else
			{
				LOG_ERROR << "ZIP: unsupported compression method" << method << "for" << fileName;
				offset = dataStart + compressedSize;
				continue;
			}

			std::ofstream out(outPath, std::ios::binary);
			if (!out.is_open())
			{
				LOG_ERROR << "ZIP: failed to create file" << outPath.string();
				return false;
			}
			out.write(reinterpret_cast<const char*>(outBuf.data()), outBuf.size());
		}

		offset = dataStart + compressedSize;
	}

	return true;
}
