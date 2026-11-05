#ifndef BATTLE3D_RENDERER_H
#define BATTLE3D_RENDERER_H

#include "battle3d/battle_view.h"

int b3d_renderer_init(int width, int height);
void b3d_renderer_resize(int width, int height);
void b3d_renderer_draw(B3dBattleView *view);
void b3d_renderer_shutdown(void);

#endif
