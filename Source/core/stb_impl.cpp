// Single compilation unit for stb implementations.
// stb_image and stb_image_write are header-only libraries that require
// exactly one source file to define their implementations.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
