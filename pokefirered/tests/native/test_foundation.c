#include "gba/gba.h"
#include "native/decompress.h"
#include "native/memory.h"
#include "random.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int sChecks;

#define CHECK(condition) do { \
    sChecks++; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void TestMemory(void)
{
    NativeGraphicsMemoryReset();
    REG_DISPCNT = 0x1234;
    CHECK(REG_DISPCNT == 0x1234);
    CHECK(gNativeGraphicsMemory.registers[0] == 0x34);
    *(u16 *)BG_PLTT = 0x7FFF;
    *(u16 *)OBJ_PLTT = 0x001F;
    *(u32 *)BG_CHAR_ADDR(2) = 0x12345678;
    *(u16 *)OAM = 0xABCD;
    CHECK(gNativeGraphicsMemory.palette[0] == 0xFF);
    CHECK(gNativeGraphicsMemory.palette[0x200] == 0x1F);
    CHECK(gNativeGraphicsMemory.vram[0x8000] == 0x78);
    CHECK(gNativeGraphicsMemory.oam[0] == 0xCD);
    NativeGraphicsMemoryReset();
    CHECK(REG_DISPCNT == 0);
    CHECK(*(u16 *)BG_PLTT == 0);
    CHECK(*(u32 *)BG_CHAR_ADDR(2) == 0);
    CHECK(*(u16 *)OAM == 0);
}

static void TestCopies(void)
{
    const u16 halfwords[] = {0x1234, 0x5678, 0x9ABC};
    u16 output[4] = {0};
    u32 words[17], copied[17];
    u32 fill = 0xCAFEBABE;
    unsigned int i;

    CpuSet(halfwords, output, 3);
    CHECK(memcmp(halfwords, output, sizeof(halfwords)) == 0);
    CHECK(output[3] == 0);
    CpuSet(halfwords + 1, output, CPU_SET_SRC_FIXED | 4);
    for (i = 0; i < 4; i++)
        CHECK(output[i] == halfwords[1]);
    for (i = 0; i < 17; i++)
    {
        words[i] = i * 12345;
        copied[i] = 0xFFFFFFFF;
    }
    CpuSet(words, copied, CPU_SET_32BIT | 3);
    CHECK(memcmp(words, copied, 12) == 0);
    CHECK(copied[3] == 0xFFFFFFFF);
    CpuFastSet(words, copied, 9);
    CHECK(memcmp(words, copied, 16 * sizeof(u32)) == 0);
    CHECK(copied[16] == 0xFFFFFFFF);
    CpuFastSet(&fill, copied, CPU_FAST_SET_SRC_FIXED | 8);
    for (i = 0; i < 8; i++)
        CHECK(copied[i] == fill);
    CpuSet(NULL, NULL, 0);
    CpuFastSet(NULL, NULL, 0);
}

