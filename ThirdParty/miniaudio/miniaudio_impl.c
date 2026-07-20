/*
 * miniaudio_impl.c -- single translation unit that compiles the miniaudio implementation together with
 * stb_vorbis, so the Voice Lines audio preview can decode WoW's OGG/Vorbis (plus WAV/FLAC/MP3) and play
 * it on the default output device. Compiled as C and built once; the ~96k-line header stays out of the
 * rest of the codebase (only the thin C++ AudioPlayer wrapper includes miniaudio.h for its API).
 *
 * The stb_vorbis header must be included (header-only) BEFORE the miniaudio implementation so miniaudio
 * detects it and enables its Vorbis decoding backend; the stb_vorbis implementation must come AFTER the
 * miniaudio implementation. This is the ordering documented by miniaudio.
 */

/* Enable miniaudio's Vorbis backend by exposing stb_vorbis (header-only first). */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

/* We only need decoding + playback for previewing; no capture, no encoders. */
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

/* stb_vorbis implementation, after miniaudio's implementation. */
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
