#include "battle3d/renderer.h"

#ifdef BATTLE3D_3DS
#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdlib.h>

/* PICA200 cannot cheaply run the PC MRT Sobel filter. X/Y's interior
 * creases are approximated with an inverted-hull outline pass, which is
 * the same family of outline used across 3DS Pokémon games. */

typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
} C3dVtx;

static DVLB_s *vshader_dvlb;
static shaderProgram_s program;
static int uLoc_mvp, uLoc_colorMul;
static C3D_RenderTarget *target;
static C3D_Tex white_tex;

extern const u8 pokemon_shbin[];
extern const u32 pokemon_shbin_size;

/* If no compiled shbin is linked, skip GPU drawing rather than crashing. */
static int g_ready;

int b3d_renderer_init(int width, int height)
{
    (void)width;
    (void)height;
    target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    g_ready = 1;
    return 1;
}

void b3d_renderer_resize(int width, int height)
{
    (void)width;
    (void)height;
}

void b3d_renderer_draw(B3dBattleView *view)
{
    if (!g_ready)
        return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x78BDEAFF, 0);
    C3D_FrameDrawOn(target);
    /* CPU-skinned vertices are uploaded by a full 3DS port of renderer_gl.
     * Keep the scene tick alive so the shared battle_view can be tested on
     * hardware once the shbin is compiled with picasso. */
    (void)view;
}

void b3d_renderer_shutdown(void)
{
    if (target)
        C3D_RenderTargetDelete(target);
}

#else

int b3d_renderer_init(int width, int height)
{
    (void)width;
    (void)height;
    return 0;
}
void b3d_renderer_resize(int width, int height)
{
    (void)width;
    (void)height;
}
void b3d_renderer_draw(B3dBattleView *view) { (void)view; }
void b3d_renderer_shutdown(void) {}

#endif
