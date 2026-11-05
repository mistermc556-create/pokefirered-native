#include <SDL.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include "global.h"
#include "main.h"
#include "gba/flash_internal.h"
#include "platform.h"
#include "platform/dma.h"
#include "platform/framedraw.h"

extern void AgbMain(void);
extern void m4aSoundVSync(void);
extern void (*gIntrTable[])(void);

const char RomHeaderGameCode[4] = {'B', 'P', 'R', 'E'};
const char RomHeaderSoftwareVersion = 0;

// The PC has no wireless serial interrupt source.
void IntrSIO32(void)
{
}

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sTexture;
static SDL_AudioDeviceID sAudio;
static u16 sPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sRgba[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u64 sDeadline, sFrequency;
static unsigned long sFrames, sFrameLimit;
static char sSavePath[PATH_MAX];
static int sSaveDirty;
static u8 sKeys[SDL_NUM_SCANCODES];
static u16 sPressedKeys;
static unsigned long sKeyEvents, sAudioBatches, sAudioSamples, sAudioNonzero;
static FILE *sAudioCapture;
static void PushTestKey(SDL_Scancode code, u32 type);

static u16 KeySymbolButton(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_z: case SDLK_SPACE: return A_BUTTON;
    case SDLK_x: case SDLK_LSHIFT: return B_BUTTON;
    case SDLK_RETURN: case SDLK_KP_ENTER: return START_BUTTON;
    case SDLK_BACKSPACE: case SDLK_TAB: return SELECT_BUTTON;
    case SDLK_a: return L_BUTTON;
    case SDLK_s: return R_BUTTON;
    case SDLK_UP: return DPAD_UP;
    case SDLK_DOWN: return DPAD_DOWN;
    case SDLK_LEFT: return DPAD_LEFT;
    case SDLK_RIGHT: return DPAD_RIGHT;
    default: return 0;
    }
}

static u16 KeyButton(SDL_Scancode key)
{
    // Scancodes identify physical US-keyboard positions; keycodes identify
    // letters in the active layout (e.g. Austrian QWERTZ swaps Y and Z).
    return KeySymbolButton(SDL_GetKeyFromScancode(key));
}

static void PollInput(void)
{
    SDL_Event event;

    // Run on the window-owning thread immediately before the game reads keys.
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            exit(EXIT_SUCCESS);
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        {
            memset(sKeys, 0, sizeof(sKeys));
            sPressedKeys = 0;
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        {
            SDL_Scancode code = event.key.keysym.scancode;

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                exit(EXIT_SUCCESS);
            if (code > SDL_SCANCODE_UNKNOWN && code < SDL_NUM_SCANCODES)
            {
                sKeys[code] = event.type == SDL_KEYDOWN;
                // Preserve a tap even when both events arrive between frames.
                if (event.type == SDL_KEYDOWN && !event.key.repeat)
                    sPressedKeys |= KeyButton(code);
                sKeyEvents++;
            }
        }
    }
}

static void Fail(const char *operation)
{
    fprintf(stderr, "%s: %s\n", operation, SDL_GetError());
    exit(EXIT_FAILURE);
}

void Platform_StoreSaveFile(void)
{
    char temporary[PATH_MAX];
    FILE *file;

    if (!sSaveDirty)
        return;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", sSavePath) >= sizeof(temporary))
    {
        fprintf(stderr, "Save path too long\n");
        return;
    }
    file = fopen(temporary, "wb");
    if (file == NULL)
    {
        perror("Opening temporary save");
        return;
    }
    if (fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), file) != sizeof(FLASH_BASE)
        || fflush(file) != 0 || fsync(fileno(file)) != 0)
    {
        perror("Writing save");
        fclose(file);
        return;
    }
    if (fclose(file) != 0 || rename(temporary, sSavePath) != 0)
    {
        perror("Replacing save");
        return;
    }
    sSaveDirty = 0;
}

void NativePcSaveChanged(void)
{
    sSaveDirty = 1;
}

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    size_t start = (size_t)sectorNum * 4096 + offset;

    if (start > sizeof(FLASH_BASE) || size > sizeof(FLASH_BASE) - start)
    {
        fprintf(stderr, "Invalid flash read\n");
        abort();
    }
    memcpy(dest, FLASH_BASE + start, size);
}