static void TestDecompression(void)
{
    // Three literals followed by an overlapping six-byte match at distance 3.
    const u8 lz[] = {0x10, 9, 0, 0, 0x10, 'A', 'B', 'C', 0x30, 2};
    const u8 rl[] = {0x30, 9, 0, 0, 2, 'A', 'B', 'C', 0x83, 'Z'};
    const u8 badDistance[] = {0x10, 3, 0, 0, 0x80, 0, 0};
    const u8 badRun[] = {0x30, 1, 0, 0, 0x80, 'Z'};
    const u8 emptyLz[] = {0x10, 0, 0, 0};
    u8 output[16];
    size_t written, i;

    memset(output, 0xCD, sizeof(output));
    CHECK(NativeDecodeLz77(lz, sizeof(lz), output, 9, &written) == NATIVE_DECODE_OK);
    CHECK(written == 9);
    CHECK(memcmp(output, "ABCABCABC", 9) == 0);
    CHECK(output[9] == 0xCD);
    CHECK(NativeDecodeRl(rl, sizeof(rl), output, 9, &written) == NATIVE_DECODE_OK);
    CHECK(written == 9);
    CHECK(memcmp(output, "ABCZZZZZZ", 9) == 0);
    CHECK(output[9] == 0xCD);
    for (i = 0; i < sizeof(lz); i++)
    {
        CHECK(NativeDecodeLz77(lz, i, output, sizeof(output), &written) == NATIVE_DECODE_TRUNCATED);
        CHECK(written == 0);
    }
    for (i = 0; i < sizeof(rl); i++)
    {
        CHECK(NativeDecodeRl(rl, i, output, sizeof(output), &written) == NATIVE_DECODE_TRUNCATED);
        CHECK(written == 0);
    }
    CHECK(NativeDecodeLz77(lz, sizeof(lz), output, 8, &written) == NATIVE_DECODE_NO_SPACE);
    CHECK(written == 0);
    CHECK(NativeDecodeRl(rl, sizeof(rl), output, 8, &written) == NATIVE_DECODE_NO_SPACE);
    CHECK(written == 0);
    CHECK(NativeDecodeLz77(badDistance, sizeof(badDistance), output, sizeof(output), &written) == NATIVE_DECODE_INVALID);
    CHECK(NativeDecodeRl(badRun, sizeof(badRun), output, sizeof(output), &written) == NATIVE_DECODE_INVALID);
    CHECK(NativeDecodeLz77(rl, sizeof(rl), output, sizeof(output), &written) == NATIVE_DECODE_INVALID);
    CHECK(NativeDecodeRl(lz, sizeof(lz), output, sizeof(output), &written) == NATIVE_DECODE_INVALID);
    CHECK(NativeDecodeLz77(NULL, 0, output, sizeof(output), &written) == NATIVE_DECODE_INVALID);
    CHECK(NativeDecodeRl(rl, sizeof(rl), NULL, 0, &written) == NATIVE_DECODE_INVALID);
    CHECK(NativeDecodeLz77(lz, sizeof(lz), output, sizeof(output), NULL) == NATIVE_DECODE_INVALID);
    CHECK(NativeDecodeLz77(emptyLz, sizeof(emptyLz), output, sizeof(output), &written) == NATIVE_DECODE_OK);
    CHECK(written == 0);
}

static void TestLiteralStreams(void)
{
    u8 encoded[300], expected[256], output[258];
    size_t length, i, cursor, written;

    // Exercise every literal-stream length through two full flag blocks and
    // both RL block boundaries, with sentinels on both sides of the output.
    for (length = 0; length <= sizeof(expected); length++)
    {
        for (i = 0; i < length; i++)
            expected[i] = (u8)(i * 73 + length);

        encoded[0] = 0x10;
        encoded[1] = (u8)length;
        encoded[2] = (u8)(length >> 8);
        encoded[3] = 0;
        cursor = 4;
        for (i = 0; i < length; i++)
        {
            if (i % 8 == 0)
                encoded[cursor++] = 0;
            encoded[cursor++] = expected[i];
        }
        memset(output, 0xCD, sizeof(output));
        CHECK(NativeDecodeLz77(encoded, cursor, output + 1, length, &written) == NATIVE_DECODE_OK);
        CHECK(written == length);
        CHECK(memcmp(output + 1, expected, length) == 0);
        CHECK(output[0] == 0xCD && output[length + 1] == 0xCD);

        encoded[0] = 0x30;
        cursor = 4;
        for (i = 0; i < length;)
        {
            size_t count = length - i;

            if (count > 128)
                count = 128;
            encoded[cursor++] = (u8)(count - 1);
            memcpy(encoded + cursor, expected + i, count);
            cursor += count;
            i += count;
        }
        memset(output, 0xCD, sizeof(output));
        CHECK(NativeDecodeRl(encoded, cursor, output + 1, length, &written) == NATIVE_DECODE_OK);
        CHECK(written == length);
        CHECK(memcmp(output + 1, expected, length) == 0);
        CHECK(output[0] == 0xCD && output[length + 1] == 0xCD);
    }
}

static void TestOriginalRng(void)
{
    // Independent fixed vectors for FireRed's unchanged src/random.c.
    static const u16 expected[] = {0, 0xE97E, 0x5271, 0x31B0, 0x8E42};
    unsigned int i;

    SeedRng(0);
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        CHECK(Random() == expected[i]);
    SeedRng(0x1234);
    CHECK(gRngValue == 0x1234);
    CHECK(Random() == 0x4DCB);
}

int main(void)
{
    TestMemory();
    TestCopies();
    TestDecompression();
    TestLiteralStreams();
    TestOriginalRng();
    printf("Native foundation: %u checks passed (%zu-bit host).\n", sChecks, sizeof(void *) * 8);
    puts("This executable tests source-port foundations; it is not the playable game.");
    return EXIT_SUCCESS;
}