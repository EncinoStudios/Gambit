#include "gbpch.h"

// The one translation unit that compiles cgltf (a header-only library); every other file includes cgltf.h for the
// declarations only. The glTF importer parses documents and buffers that it reads through the VFS (D-014): it never
// calls cgltf_parse_file and gives cgltf_load_buffers a file reader of its own.

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
