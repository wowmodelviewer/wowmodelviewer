#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// @brief CPU-side image buffer storing BGRA pixel data.
///
/// Provides scaling, mirroring, compositing, and PNG save operations.
/// Used for texture composition in the WoW Model Viewer.
class SoftwareImage
{
public:
	SoftwareImage() = default;

	/// @brief Create a blank image with the given dimensions.
	SoftwareImage(int width, int height);

	/// @brief Construct from existing BGRA pixel data (copies the data).
	SoftwareImage(const uint8_t* bgraData, int width, int height);

	/// @brief Image width in pixels.
	int width() const { return w_; }

	/// @brief Image height in pixels.
	int height() const { return h_; }

	/// @brief True if the image has zero dimensions.
	bool empty() const { return w_ == 0 || h_ == 0; }

	/// @brief Mutable pointer to the raw BGRA pixel buffer.
	uint8_t* data() { return pixels_.data(); }

	/// @brief Const pointer to the raw BGRA pixel buffer.
	const uint8_t* data() const { return pixels_.data(); }

	/// @brief Return a scaled copy using bilinear interpolation.
	SoftwareImage scaled(int newWidth, int newHeight) const;

	/// @brief Return a vertically mirrored copy.
	SoftwareImage mirrored() const;

	/// @brief Save as PNG to the given file path.
	///
	/// Internally converts BGRA → RGBA for stb_image_write.
	bool savePNG(const std::string& path) const;

	/// @brief Save as PNG to the given wide-string file path.
	bool savePNG(const std::wstring& path) const;

	/// @brief Composite a source image onto this image at the given position.
	/// @param src        Source image to composite.
	/// @param destX      Horizontal offset in this image.
	/// @param destY      Vertical offset in this image.
	/// @param blendMode  1 = SourceOver (default), 4 = Multiply, 6 = Overlay.
	void composite(const SoftwareImage& src, int destX, int destY, int blendMode = 1);

	/// @brief Replace contents entirely with a copy of src.
	void assign(const SoftwareImage& src);

	/// @brief Load a JPEG image from a memory buffer.
	/// @return A BGRA SoftwareImage (empty on failure).
	static SoftwareImage loadFromMemory(const uint8_t* data, int size);

private:
	int w_ = 0;
	int h_ = 0;
	std::vector<uint8_t> pixels_; // BGRA format, 4 bytes per pixel
};