void Platform_QueueAudio(s32 *buffer, s32 bytes)
{
    s16 samples[4096];
    size_t count, i;

    if (bytes <= 0)
        return;
    count = (size_t)bytes / sizeof(*buffer);
    if (count > sizeof(samples) / sizeof(samples[0]))
        return;
    for (i = 0; i < count; i++)
    {
        s32 value = buffer[i] / 512;
        if (value < -32768)
            value = -32768;
        if (value > 32767)
            value = 32767;
        samples[i] = value;
        if (value != 0)
            sAudioNonzero++;
    }
    sAudioBatches++;
    sAudioSamples += count;
    if (sAudioCapture && fwrite(samples, sizeof(samples[0]), count, sAudioCapture) != count)
        Fail("Writing audio capture");
    if (sAudio)
    {
        // Avoid accumulating seconds of latency during a debugger pause or
        // an unthrottled test. Never repeatedly clear the normal audio stream.
        if (SDL_GetQueuedAudioSize(sAudio) > 42060 * 4 / 2)
            SDL_ClearQueuedAudio(sAudio);
        if (SDL_QueueAudio(sAudio, samples, count * sizeof(samples[0])) != 0)
            Fail("Queueing game audio");
    }
}

u16 Platform_GetKeyInput(void)
{
    unsigned int code;
    u16 result = 0;

    // Opt-in startup tests exercise the same event queue as real keyboards.
    if (getenv("FIRERED_SMOKE_INPUT") && sFrames >= 150)
    {
        if (sFrames % 20 == 0)
            PushTestKey(SDL_GetScancodeFromKey(sFrames < 240 || sFrames % 600 < 20 ? SDLK_RETURN : SDLK_z),
                        SDL_KEYDOWN);
        if (sFrames % 20 == 2)
        {
            PushTestKey(SDL_SCANCODE_RETURN, SDL_KEYUP);
            PushTestKey(SDL_GetScancodeFromKey(SDLK_z), SDL_KEYUP);
        }
    }
    PollInput();
    result = sPressedKeys;
    sPressedKeys = 0;
    for (code = 0; code < SDL_NUM_SCANCODES; code++)
        if (sKeys[code])
            result |= KeyButton(code);

    return result;
}

void VBlankIntrWait(void)
{
    size_t i;

    REG_VCOUNT = 160;
    REG_DISPSTAT |= 1;
    RunDMAs(DMA_VBLANK);
    if (gIntrTable[4])
        gIntrTable[4]();
    // Native audio has no hardware VCount interrupt. Submit exactly one mixed
    // stereo batch per game frame, independently of screen-specific IRQ flags.
    m4aSoundVSync();
    REG_DISPSTAT &= ~1;
    DrawFrame(sPixels);
    for (i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        u16 color = sPixels[i];
        u32 red = (color & 31) * 255 / 31;
        u32 green = ((color >> 5) & 31) * 255 / 31;
        u32 blue = ((color >> 10) & 31) * 255 / 31;
        sRgba[i] = 0xFF000000 | (red << 16) | (green << 8) | blue;
    }
    if (SDL_UpdateTexture(sTexture, NULL, sRgba, DISPLAY_WIDTH * sizeof(u32)) != 0)
        Fail("Updating frame");
    SDL_RenderClear(sRenderer);
    SDL_RenderCopy(sRenderer, sTexture, NULL, NULL);
    SDL_RenderPresent(sRenderer);
    Platform_StoreSaveFile();
    sFrames++;
    if (sFrameLimit && sFrames >= sFrameLimit)
    {
        const char *capture = getenv("FIRERED_CAPTURE");
        if (capture)
        {
            SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(sRgba, DISPLAY_WIDTH,
                DISPLAY_HEIGHT, 32, DISPLAY_WIDTH * sizeof(u32), SDL_PIXELFORMAT_ARGB8888);
            if (!surface || SDL_SaveBMP(surface, capture) != 0)
                Fail("Saving frame capture");
            SDL_FreeSurface(surface);
        }
        fprintf(stderr, "Completed %lu game frames\n", sFrames);
        exit(EXIT_SUCCESS);
    }
    sDeadline += sFrequency * 280896 / 16777216;
    if (getenv("FIRERED_UNTHROTTLED"))
        return;
    while (SDL_GetPerformanceCounter() < sDeadline)
        SDL_Delay(1);
    if (SDL_GetPerformanceCounter() - sDeadline > sFrequency / 4)
        sDeadline = SDL_GetPerformanceCounter();
}

void SoftReset(u32 flags)
{
    (void)flags;
    fprintf(stderr, "Soft reset requested; restart the executable.\n");
    exit(EXIT_SUCCESS);
}

