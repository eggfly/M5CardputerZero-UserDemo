#pragma once
// Cross-platform framebuffer stubs for macOS/Windows

#ifdef __linux__
#include <linux/fb.h>
#else

#include <stdint.h>

#ifndef _FB_COMPAT_H
#define _FB_COMPAT_H

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t bits_per_pixel;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
};

struct fb_fix_screeninfo {
    char id[16];
    unsigned long smem_len;
    uint32_t line_length;
};

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#endif // _FB_COMPAT_H
#endif // __linux__
