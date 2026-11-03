#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include "texture.hpp"

#include <GLFW/glfw3.h>

// the OpenGL 1.1 header of Windows lacks this constant
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <algorithm>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

Texture createTextureRgba(const unsigned char* rgba, int width, int height) {
    Texture texture;
    if (!rgba || width <= 0 || height <= 0) return texture;
    // clear errors left by earlier calls; bounded, because without a current context (or with a broken driver) the same error can come for ever
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {
    }
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture.id);
        return Texture();
    }
    texture.width = width;
    texture.height = height;
    return texture;
}

Texture createTexture(const unsigned char* png, std::size_t size) {
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    if (!decodePng(png, size, pixels, width, height)) return Texture();
    return createTextureRgba(pixels.data(), width, height);
}

std::vector<unsigned char> resizeRgba(const std::vector<unsigned char>& rgba, int width, int height, int newWidth, int newHeight) {
    std::vector<unsigned char> out(static_cast<std::size_t>(std::max(newWidth, 0)) * static_cast<std::size_t>(std::max(newHeight, 0)) * 4, 0);
    if (width <= 0 || height <= 0 || newWidth <= 0 || newHeight <= 0 || rgba.size() < static_cast<std::size_t>(width) * height * 4) return out;
    for (int y = 0; y < newHeight; ++y) {
        int y0 = static_cast<int>(static_cast<long long>(y) * height / newHeight);
        int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * height / newHeight));
        for (int x = 0; x < newWidth; ++x) {
            int x0 = static_cast<int>(static_cast<long long>(x) * width / newWidth);
            int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * width / newWidth));
            double sum[3] = {0, 0, 0}, alpha = 0;
            int count = 0;
            for (int sy = y0; sy < std::min(y1, height); ++sy) {
                for (int sx = x0; sx < std::min(x1, width); ++sx) {
                    const unsigned char* p = &rgba[(static_cast<std::size_t>(sy) * width + sx) * 4];
                    double a = p[3] / 255.0;
                    for (int c = 0; c < 3; ++c) sum[c] += p[c] * a;
                    alpha += a;
                    ++count;
                }
            }
            unsigned char* q = &out[(static_cast<std::size_t>(y) * newWidth + x) * 4];
            for (int c = 0; c < 3; ++c) q[c] = static_cast<unsigned char>(alpha > 0 ? sum[c] / alpha + 0.5 : 0);
            q[3] = static_cast<unsigned char>(count > 0 ? alpha / count * 255.0 + 0.5 : 0);
        }
    }
    return out;
}

bool decodePng(const unsigned char* png, std::size_t size, std::vector<unsigned char>& rgba, int& width, int& height) {
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(png, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) return false;
    rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

void destroyTexture(Texture& texture) {
    if (texture.id) glDeleteTextures(1, &texture.id);
    texture = Texture();
}
