#include "logo.hpp"

#include "logo.inc"

LogoImage logoPng(int pixels) {
    if (pixels <= 32) return {kLogo32Png, sizeof kLogo32Png};
    if (pixels <= 48) return {kLogo48Png, sizeof kLogo48Png};
    return {kLogo256Png, sizeof kLogo256Png};
}
