#pragma once
#include <cstddef>
#include <vector>

struct Texture {
    unsigned int id = 0;
    int width = 0, height = 0;
};

// Decodes a PNG and uploads it as an OpenGL texture with plain linear filtering (no mipmaps, which some drivers and virtual machines get wrong);
// needs a current OpenGL context. The id is 0 when the picture could not be decoded or the driver reported an error.
Texture createTexture(const unsigned char* png, std::size_t size);
Texture createTextureRgba(const unsigned char* rgba, int width, int height);

// Reduces RGBA pixels to another size by averaging the area each new pixel covers (colours weighted by their opacity).
std::vector<unsigned char> resizeRgba(const std::vector<unsigned char>& rgba, int width, int height, int newWidth, int newHeight);
void destroyTexture(Texture& texture);

// Decodes a PNG into RGBA pixels (no OpenGL needed).
bool decodePng(const unsigned char* png, std::size_t size, std::vector<unsigned char>& rgba, int& width, int& height);
