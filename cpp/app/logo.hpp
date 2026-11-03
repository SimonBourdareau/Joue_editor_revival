#pragma once
#include <cstddef>

struct LogoImage {
    const unsigned char* data = nullptr;
    std::size_t size = 0;
};

// The PNG data of the logo at 256, 48 or 32 pixels.
LogoImage logoPng(int pixels);
