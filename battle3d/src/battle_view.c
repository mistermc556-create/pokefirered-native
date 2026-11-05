#include "battle3d/battle_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* X/Y singles camera: low 3/4 back-left view, slight upward look at the foe. */
static const Vec3 kEyeSingles = {-2.35f, 1.12f, 4.85f};
static const Vec3 kTargetSingles = {0.55f, 0.82f, -1.55f};

static const Vec3 kPlayerPos[2] = {{-1.20f, 0.0f, 2.20f}, {-0.15f, 0.0f, 2.55f}};
static const Vec3 kFoePos[2] = {{1.55f, 0.0f, -3.35f}, {2.55f, 0.0f, -2.85f}};

static void resolve_clips(B3dBattler *b)
{
    static const char *idle[] = {"battlewait", "aidle", "defaultwait", "defaultidle", "idle", "wait"};
    static const char *attack[] = {"attack01", "fight_b", "rangeattack", "attack", "fight"};
    static const char *damage[] = {"damage01", "fight_d", "damage", "stun"};
    static const char *faint[] = {"down01_loop", "down01", "ko", "down"};
    static const char *intro[] = {"stepout01", "jump_s", "jumpup", "jump_e", "land02"};
    b->anim_index[B3D_ANIM_IDLE] = pk3m_find_anim(&b->model, idle, 6);
    b->anim_index[B3D_ANIM_ATTACK] = pk3m_find_anim(&b->model, attack, 5);
    b->anim_index[B3D_ANIM_DAMAGE] = pk3m_find_anim(&b->model, damage, 4);
    b->anim_index[B3D_ANIM_FAINT] = pk3m_find_anim(&b->model, faint, 4);
    b->anim_index[B3D_ANIM_INTRO] = pk3m_find_anim(&b->model, intro, 5);
}

void b3d_view_init(B3dBattleView *view, int doubles)
{
    memset(view, 0, sizeof(*view));
    view->doubles = doubles;
    view->eye = kEyeSingles;
    view->target = kTargetSingles;
    view->fov = 40.0f;
    snprintf(view->status_line, sizeof(view->status_line), "A wild battle broke out!");
}

void b3d_view_set_battler(B3dBattleView *view, int battler, int species, int shiny, int player_side, int slot,
                          const char *model_dir)
{
    if (battler < 0 || battler >= B3D_MAX_BATTLERS)
        return;
    B3dBattler *b = &view->battlers[battler];
    pk3m_free(&b->model);
    memset(b, 0, sizeof(*b));
    char path[512];
    const char *cat = shiny ? "shiny" : "regular";
    snprintf(path, sizeof(path), "%s/%s/%d.pk3m", model_dir, cat, species);
    if (!pk3m_load(path, &b->model)) {
        snprintf(path, sizeof(path), "%s/%d.pk3m", model_dir, species);
        if (!pk3m_load(path, &b->model))
            return;
    }
    b->occupied = 1;
    b->species = species;
    b->shiny = shiny;
    b->player_side = player_side;
    b->slot = slot;
    b->hp = b->max_hp = 100.0f;
    b->pos = player_side ? kPlayerPos[slot & 1] : kFoePos[slot & 1];
    b->yaw = player_side ? 2.85f : 0.40f;
    b->appear = 0.0f;
    resolve_clips(b);
    b3d_view_play(view, battler, B3D_ANIM_INTRO, 0);
    if (b->playing < 0)
        b3d_view_play(view, battler, B3D_ANIM_IDLE, 1);
}

void b3d_view_play(B3dBattleView *view, int battler, B3dAnimSlot slot, int loop)
{
    B3dBattler *b = &view->battlers[battler];
    if (!b->occupied)
        return;
    b->playing = b->anim_index[slot];
    b->anim_time = 0.0f;
    b->loop = loop;
    if (slot == B3D_ANIM_DAMAGE)
        b->hit_flash = 1.0f;
}

void b3d_view_set_hp(B3dBattleView *view, int battler, float hp, float max_hp)
{
    view->battlers[battler].hp = hp;
    view->battlers[battler].max_hp = max_hp > 1.0f ? max_hp : 1.0f;
}

