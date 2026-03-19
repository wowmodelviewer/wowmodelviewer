#include "SoftwareImage.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "stb_image.h"
#include "stb_image_write.h"

#include "Logger.h"

SoftwareImage::SoftwareImage(int width, int height)
	: w_(width), h_(height), pixels_(width * height * 4, 0)
{
}

SoftwareImage::SoftwareImage(const uint8_t* bgraData, int width, int height)
	: w_(width), h_(height), pixels_(bgraData, bgraData + width * height * 4)
{
}

SoftwareImage SoftwareImage::scaled(int newWidth, int newHeight) const
{
	if (empty() || newWidth <= 0 || newHeight <= 0)
		return {};

	if (newWidth == w_ && newHeight == h_)
		return *this;

	SoftwareImage result(newWidth, newHeight);

	const float xRatio = static_cast<float>(w_) / newWidth;
	const float yRatio = static_cast<float>(h_) / newHeight;

	for (int y = 0; y < newHeight; ++y)
	{
		const float srcY = y * yRatio;
		const int y0 = static_cast<int>(srcY);
		const int y1 = std::min(y0 + 1, h_ - 1);
		const float fy = srcY - y0;

		for (int x = 0; x < newWidth; ++x)
		{
			const float srcX = x * xRatio;
			const int x0 = static_cast<int>(srcX);
			const int x1 = std::min(x0 + 1, w_ - 1);
			const float fx = srcX - x0;

			const uint8_t* p00 = &pixels_[(y0 * w_ + x0) * 4];
			const uint8_t* p10 = &pixels_[(y0 * w_ + x1) * 4];
			const uint8_t* p01 = &pixels_[(y1 * w_ + x0) * 4];
			const uint8_t* p11 = &pixels_[(y1 * w_ + x1) * 4];

			uint8_t* dst = &result.pixels_[(y * newWidth + x) * 4];
			for (int c = 0; c < 4; ++c)
			{
				const float top = p00[c] * (1.0f - fx) + p10[c] * fx;
				const float bot = p01[c] * (1.0f - fx) + p11[c] * fx;
				dst[c] = static_cast<uint8_t>(std::clamp(top * (1.0f - fy) + bot * fy, 0.0f, 255.0f));
			}
		}
	}

	return result;
}

SoftwareImage SoftwareImage::mirrored() const
{
	if (empty())
		return {};

	SoftwareImage result(w_, h_);
	const int rowBytes = w_ * 4;
	for (int y = 0; y < h_; ++y)
		std::memcpy(&result.pixels_[y * rowBytes], &pixels_[(h_ - 1 - y) * rowBytes], rowBytes);

	return result;
}

// Convert BGRA buffer to RGBA in-place (for stb_image_write which expects RGBA)
static void bgraToRgba(const uint8_t* src, uint8_t* dst, int count)
{
	for (int i = 0; i < count; ++i)
	{
		dst[i * 4 + 0] = src[i * 4 + 2]; // R
		dst[i * 4 + 1] = src[i * 4 + 1]; // G
		dst[i * 4 + 2] = src[i * 4 + 0]; // B
		dst[i * 4 + 3] = src[i * 4 + 3]; // A
	}
}

bool SoftwareImage::savePNG(const std::string& path) const
{
	if (empty())
		return false;

	const int count = w_ * h_;
	std::vector<uint8_t> rgba(count * 4);
	bgraToRgba(pixels_.data(), rgba.data(), count);

	return stbi_write_png(path.c_str(), w_, h_, 4, rgba.data(), w_ * 4) != 0;
}

bool SoftwareImage::savePNG(const std::wstring& path) const
{
	// Convert wstring to UTF-8 string for stb_image_write
	std::string utf8;
	utf8.reserve(path.size());
	for (wchar_t ch : path)
	{
		if (ch < 0x80)
			utf8.push_back(static_cast<char>(ch));
		else if (ch < 0x800)
		{
			utf8.push_back(static_cast<char>(0xC0 | (ch >> 6)));
			utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
		}
		else
		{
			utf8.push_back(static_cast<char>(0xE0 | (ch >> 12)));
			utf8.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
			utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
		}
	}
	return savePNG(utf8);
}

