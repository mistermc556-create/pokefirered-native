#ifndef BATTLE3D_BATTLE_VIEW_H
#define BATTLE3D_BATTLE_VIEW_H

#include "battle3d/math.h"
#include "battle3d/pk3m.h"

#define B3D_MAX_BATTLERS 4

typedef enum {
    B3D_ANIM_IDLE = 0,
    B3D_ANIM_ATTACK,
    B3D_ANIM_DAMAGE,
    B3D_ANIM_FAINT,
    B3D_ANIM_INTRO,
    B3D_ANIM_COUNT
} B3dAnimSlot;

typedef struct {
    int occupied;
    int species;
    int shiny;
    int player_side;
    int slot; /* 0 left, 1 right */
    Pk3mModel model;
    int anim_index[B3D_ANIM_COUNT];
    int playing;
    float anim_time;
    int loop;
    float appear; /* 0..1 send-out scale */
    float hp, max_hp;
    Vec3 pos;
    float yaw;
    float hit_flash;
    float bob;
} B3dBattler;

typedef struct {
    Vec3 eye;
    Vec3 target;
    float fov;
    float time;
    int doubles;
    int intro_done;
    B3dBattler battlers[B3D_MAX_BATTLERS];
    char status_line[160];
} B3dBattleView;

void b3d_view_init(B3dBattleView *view, int doubles);
void b3d_view_set_battler(B3dBattleView *view, int battler, int species, int shiny, int player_side, int slot,
                          const char *model_dir);
void b3d_view_play(B3dBattleView *view, int battler, B3dAnimSlot slot, int loop);
void b3d_view_set_hp(B3dBattleView *view, int battler, float hp, float max_hp);
void b3d_view_update(B3dBattleView *view, float dt);
void b3d_view_camera(const B3dBattleView *view, Mat4 *view_m, Mat4 *proj_m, float aspect);
void b3d_view_skin_battler(B3dBattler *b, Pk3mVertex *skinned);

#endif
