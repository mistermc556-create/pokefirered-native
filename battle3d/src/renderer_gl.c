#include "battle3d/renderer.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

typedef struct {
    GLuint program;
    GLuint vao, vbo, ebo;
    GLuint tex;
    GLsizei index_count;
    int species;
} GpuMon;

typedef struct {
    int w, h;
    GLuint pokemon_prog, field_prog, outline_prog, ui_prog;
    GLuint fbo, color_tex, normal_tex, depth_rbo;
    GLuint quad_vao, quad_vbo;
    GLuint field_vao, field_vbo, field_ebo;
    GLsizei field_index_count;
    GpuMon mons[B3D_MAX_BATTLERS];
} Renderer;

static Renderer g_r;

static const char *kPokemonVS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNrm;\n"
    "layout(location=2) in vec2 aUv;\n"
    "uniform mat4 uViewProj;\n"
    "out vec3 vNrm;\n"
    "out vec3 vPos;\n"
    "out vec2 vUv;\n"
    "void main(){\n"
    "  vPos = aPos;\n"
    "  vNrm = normalize(aNrm);\n"
    "  vUv = aUv;\n"
    "  gl_Position = uViewProj * vec4(aPos,1.0);\n"
    "}\n";

static const char *kPokemonFS =
    "#version 330 core\n"
    "in vec3 vNrm; in vec3 vPos; in vec2 vUv;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec3 uCamPos;\n"
    "uniform vec3 uLightDir;\n"
    "uniform float uFlash;\n"
    "layout(location=0) out vec4 oColor;\n"
    "layout(location=1) out vec4 oNormal;\n"
    "void main(){\n"
    "  vec3 n = normalize(vNrm);\n"
    "  vec3 albedo = texture(uTex, vUv).rgb;\n"
    "  float ndl = max(dot(n, normalize(uLightDir)), 0.0);\n"
    "  float band = ndl > 0.62 ? 1.0 : ndl > 0.28 ? 0.72 : 0.48;\n"
    "  vec3 view = normalize(uCamPos - vPos);\n"
    "  float rim = pow(1.0 - max(dot(n, view), 0.0), 3.0) * 0.22;\n"
    "  vec3 col = albedo * (band * 0.82 + 0.22) + vec3(0.55,0.72,1.0)*rim;\n"
    "  col = mix(col, vec3(1.0,1.0,1.0), uFlash * 0.55);\n"
    "  oColor = vec4(col, 1.0);\n"
    "  oNormal = vec4(n * 0.5 + 0.5, gl_FragCoord.z);\n"
    "}\n";

static const char *kFieldVS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNrm;\n"
    "layout(location=2) in vec3 aCol;\n"
    "uniform mat4 uViewProj;\n"
    "out vec3 vNrm; out vec3 vCol; out vec3 vPos;\n"
    "void main(){\n"
    "  vPos=aPos; vNrm=aNrm; vCol=aCol;\n"
    "  gl_Position = uViewProj * vec4(aPos,1.0);\n"
    "}\n";

static const char *kFieldFS =
    "#version 330 core\n"
    "in vec3 vNrm; in vec3 vCol; in vec3 vPos;\n"
    "uniform vec3 uLightDir;\n"
    "layout(location=0) out vec4 oColor;\n"
    "layout(location=1) out vec4 oNormal;\n"
    "void main(){\n"
    "  vec3 n = normalize(vNrm);\n"
    "  float ndl = max(dot(n, normalize(uLightDir)), 0.0);\n"
    "  float band = ndl > 0.5 ? 1.0 : 0.78;\n"
    "  vec3 col = vCol * band;\n"
    "  float fog = clamp((length(vPos.xz)-8.0)/18.0, 0.0, 1.0);\n"
    "  col = mix(col, vec3(0.55,0.78,0.95), fog);\n"
    "  oColor = vec4(col,1.0);\n"
    "  oNormal = vec4(n*0.5+0.5, gl_FragCoord.z);\n"
    "}\n";

static const char *kOutlineVS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "out vec2 vUv;\n"
    "void main(){ vUv = aPos*0.5+0.5; gl_Position=vec4(aPos,0.0,1.0); }\n";

