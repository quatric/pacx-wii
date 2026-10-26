#ifndef PLATFORM_H
#define PLATFORM_H
#ifdef _WIN32
/* Windows: mkdir takes no mode */
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#endif
/*
 * The integer types of libogc, for the code shared by every build: the Wii, the Mac, and the
 * Wii U, Switch and 3DS ports (PACX_PORT). PACX_NOGX: no GX textures; PACX_PACKFILE: the
 * pack is a file that is read at start-up (the Wii links it into the program).
 */
#if defined(PACX_SDL) && !defined(__SWITCH__) && !defined(__WIIU__)
#define PACX_DESKTOP 1      /* the SDL build for Linux and Windows (the Mac has its own) */
#endif
#if defined(PACX_MAC) || defined(PACX_PORT)
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
#define PACX_NOGX 1
#define PACX_PACKFILE 1
#else
#include <gctypes.h>
#endif
#endif
