#include "battle3d/battle_view.h"
#include "battle3d/renderer.h"

#ifdef BATTLE3D_3DS
#include <3ds.h>
#include <citro3d.h>
#endif

#include <stdio.h>
#include <stdlib.h>

static B3dBattleView g_view;

int main(int argc, char **argv)
{
#ifdef BATTLE3D_3DS
    int player = argc > 1 ? atoi(argv[1]) : 6;
    int foe = argc > 2 ? atoi(argv[2]) : 25;
    const char *dir = "romfs:/models";

    gfxInitDefault();
    gfxSet3D(false);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    romfsInit();
    consoleInit(GFX_BOTTOM, NULL);
    printf("FireRed battle — X/Y camera\nA: attack  START: quit\n");

    b3d_renderer_init(400, 240);
    b3d_view_init(&g_view, 0);
    b3d_view_set_battler(&g_view, 0, player, 0, 1, 0, dir);
    b3d_view_set_battler(&g_view, 1, foe, 0, 0, 0, dir);

    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_START)
            break;
        if (k & KEY_A) {
            b3d_view_play(&g_view, 0, B3D_ANIM_ATTACK, 0);
            b3d_view_play(&g_view, 1, B3D_ANIM_DAMAGE, 0);
            g_view.battlers[1].hp -= 18.0f;
        }
        b3d_view_update(&g_view, 1.0f / 60.0f);
        b3d_renderer_draw(&g_view);
        C3D_FrameEnd(0);
    }
    b3d_renderer_shutdown();
    romfsExit();
    C3D_Fini();
    gfxExit();
#else
    (void)argc;
    (void)argv;
    fprintf(stderr, "This binary is the 3DS entry point. Build with -DBATTLE3D_3DS=ON and the 3DS toolchain.\n");
#endif
    return 0;
}