static void sample_channel(const Pk3mChannel *ch, float time, float *out, int comps)
{
    if (ch->key_count <= 0) {
        memset(out, 0, sizeof(float) * comps);
        return;
    }
    if (time <= ch->times[0] || ch->key_count == 1) {
        memcpy(out, ch->values, sizeof(float) * comps);
        return;
    }
    if (time >= ch->times[ch->key_count - 1]) {
        memcpy(out, ch->values + (ch->key_count - 1) * comps, sizeof(float) * comps);
        return;
    }
    int i = 0;
    while (i + 1 < ch->key_count && ch->times[i + 1] < time)
        ++i;
    float t0 = ch->times[i];
    float t1 = ch->times[i + 1];
    float u = (time - t0) / (t1 - t0 + 1e-8f);
    const float *a = ch->values + i * comps;
    const float *b = ch->values + (i + 1) * comps;
    if (comps == 4) {
        Vec4 qa = {a[0], a[1], a[2], a[3]};
        Vec4 qb = {b[0], b[1], b[2], b[3]};
        Vec4 q = quat_slerp(qa, qb, u);
        out[0] = q.x;
        out[1] = q.y;
        out[2] = q.z;
        out[3] = q.w;
    } else {
        for (int c = 0; c < comps; ++c)
            out[c] = lerp(a[c], b[c], u);
    }
}

static void eval_pose(B3dBattler *b, Mat4 *world)
{
    const Pk3mModel *m = &b->model;
    float *t = (float *)malloc(sizeof(float) * 3 * m->node_count);
    float *r = (float *)malloc(sizeof(float) * 4 * m->node_count);
    float *s = (float *)malloc(sizeof(float) * 3 * m->node_count);
    memcpy(t, m->rest_t, sizeof(float) * 3 * m->node_count);
    memcpy(r, m->rest_r, sizeof(float) * 4 * m->node_count);
    memcpy(s, m->rest_s, sizeof(float) * 3 * m->node_count);
    if (b->playing >= 0 && b->playing < m->anim_count) {
        const Pk3mAnim *anim = &m->anims[b->playing];
        for (int c = 0; c < anim->channel_count; ++c) {
            const Pk3mChannel *ch = &anim->channels[c];
            if (ch->node < 0 || ch->node >= m->node_count)
                continue;
            if (ch->path == 0)
                sample_channel(ch, b->anim_time, t + ch->node * 3, 3);
            else if (ch->path == 1)
                sample_channel(ch, b->anim_time, r + ch->node * 4, 4);
            else
                sample_channel(ch, b->anim_time, s + ch->node * 3, 3);
        }
    }
    int *ready = (int *)calloc((size_t)m->node_count, sizeof(int));
    int progress = 0;
    while (progress < m->node_count) {
        int stepped = 0;
        for (int i = 0; i < m->node_count; ++i) {
            if (ready[i])
                continue;
            int p = m->parent[i];
            if (p >= 0 && !ready[p])
                continue;
            Vec3 tv = vec3(t[i * 3 + 0], t[i * 3 + 1], t[i * 3 + 2]);
            Vec4 rv = {r[i * 4 + 0], r[i * 4 + 1], r[i * 4 + 2], r[i * 4 + 3]};
            Vec3 sv = vec3(s[i * 3 + 0], s[i * 3 + 1], s[i * 3 + 2]);
            Mat4 local = mat4_trs(tv, rv, sv);
            world[i] = p >= 0 ? mat4_mul(world[p], local) : local;
            ready[i] = 1;
            ++progress;
            stepped = 1;
        }
        if (!stepped)
            break;
    }
    free(ready);
    free(t);
    free(r);
    free(s);
}