/* X/Y-style interior + silhouette outlines from view-space normals (GF 3DS look). */
static const char *kOutlineFS =
    "#version 330 core\n"
    "in vec2 vUv;\n"
    "uniform sampler2D uColor;\n"
    "uniform sampler2D uNormal;\n"
    "uniform vec2 uTexel;\n"
    "out vec4 oColor;\n"
    "vec3 sn(vec2 o){ return texture(uNormal, vUv+o).xyz * 2.0 - 1.0; }\n"
    "void main(){\n"
    "  vec3 c = texture(uColor, vUv).rgb;\n"
    "  vec3 n = sn(vec2(0.0));\n"
    "  vec3 t = abs(n - sn(vec2(0.0, uTexel.y)));\n"
    "  vec3 r = abs(n - sn(vec2(uTexel.x, 0.0)));\n"
    "  vec3 tr = abs(n - sn(uTexel));\n"
    "  float e = max(max(max(t.x,t.y),t.z), max(max(r.x,r.y),r.z));\n"
    "  e = max(e, max(max(tr.x,tr.y),tr.z));\n"
    "  float depthC = texture(uNormal, vUv).a;\n"
    "  float depthN = texture(uNormal, vUv+vec2(0.0,uTexel.y)).a;\n"
    "  float depthE = texture(uNormal, vUv+vec2(uTexel.x,0.0)).a;\n"
    "  e = max(e, abs(depthC-depthN)*18.0);\n"
    "  e = max(e, abs(depthC-depthE)*18.0);\n"
    "  float line = 1.0 - smoothstep(0.18, 0.42, e);\n"
    "  vec3 outlined = c * (0.08 + 0.92 * line);\n"
    "  oColor = vec4(outlined, 1.0);\n"
    "}\n";

static const char *kUiVS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "uniform mat4 uProj;\n"
    "uniform vec2 uOff;\n"
    "uniform vec2 uSize;\n"
    "void main(){ vec2 p = uOff + (aPos * 0.5 + 0.5) * uSize; gl_Position = uProj * vec4(p,0,1); }\n";

static const char *kUiFS =
    "#version 330 core\n"
    "uniform vec4 uColor;\n"
    "out vec4 oColor;\n"
    "void main(){ oColor = uColor; }\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, NULL, log);
        fprintf(stderr, "shader error: %s\n", log);
    }
    return s;
}

