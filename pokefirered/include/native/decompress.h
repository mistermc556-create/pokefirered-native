#ifndef GUARD_NATIVE_DECOMPRESS_H
#define GUARD_NATIVE_DECOMPRESS_H

#include <stddef.h>

enum NativeDecodeResult
{
    NATIVE_DECODE_OK,
    NATIVE_DECODE_INVALID,
    NATIVE_DECODE_TRUNCATED,
    NATIVE_DECODE_NO_SPACE,
};

// Decode GBA-format assets with explicit bounds. On failure, written is zero;
// destination contents may have been partially modified. NULL is invalid.
// Source and destination must not overlap. Trailing source padding is allowed.
enum NativeDecodeResult NativeDecodeLz77(const void *src, size_t srcSize,
                                        void *dest, size_t destSize, size_t *written);
enum NativeDecodeResult NativeDecodeRl(const void *src, size_t srcSize,
                                      void *dest, size_t destSize, size_t *written);

#endif // GUARD_NATIVE_DECOMPRESS_H