static void Shutdown(void)
{
    const char *stats = getenv("FIRERED_BACKEND_STATS");

    Platform_StoreSaveFile();
    if (sAudioCapture)
        fclose(sAudioCapture);
    if (stats)
    {
        FILE *file = fopen(stats, "w");
        if (file)
        {
            fprintf(file, "{\"frames\":%lu,\"key_events\":%lu,\"audio_batches\":%lu,"
                    "\"audio_samples\":%lu,\"audio_nonzero\":%lu,\"audio_device\":%u}\n",
                    sFrames, sKeyEvents, sAudioBatches, sAudioSamples, sAudioNonzero, sAudio);
            fclose(file);
        }
    }
    if (sAudio) SDL_CloseAudioDevice(sAudio);
    SDL_DestroyTexture(sTexture);
    SDL_DestroyRenderer(sRenderer);
    SDL_DestroyWindow(sWindow);
    SDL_Quit();
}

static void PushTestKey(SDL_Scancode code, u32 type)
{
    SDL_Event event = {0};

    event.type = type;
    event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = code;
    event.key.keysym.sym = SDL_GetKeyFromScancode(code);
    if (SDL_PushEvent(&event) != 1)
        Fail("Pushing keyboard test event");
}

static int TestKeyboard(void)
{
    static const struct { SDL_Keycode key; u16 button; } cases[] = {
        {SDLK_z, A_BUTTON}, {SDLK_x, B_BUTTON},
        {SDLK_RETURN, START_BUTTON}, {SDLK_BACKSPACE, SELECT_BUTTON},
        {SDLK_a, L_BUTTON}, {SDLK_s, R_BUTTON},
        {SDLK_UP, DPAD_UP}, {SDLK_DOWN, DPAD_DOWN},
        {SDLK_LEFT, DPAD_LEFT}, {SDLK_RIGHT, DPAD_RIGHT},
        {SDLK_SPACE, A_BUTTON}, {SDLK_LSHIFT, B_BUTTON},
        {SDLK_KP_ENTER, START_BUTTON}, {SDLK_TAB, SELECT_BUTTON},
    };
    size_t i;

    PollInput();
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        SDL_Scancode code = SDL_GetScancodeFromKey(cases[i].key);

        PushTestKey(code, SDL_KEYDOWN);
        if (Platform_GetKeyInput() != cases[i].button || Platform_GetKeyInput() != cases[i].button)
            return EXIT_FAILURE;
        PushTestKey(code, SDL_KEYUP);
        if (Platform_GetKeyInput() != 0)
            return EXIT_FAILURE;
        PushTestKey(code, SDL_KEYDOWN);
        PushTestKey(code, SDL_KEYUP);
        if (Platform_GetKeyInput() != cases[i].button || Platform_GetKeyInput() != 0)
            return EXIT_FAILURE;
    }
    PushTestKey(SDL_SCANCODE_UP, SDL_KEYDOWN);
    PushTestKey(SDL_GetScancodeFromKey(SDLK_z), SDL_KEYDOWN);
    if (Platform_GetKeyInput() != (DPAD_UP | A_BUTTON))
        return EXIT_FAILURE;
    {
        SDL_Event event = {0};
        event.type = SDL_WINDOWEVENT;
        event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        SDL_PushEvent(&event);
    }
    if (Platform_GetKeyInput() != 0)
        return EXIT_FAILURE;
    if (KeySymbolButton(SDLK_z) != A_BUTTON || KeySymbolButton(SDLK_x) != B_BUTTON
        || KeySymbolButton(SDLK_y) != 0)
        return EXIT_FAILURE;
    puts("PASS: layout-aware SDL keyboard down/hold/up, short taps, combinations and focus loss");
    return EXIT_SUCCESS;
}