static GLuint link_prog(const char *vs, const char *fs)
{
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static void create_fbo(int w, int h)
{
    if (g_r.fbo) {
        glDeleteFramebuffers(1, &g_r.fbo);
        glDeleteTextures(1, &g_r.color_tex);
        glDeleteTextures(1, &g_r.normal_tex);
        glDeleteRenderbuffers(1, &g_r.depth_rbo);
    }
    glGenFramebuffers(1, &g_r.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_r.fbo);
    glGenTextures(1, &g_r.color_tex);
    glBindTexture(GL_TEXTURE_2D, g_r.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_r.color_tex, 0);
    glGenTextures(1, &g_r.normal_tex);
    glBindTexture(GL_TEXTURE_2D, g_r.normal_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, g_r.normal_tex, 0);
    glGenRenderbuffers(1, &g_r.depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, g_r.depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_r.depth_rbo);
    GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "FBO incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void build_field(void)
{
    enum { RINGS = 18, SEGS = 48 };
    int vtx = (RINGS + 1) * SEGS + 8;
    float *data = (float *)malloc(sizeof(float) * vtx * 9);
    uint16_t *idx = (uint16_t *)malloc(sizeof(uint16_t) * RINGS * SEGS * 6 + 36);
    int v = 0;
    for (int r = 0; r <= RINGS; ++r) {
        float t = (float)r / (float)RINGS;
        float rad = 0.4f + t * 16.0f;
        float y = 0.0f - t * 0.15f;
        for (int s = 0; s < SEGS; ++s) {
            float a = (float)s / (float)SEGS * 6.2831853f;
            float x = cosf(a) * rad;
            float z = sinf(a) * rad;
            Vec3 col = t < 0.18f ? vec3(0.42f, 0.78f, 0.38f) : vec3(0.55f + t * 0.1f, 0.76f, 0.40f);
            data[v * 9 + 0] = x;
            data[v * 9 + 1] = y;
            data[v * 9 + 2] = z;
            data[v * 9 + 3] = 0;
            data[v * 9 + 4] = 1;
            data[v * 9 + 5] = 0;
            data[v * 9 + 6] = col.x;
            data[v * 9 + 7] = col.y;
            data[v * 9 + 8] = col.z;
            ++v;
        }
    }
    int nidx = 0;
    for (int r = 0; r < RINGS; ++r) {
        for (int s = 0; s < SEGS; ++s) {
            int a = r * SEGS + s;
            int b = r * SEGS + (s + 1) % SEGS;
            int c = (r + 1) * SEGS + s;
            int d = (r + 1) * SEGS + (s + 1) % SEGS;
            idx[nidx++] = (uint16_t)a;
            idx[nidx++] = (uint16_t)c;
            idx[nidx++] = (uint16_t)b;
            idx[nidx++] = (uint16_t)b;
            idx[nidx++] = (uint16_t)c;
            idx[nidx++] = (uint16_t)d;
        }
    }
    g_r.field_index_count = nidx;
    glGenVertexArrays(1, &g_r.field_vao);
    glGenBuffers(1, &g_r.field_vbo);
    glGenBuffers(1, &g_r.field_ebo);
    glBindVertexArray(g_r.field_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_r.field_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * v * 9, data, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_r.field_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * nidx, idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, (void *)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 36, (void *)24);
    free(data);
    free(idx);
}

int b3d_renderer_init(int width, int height)
{
    memset(&g_r, 0, sizeof(g_r));
    g_r.w = width;
    g_r.h = height;
    g_r.pokemon_prog = link_prog(kPokemonVS, kPokemonFS);
    g_r.field_prog = link_prog(kFieldVS, kFieldFS);
    g_r.outline_prog = link_prog(kOutlineVS, kOutlineFS);
    g_r.ui_prog = link_prog(kUiVS, kUiFS);
    create_fbo(width, height);
    float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenVertexArrays(1, &g_r.quad_vao);
    glGenBuffers(1, &g_r.quad_vbo);
    glBindVertexArray(g_r.quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_r.quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void *)0);
    build_field();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    return 1;
}

void b3d_renderer_resize(int width, int height)
{
    g_r.w = width;
    g_r.h = height;
    create_fbo(width, height);
}

static void upload_mon(GpuMon *gpu, B3dBattler *b, const Pk3mVertex *skinned)
{
    if (!gpu->vao) {
        glGenVertexArrays(1, &gpu->vao);
        glGenBuffers(1, &gpu->vbo);
        glGenBuffers(1, &gpu->ebo);
        glGenTextures(1, &gpu->tex);
        glBindTexture(GL_TEXTURE_2D, gpu->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, b->model.tex_w, b->model.tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     b->model.rgba);
        gpu->index_count = b->model.index_count;
        glBindVertexArray(gpu->vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * b->model.index_count, b->model.indices, GL_STATIC_DRAW);
        gpu->species = b->species;
    }
    glBindVertexArray(gpu->vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Pk3mVertex) * b->model.vertex_count, skinned, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Pk3mVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Pk3mVertex), (void *)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Pk3mVertex), (void *)24);
}

