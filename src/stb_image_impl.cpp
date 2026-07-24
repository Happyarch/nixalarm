// Single translation unit that compiles the stb_image implementation. Keeping it
// isolated stops the ~9k-line implementation from being recompiled with every
// edit to the renderer, and avoids multiple-definition errors.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO  // we feed bytes we read ourselves; no stdio pathway needed
#include "stb_image.h"
