#include "gbpch.h"

// The one translation unit that compiles stb_image and stb_image_write (header-only libraries). Every other file
// includes the headers for declarations only. STBI_NO_STDIO and STBI_WRITE_NO_STDIO come from the stb::stb CMake
// target: images are decoded from and encoded to memory so that all file access goes through the VFS (D-014).

#include "Gambit/RHI/Image.h"

// Larger images are rejected before any pixel memory is allocated.
#define STBI_MAX_DIMENSIONS ::Gambit::Image::MaxDimension

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