static void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a)
{
    glUseProgram(g_r.ui_prog);
    Mat4 proj = mat4_ortho(0, (float)g_r.w, (float)g_r.h, 0, -1, 1);
    glUniformMatrix4fv(glGetUniformLocation(g_r.ui_prog, "uProj"), 1, GL_FALSE, proj.m);
    glUniform2f(glGetUniformLocation(g_r.ui_prog, "uOff"), x, y);
    glUniform2f(glGetUniformLocation(g_r.ui_prog, "uSize"), w, h);
    glUniform4f(glGetUniformLocation(g_r.ui_prog, "uColor"), r, g, b, a);
    glBindVertexArray(g_r.quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void b3d_renderer_draw(B3dBattleView *view)
{
    Mat4 v, p, vp;
    b3d_view_camera(view, &v, &p, (float)g_r.w / (float)g_r.h);
    vp = mat4_mul(p, v);
    Vec3 light = vec3_norm(vec3(-0.35f, 0.82f, 0.45f));

    glBindFramebuffer(GL_FRAMEBUFFER, g_r.fbo);
    glViewport(0, 0, g_r.w, g_r.h);
    glClearColor(0.47f, 0.74f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(g_r.field_prog);
    glUniformMatrix4fv(glGetUniformLocation(g_r.field_prog, "uViewProj"), 1, GL_FALSE, vp.m);
    glUniform3f(glGetUniformLocation(g_r.field_prog, "uLightDir"), light.x, light.y, light.z);
    glBindVertexArray(g_r.field_vao);
    glDrawElements(GL_TRIANGLES, g_r.field_index_count, GL_UNSIGNED_SHORT, 0);

    glUseProgram(g_r.pokemon_prog);
    glUniformMatrix4fv(glGetUniformLocation(g_r.pokemon_prog, "uViewProj"), 1, GL_FALSE, vp.m);
    glUniform3f(glGetUniformLocation(g_r.pokemon_prog, "uCamPos"), view->eye.x, view->eye.y, view->eye.z);
    glUniform3f(glGetUniformLocation(g_r.pokemon_prog, "uLightDir"), light.x, light.y, light.z);

    for (int i = 0; i < B3D_MAX_BATTLERS; ++i) {
        B3dBattler *b = &view->battlers[i];
        if (!b->occupied || b->model.vertex_count == 0)
            continue;
        Pk3mVertex *skinned = (Pk3mVertex *)malloc(sizeof(Pk3mVertex) * b->model.vertex_count);
        b3d_view_skin_battler(b, skinned);
        upload_mon(&g_r.mons[i], b, skinned);
        free(skinned);
        glUniform1f(glGetUniformLocation(g_r.pokemon_prog, "uFlash"), b->hit_flash);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_r.mons[i].tex);
        glUniform1i(glGetUniformLocation(g_r.pokemon_prog, "uTex"), 0);
        glBindVertexArray(g_r.mons[i].vao);
        glDrawElements(GL_TRIANGLES, g_r.mons[i].index_count, GL_UNSIGNED_INT, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_r.w, g_r.h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(g_r.outline_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_r.color_tex);
    glUniform1i(glGetUniformLocation(g_r.outline_prog, "uColor"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_r.normal_tex);
    glUniform1i(glGetUniformLocation(g_r.outline_prog, "uNormal"), 1);
    glUniform2f(glGetUniformLocation(g_r.outline_prog, "uTexel"), 1.0f / g_r.w, 1.0f / g_r.h);
    glBindVertexArray(g_r.quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* X/Y-ish HP ribbons: foe top-left, player bottom-right. */
    for (int i = 0; i < B3D_MAX_BATTLERS; ++i) {
        B3dBattler *b = &view->battlers[i];
        if (!b->occupied)
            continue;
        float x = b->player_side ? g_r.w - 360.0f : 40.0f;
        float y = b->player_side ? g_r.h - 90.0f : 36.0f;
        float ratio = b->hp / b->max_hp;
        draw_rect(x, y, 300, 18, 0.12f, 0.12f, 0.16f, 0.75f);
        float hr = ratio > 0.5f ? 0.46f : ratio > 0.2f ? 0.92f : 0.92f;
        float hg = ratio > 0.5f ? 0.86f : ratio > 0.2f ? 0.78f : 0.28f;
        float hb = ratio > 0.5f ? 0.38f : 0.18f;
        draw_rect(x + 4, y + 3, 292.0f * ratio, 12, hr, hg, hb, 1.0f);
    }
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void b3d_renderer_shutdown(void)
{
    for (int i = 0; i < B3D_MAX_BATTLERS; ++i) {
        if (g_r.mons[i].vao)
            glDeleteVertexArrays(1, &g_r.mons[i].vao);
    }
}