static int TestVideo(void)
{
    memset(REG_BASE, 0, 0x400);
    memset(VRAM_, 0, VRAM_SIZE);
    memset(PLTT, 0, PLTT_SIZE);
    REG_DISPCNT = DISPCNT_BG0_ON | DISPCNT_BG1_ON;
    REG_BG0CNT = (31 << 8) | BGCNT_256COLOR;
    REG_BG1CNT = (30 << 8) | (1 << 2) | 1;
    ((u16 *)PLTT)[1] = RGB(31, 0, 0);
    ((u16 *)PLTT)[17] = RGB(0, 0, 31);
    ((u8 *)VRAM_)[1] = 1;
    memset(VRAM_ + BG_CHAR_SIZE, 0x11, 32);
    // BG1 uses palette bank 1 and must remain visible through BG0 index 0.
    ((u16 *)(VRAM_ + 30 * BG_SCREEN_SIZE))[0] = 1 << 12;
    DrawFrame(sPixels);
    if ((sPixels[0] & 0x7FFF) != RGB(0, 0, 31)
        || (sPixels[1] & 0x7FFF) != RGB(31, 0, 0))
    {
        fprintf(stderr, "FAIL: tiled 8bpp index-zero transparency\n");
        return EXIT_FAILURE;
    }
    memset(VRAM_, 0, VRAM_SIZE);
    REG_DISPCNT = DISPCNT_MODE_1 | DISPCNT_BG2_ON;
    REG_BG2CNT = (30 << 8) | (1 << 2);
    REG_BG2PA = REG_BG2PD = 0x100;
    memset(VRAM_ + BG_CHAR_SIZE, 1, 64);
    DrawFrame(sPixels);
    if ((sPixels[0] & 0x7FFF) != RGB(31, 0, 0)
        || (sPixels[DISPLAY_WIDTH + 1] & 0x7FFF) != RGB(31, 0, 0))
    {
        fprintf(stderr, "FAIL: affine background identity mapping\n");
        return EXIT_FAILURE;
    }
    puts("PASS: 8bpp transparent layering and affine background identity");
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    SDL_AudioSpec desired = {0};
    const char *save = getenv("FIRERED_SAVE_PATH");
    FILE *file;
    int keyboardTest = argc == 2 && strcmp(argv[1], "--test-keyboard") == 0;
    int videoTest = argc == 2 && strcmp(argv[1], "--test-video") == 0;

    if (argc > 1 && strcmp(argv[1], "--frames") == 0 && argc == 3)
        sFrameLimit = strtoul(argv[2], NULL, 10);
    else if (argc != 1 && !keyboardTest && !videoTest)
    {
        fprintf(stderr, "Usage: %s [--frames count | --test-keyboard | --test-video]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!save)
    {
        char *directory = SDL_GetPrefPath("firered-native", "firered");
        if (!directory)
            Fail("Locating save directory");
        snprintf(sSavePath, sizeof(sSavePath), "%sfirered.sav", directory);
        SDL_free(directory);
    }
    else if (snprintf(sSavePath, sizeof(sSavePath), "%s", save) >= sizeof(sSavePath))
        return EXIT_FAILURE;
    memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
    file = fopen(sSavePath, "rb");
    if (file)
    {
        size_t bytes = fread(FLASH_BASE, 1, sizeof(FLASH_BASE), file);
        int extra = fgetc(file);
        fclose(file);
        if (bytes != sizeof(FLASH_BASE) || extra != EOF)
        {
            fprintf(stderr, "Refusing invalid save file: %s\n", sSavePath);
            return EXIT_FAILURE;
        }
    }
    else if (errno != ENOENT)
    {
        perror(sSavePath);
        return EXIT_FAILURE;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
        Fail("Initializing SDL2");
    atexit(Shutdown);
    sWindow = SDL_CreateWindow("FireRed — native PC", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 720, 480, SDL_WINDOW_RESIZABLE);
    if (!sWindow) Fail("Creating game window");
    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_ACCELERATED);
    if (!sRenderer) sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!sRenderer) Fail("Creating renderer");
    SDL_RenderSetLogicalSize(sRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!sTexture) Fail("Creating game texture");
    if (keyboardTest)
        return TestKeyboard();
    if (videoTest)
        return TestVideo();
    desired.freq = 42060;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
        sAudio = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if (sAudio)
    {
        SDL_PauseAudioDevice(sAudio, 0);
        fprintf(stderr, "Audio: %s, %d Hz stereo\n", SDL_GetCurrentAudioDriver(), desired.freq);
    }
    else fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError());
    if (getenv("FIRERED_AUDIO_CAPTURE"))
    {
        sAudioCapture = fopen(getenv("FIRERED_AUDIO_CAPTURE"), "wb");
        if (!sAudioCapture)
        {
            perror("Opening audio capture");
            return EXIT_FAILURE;
        }
    }
    fprintf(stderr, "Video: %s. Controls: arrows, Z/X, Enter/Backspace, A/S\n", SDL_GetCurrentVideoDriver());
    sFrequency = SDL_GetPerformanceFrequency();
    sDeadline = SDL_GetPerformanceCounter();
    // The cartridge enters through BIOS reset, which initializes affine BGs
    // to identity. Zero-filled native registers collapse mode-1 portraits.
    REG_BG2PA = REG_BG2PD = 0x100;
    REG_BG3PA = REG_BG3PD = 0x100;
    fprintf(stderr, "Native FireRed starting. Save: %s\n", sSavePath);
    AgbMain();
    return EXIT_SUCCESS;
}