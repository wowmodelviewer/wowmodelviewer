#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A lightweight software image class storing BGRA pixel data.
// Replaces QImage/QPainter for texture composition, scaling, mirroring, and saving.
class SoftwareImage
{
public:
	SoftwareImage() = default;
	SoftwareImage(int width, int height);

	// Construct from existing BGRA pixel data (copies the data)
	SoftwareImage(const uint8_t* bgraData, int width, int height);

	int width() const { return w_; }
	int height() const { return h_; }
	bool empty() const { return w_ == 0 || h_ == 0; }
	uint8_t* data() { return pixels_.data(); }
	const uint8_t* data() const { return pixels_.data(); }

	// Return a scaled copy using bilinear interpolation
	SoftwareImage scaled(int newWidth, int newHeight) const;

	// Return a vertically mirrored copy
	SoftwareImage mirrored() const;

	// Save as PNG to the given file path.
	// Internally converts BGRA → RGBA for stb_image_write.
	bool savePNG(const std::string& path) const;

	// Save as PNG to the given wide-string file path.
	bool savePNG(const std::wstring& path) const;

	// Composite a source image onto this image at the given position.
	// blendMode: 1 = SourceOver (default), 4 = Multiply, 6 = Overlay
	void composite(const SoftwareImage& src, int destX, int destY, int blendMode = 1);

	// Assign this image to be a copy of src (replaces contents entirely)
	void assign(const SoftwareImage& src);

	// Load a JPEG image from a memory buffer. Returns a BGRA SoftwareImage.
	static SoftwareImage loadFromMemory(const uint8_t* data, int size);

private:
	int w_ = 0;
	int h_ = 0;
	std::vector<uint8_t> pixels_; // BGRA format, 4 bytes per pixel
};
