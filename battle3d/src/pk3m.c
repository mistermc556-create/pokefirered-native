#include "battle3d/pk3m.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int read_fully(FILE *f, void *dst, size_t n)
{
    return fread(dst, 1, n, f) == n;
}

int pk3m_load(const char *path, Pk3mModel *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    char magic[4];
    uint32_t version = 0;
    if (!read_fully(f, magic, 4) || memcmp(magic, "PK3M", 4) != 0) {
        fclose(f);
        return 0;
    }
    if (!read_fully(f, &version, 4) || version != 1) {
        fclose(f);
        return 0;
    }
    uint32_t vtx, idx, joints, nodes, anims, flags;
    uint32_t tw, th;
    if (!read_fully(f, &vtx, 4) || !read_fully(f, &idx, 4) || !read_fully(f, &joints, 4) || !read_fully(f, &nodes, 4) ||
        !read_fully(f, &anims, 4) || !read_fully(f, &flags, 4) || !read_fully(f, &tw, 4) || !read_fully(f, &th, 4)) {
        fclose(f);
        return 0;
    }
    (void)flags;
    out->vertex_count = (int)vtx;
    out->index_count = (int)idx;
    out->joint_count = (int)joints;
    out->node_count = (int)nodes;
    out->anim_count = (int)anims;
    out->tex_w = (int)tw;
    out->tex_h = (int)th;
    out->vertices = (Pk3mVertex *)malloc(sizeof(Pk3mVertex) * vtx);
    out->indices = (uint32_t *)malloc(sizeof(uint32_t) * idx);
    if (!out->vertices || !out->indices) {
        fclose(f);
        pk3m_free(out);
        return 0;
    }
    for (uint32_t i = 0; i < vtx; ++i) {
        Pk3mVertex *v = &out->vertices[i];
        if (!read_fully(f, &v->px, 12) || !read_fully(f, &v->nx, 12) || !read_fully(f, &v->u, 8) ||
            !read_fully(f, v->j, 8) || !read_fully(f, v->w, 16)) {
            fclose(f);
            pk3m_free(out);
            return 0;
        }
    }
    if (!read_fully(f, out->indices, sizeof(uint32_t) * idx)) {
        fclose(f);
        pk3m_free(out);
        return 0;
    }
    if (joints) {
        out->joint_nodes = (int32_t *)malloc(sizeof(int32_t) * joints);
        out->ibms = (float *)malloc(sizeof(float) * 16 * joints);
        if (!read_fully(f, out->joint_nodes, sizeof(int32_t) * joints) ||
            !read_fully(f, out->ibms, sizeof(float) * 16 * joints)) {
            fclose(f);
            pk3m_free(out);
            return 0;
        }
    }
    out->parent = (int32_t *)malloc(sizeof(int32_t) * nodes);
    out->rest_t = (float *)malloc(sizeof(float) * 3 * nodes);
    out->rest_r = (float *)malloc(sizeof(float) * 4 * nodes);
    out->rest_s = (float *)malloc(sizeof(float) * 3 * nodes);
    if (!read_fully(f, out->parent, sizeof(int32_t) * nodes) || !read_fully(f, out->rest_t, sizeof(float) * 3 * nodes) ||
        !read_fully(f, out->rest_r, sizeof(float) * 4 * nodes) || !read_fully(f, out->rest_s, sizeof(float) * 3 * nodes)) {
        fclose(f);
        pk3m_free(out);
        return 0;
    }
    out->anims = (Pk3mAnim *)calloc(anims, sizeof(Pk3mAnim));
    for (uint32_t a = 0; a < anims; ++a) {
        Pk3mAnim *anim = &out->anims[a];
        uint32_t ch_count = 0;
        if (!read_fully(f, anim->name, PK3M_MAX_NAME) || !read_fully(f, &anim->duration, 4) ||
            !read_fully(f, &ch_count, 4)) {
            fclose(f);
            pk3m_free(out);
            return 0;
        }
        anim->name[PK3M_MAX_NAME - 1] = 0;
        anim->channel_count = (int)ch_count;
        anim->channels = (Pk3mChannel *)calloc(ch_count, sizeof(Pk3mChannel));
        for (uint32_t c = 0; c < ch_count; ++c) {
            int32_t node, path, keys;
            if (!read_fully(f, &node, 4) || !read_fully(f, &path, 4) || !read_fully(f, &keys, 4)) {
                fclose(f);
                pk3m_free(out);
                return 0;
            }
            Pk3mChannel *ch = &anim->channels[c];
            ch->node = node;
            ch->path = path;
            ch->key_count = keys;
            int comps = path == 1 ? 4 : 3;
            ch->times = (float *)malloc(sizeof(float) * keys);
            ch->values = (float *)malloc(sizeof(float) * keys * comps);
            if (!read_fully(f, ch->times, sizeof(float) * keys) ||
                !read_fully(f, ch->values, sizeof(float) * keys * comps)) {
                fclose(f);
                pk3m_free(out);
                return 0;
            }
        }
    }
    size_t tex_bytes = (size_t)tw * (size_t)th * 4u;
    out->rgba = (uint8_t *)malloc(tex_bytes);
    if (!read_fully(f, out->rgba, tex_bytes)) {
        fclose(f);
        pk3m_free(out);
        return 0;
    }
    fclose(f);
    return 1;
}

void pk3m_free(Pk3mModel *model)
{
    if (!model)
        return;
    free(model->vertices);
    free(model->indices);
    free(model->joint_nodes);
    free(model->ibms);
    free(model->parent);
    free(model->rest_t);
    free(model->rest_r);
    free(model->rest_s);
    if (model->anims) {
        for (int a = 0; a < model->anim_count; ++a) {
            for (int c = 0; c < model->anims[a].channel_count; ++c) {
                free(model->anims[a].channels[c].times);
                free(model->anims[a].channels[c].values);
            }
            free(model->anims[a].channels);
        }
        free(model->anims);
    }
    free(model->rgba);
    memset(model, 0, sizeof(*model));
}

static void lower_copy(const char *in, char *out, size_t n)
{
    size_t i;
    for (i = 0; i + 1 < n && in[i]; ++i)
        out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = 0;
}

int pk3m_find_anim(const Pk3mModel *model, const char *const *needles, int needle_count)
{
    int best = -1;
    int best_score = -1;
    char name[PK3M_MAX_NAME];
    for (int i = 0; i < model->anim_count; ++i) {
        lower_copy(model->anims[i].name, name, sizeof(name));
        for (int n = 0; n < needle_count; ++n) {
            if (strstr(name, needles[n])) {
                int score = 1000 - n * 50 + (int)strlen(needles[n]);
                if (score > best_score) {
                    best_score = score;
                    best = i;
                }
            }
        }
    }
    return best;
}