void b3d_view_skin_battler(B3dBattler *b, Pk3mVertex *skinned)
{
    const Pk3mModel *m = &b->model;
    Mat4 *world = (Mat4 *)malloc(sizeof(Mat4) * m->node_count);
    eval_pose(b, world);
    Mat4 *palette = (Mat4 *)malloc(sizeof(Mat4) * (m->joint_count > 0 ? m->joint_count : 1));
    if (m->joint_count) {
        for (int j = 0; j < m->joint_count; ++j) {
            int node = m->joint_nodes[j];
            Mat4 ibm;
            memcpy(ibm.m, m->ibms + j * 16, sizeof(float) * 16);
            palette[j] = (node >= 0 && node < m->node_count) ? mat4_mul(world[node], ibm) : ibm;
        }
    }
    float appear = b->appear < 1.0f ? b->appear : 1.0f;
    Mat4 yaw = mat4_ident();
    float cy = cosf(b->yaw), sy = sinf(b->yaw);
    yaw.m[0] = cy;
    yaw.m[2] = -sy;
    yaw.m[8] = sy;
    yaw.m[10] = cy;
    Mat4 root = mat4_mul(mat4_translate(vec3(b->pos.x, b->pos.y, b->pos.z)),
                         mat4_mul(yaw, mat4_scale(vec3(appear, appear, appear))));
    for (int i = 0; i < m->vertex_count; ++i) {
        const Pk3mVertex *v = &m->vertices[i];
        Vec3 p = vec3(0, 0, 0);
        Vec3 n = vec3(0, 0, 0);
        Vec3 bind_p = vec3(v->px, v->py, v->pz);
        Vec3 bind_n = vec3(v->nx, v->ny, v->nz);
        if (m->joint_count) {
            for (int k = 0; k < 4; ++k) {
                float w = v->w[k];
                if (w <= 0.0f)
                    continue;
                int ji = v->j[k];
                if (ji >= m->joint_count)
                    ji = 0;
                p = vec3_add(p, vec3_scale(mat4_mul_point(palette[ji], bind_p), w));
                n = vec3_add(n, vec3_scale(mat4_mul_dir(palette[ji], bind_n), w));
            }
        } else {
            p = bind_p;
            n = bind_n;
        }
        p = mat4_mul_point(root, p);
        n = vec3_norm(mat4_mul_dir(root, n));
        skinned[i] = *v;
        skinned[i].px = p.x;
        skinned[i].py = p.y;
        skinned[i].pz = p.z;
        skinned[i].nx = n.x;
        skinned[i].ny = n.y;
        skinned[i].nz = n.z;
    }
    free(palette);
    free(world);
}

void b3d_view_update(B3dBattleView *view, float dt)
{
    view->time += dt;
    /* Slow X/Y idle camera sway (Kalos battle intro settles into this). */
    float sway = sinf(view->time * 0.35f);
    view->eye = vec3_add(kEyeSingles, vec3(0.18f * sway, 0.04f * cosf(view->time * 0.27f), 0.10f * sway));
    view->target = vec3_add(kTargetSingles, vec3(-0.05f * sway, 0.03f * sinf(view->time * 0.22f), 0));

    for (int i = 0; i < B3D_MAX_BATTLERS; ++i) {
        B3dBattler *b = &view->battlers[i];
        if (!b->occupied)
            continue;
        if (b->appear < 1.0f)
            b->appear = fminf(1.0f, b->appear + dt * 2.4f);
        if (b->hit_flash > 0.0f)
            b->hit_flash = fmaxf(0.0f, b->hit_flash - dt * 3.5f);
        b->bob += dt;
        if (b->playing >= 0 && b->playing < b->model.anim_count) {
            const Pk3mAnim *anim = &b->model.anims[b->playing];
            b->anim_time += dt;
            if (anim->duration > 0.0f && b->anim_time >= anim->duration) {
                if (b->loop)
                    b->anim_time = fmodf(b->anim_time, anim->duration);
                else
                    b3d_view_play(view, i, B3D_ANIM_IDLE, 1);
            }
        } else if (b->anim_index[B3D_ANIM_IDLE] >= 0) {
            b3d_view_play(view, i, B3D_ANIM_IDLE, 1);
        }
    }
}

void b3d_view_camera(const B3dBattleView *view, Mat4 *view_m, Mat4 *proj_m, float aspect)
{
    *view_m = mat4_look_at(view->eye, view->target, vec3(0, 1, 0));
    *proj_m = mat4_perspective(view->fov, aspect, 0.08f, 80.0f);
}
