#ifndef BATTLE3D_PK3M_H
#define BATTLE3D_PK3M_H

#include "battle3d/math.h"

#include <stdint.h>
#include <stddef.h>

#define PK3M_MAX_ANIMS 96
#define PK3M_MAX_NAME 64

typedef struct {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    uint16_t j[4];
    float w[4];
} Pk3mVertex;

typedef struct {
    int node;
    int path; /* 0 translation, 1 rotation, 2 scale */
    int key_count;
    float *times;
    float *values;
} Pk3mChannel;

typedef struct {
    char name[PK3M_MAX_NAME];
    float duration;
    int channel_count;
    Pk3mChannel *channels;
} Pk3mAnim;

typedef struct {
    int vertex_count;
    int index_count;
    int joint_count;
    int node_count;
    int anim_count;
    int tex_w, tex_h;
    Pk3mVertex *vertices;
    uint32_t *indices;
    int32_t *joint_nodes;
    float *ibms; /* joint_count * 16 */
    int32_t *parent;
    float *rest_t; /* node_count * 3 */
    float *rest_r; /* node_count * 4 */
    float *rest_s; /* node_count * 3 */
    Pk3mAnim *anims;
    uint8_t *rgba;
} Pk3mModel;

int pk3m_load(const char *path, Pk3mModel *out);
void pk3m_free(Pk3mModel *model);
int pk3m_find_anim(const Pk3mModel *model, const char *const *needles, int needle_count);

#endif
