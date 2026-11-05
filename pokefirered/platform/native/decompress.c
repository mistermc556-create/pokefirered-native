#include "native/decompress.h"

#include <stdint.h>
#include <string.h>

static enum NativeDecodeResult ReadHeader(const void *src, size_t srcSize,
                                         void *dest, size_t destSize, size_t *written,
                                         uint8_t type, size_t *outputSize)
{
    const uint8_t *input = src;

    if (written == NULL)
        return NATIVE_DECODE_INVALID;
    *written = 0;
    if (src == NULL || dest == NULL)
        return NATIVE_DECODE_INVALID;
    if (srcSize < 4)
        return NATIVE_DECODE_TRUNCATED;
    if (input[0] != type)
        return NATIVE_DECODE_INVALID;

    *outputSize = (size_t)input[1] | ((size_t)input[2] << 8) | ((size_t)input[3] << 16);
    if (*outputSize > destSize)
        return NATIVE_DECODE_NO_SPACE;
    return NATIVE_DECODE_OK;
}

enum NativeDecodeResult NativeDecodeLz77(const void *src, size_t srcSize,
                                        void *dest, size_t destSize, size_t *written)
{
    const uint8_t *input = src;
    uint8_t *output = dest;
    size_t inputPos = 4, outputPos = 0, outputSize;
    enum NativeDecodeResult result;

    result = ReadHeader(src, srcSize, dest, destSize, written, 0x10, &outputSize);
    if (result != NATIVE_DECODE_OK)
        return result;

    while (outputPos < outputSize)
    {
        unsigned int mask;
        uint8_t flags;

        if (inputPos >= srcSize)
            return NATIVE_DECODE_TRUNCATED;
        flags = input[inputPos++];
        for (mask = 0x80; mask != 0 && outputPos < outputSize; mask >>= 1)
        {
            if (flags & mask)
            {
                size_t length, distance, i;

                if (srcSize - inputPos < 2)
                    return NATIVE_DECODE_TRUNCATED;
                length = (input[inputPos] >> 4) + 3;
                distance = (((size_t)input[inputPos] & 0xF) << 8) | input[inputPos + 1];
                distance++;
                inputPos += 2;
                if (distance > outputPos || length > outputSize - outputPos)
                    return NATIVE_DECODE_INVALID;
                // Bytewise forward copying is required for overlapping runs.
                for (i = 0; i < length; i++)
                {
                    output[outputPos] = output[outputPos - distance];
                    outputPos++;
                }
            }
            else
            {
                if (inputPos >= srcSize)
                    return NATIVE_DECODE_TRUNCATED;
                output[outputPos++] = input[inputPos++];
            }
        }
    }

    *written = outputSize;
    return NATIVE_DECODE_OK;
}

enum NativeDecodeResult NativeDecodeRl(const void *src, size_t srcSize,
                                      void *dest, size_t destSize, size_t *written)
{
    const uint8_t *input = src;
    uint8_t *output = dest;
    size_t inputPos = 4, outputPos = 0, outputSize;
    enum NativeDecodeResult result;

    result = ReadHeader(src, srcSize, dest, destSize, written, 0x30, &outputSize);
    if (result != NATIVE_DECODE_OK)
        return result;

    while (outputPos < outputSize)
    {
        uint8_t control;
        size_t length;

        if (inputPos >= srcSize)
            return NATIVE_DECODE_TRUNCATED;
        control = input[inputPos++];
        length = (control & 0x7F) + ((control & 0x80) ? 3 : 1);
        if (length > outputSize - outputPos)
            return NATIVE_DECODE_INVALID;
        if (control & 0x80)
        {
            if (inputPos >= srcSize)
                return NATIVE_DECODE_TRUNCATED;
            memset(output + outputPos, input[inputPos++], length);
        }
        else
        {
            if (length > srcSize - inputPos)
                return NATIVE_DECODE_TRUNCATED;
            memcpy(output + outputPos, input + inputPos, length);
            inputPos += length;
        }
        outputPos += length;
    }

    *written = outputSize;
    return NATIVE_DECODE_OK;
}