// Blend helpers
static inline uint8_t clampByte(float v)
{
	return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

static inline float overlayChannel(float base, float blend)
{
	// Overlay: if base < 0.5: 2*base*blend, else 1 - 2*(1-base)*(1-blend)
	if (base < 0.5f)
		return 2.0f * base * blend;
	else
		return 1.0f - 2.0f * (1.0f - base) * (1.0f - blend);
}

void SoftwareImage::composite(const SoftwareImage& src, int destX, int destY, int blendMode)
{
	if (src.empty() || empty())
		return;

	// Clip the source region to fit within destination
	const int srcW = std::min(src.w_, w_ - destX);
	const int srcH = std::min(src.h_, h_ - destY);

	if (srcW <= 0 || srcH <= 0)
		return;

	for (int y = 0; y < srcH; ++y)
	{
		for (int x = 0; x < srcW; ++x)
		{
			const uint8_t* s = &src.pixels_[(y * src.w_ + x) * 4];
			uint8_t* d = &pixels_[((destY + y) * w_ + (destX + x)) * 4];

			const float sa = s[3] / 255.0f; // source alpha (BGRA: index 3 = A)

			if (blendMode == 4) // Multiply
			{
				// Multiply blend: result = src * dst, then alpha-composite
				for (int c = 0; c < 3; ++c)
				{
					const float sc = s[c] / 255.0f;
					const float dc = d[c] / 255.0f;
					const float blended = sc * dc;
					d[c] = clampByte((blended * sa + dc * (1.0f - sa)) * 255.0f);
				}
				d[3] = clampByte((sa + (d[3] / 255.0f) * (1.0f - sa)) * 255.0f);
			}
			else if (blendMode == 6) // Overlay
			{
				for (int c = 0; c < 3; ++c)
				{
					const float sc = s[c] / 255.0f;
					const float dc = d[c] / 255.0f;
					const float blended = overlayChannel(dc, sc);
					d[c] = clampByte((blended * sa + dc * (1.0f - sa)) * 255.0f);
				}
				d[3] = clampByte((sa + (d[3] / 255.0f) * (1.0f - sa)) * 255.0f);
			}
			else // SourceOver (default, blendMode == 1 or anything else)
			{
				const float da = d[3] / 255.0f;
				const float outA = sa + da * (1.0f - sa);
				if (outA > 0.0f)
				{
					for (int c = 0; c < 3; ++c)
					{
						const float sc = s[c] / 255.0f;
						const float dc = d[c] / 255.0f;
						d[c] = clampByte(((sc * sa + dc * da * (1.0f - sa)) / outA) * 255.0f);
					}
				}
				d[3] = clampByte(outA * 255.0f);
			}
		}
	}
}

void SoftwareImage::assign(const SoftwareImage& src)
{
	w_ = src.w_;
	h_ = src.h_;
	pixels_ = src.pixels_;
}

SoftwareImage SoftwareImage::loadFromMemory(const uint8_t* data, int size)
{
	int w, h, channels;
	uint8_t* rgb = stbi_load_from_memory(data, size, &w, &h, &channels, 4); // force RGBA

	if (!rgb)
	{
		LOG_ERROR << "SoftwareImage::loadFromMemory failed:" << stbi_failure_reason();
		return {};
	}

	// stb gives us RGBA, convert to BGRA for our internal format
	SoftwareImage img(w, h);
	const int count = w * h;
	for (int i = 0; i < count; ++i)
	{
		img.pixels_[i * 4 + 0] = rgb[i * 4 + 2]; // B
		img.pixels_[i * 4 + 1] = rgb[i * 4 + 1]; // G
		img.pixels_[i * 4 + 2] = rgb[i * 4 + 0]; // R
		img.pixels_[i * 4 + 3] = rgb[i * 4 + 3]; // A
	}

	stbi_image_free(rgb);
	return img;
}
