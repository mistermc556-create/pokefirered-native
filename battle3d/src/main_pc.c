#include "battle3d/battle_view.h"
#include "battle3d/renderer.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static B3dBattleView g_view;
static int g_width = 1280, g_height = 720;
static int g_turn_lock;

static void on_resize(GLFWwindow *w, int width, int height)
{
    (void)w;
    g_width = width > 1 ? width : 1;
    g_height = height > 1 ? height : 1;
    b3d_renderer_resize(g_width, g_height);
}

static void player_attack(void)
{
    if (g_turn_lock)
        return;
    g_turn_lock = 1;
    b3d_view_play(&g_view, 0, B3D_ANIM_ATTACK, 0);
    b3d_view_play(&g_view, 1, B3D_ANIM_DAMAGE, 0);
    g_view.battlers[1].hp -= 18.0f;
    if (g_view.battlers[1].hp < 0.0f)
        g_view.battlers[1].hp = 0.0f;
    snprintf(g_view.status_line, sizeof(g_view.status_line), "Player's Pokémon used a move!");
    if (g_view.battlers[1].hp <= 0.0f)
        b3d_view_play(&g_view, 1, B3D_ANIM_FAINT, 0);
}

static void foe_attack(void)
{
    b3d_view_play(&g_view, 1, B3D_ANIM_ATTACK, 0);
    b3d_view_play(&g_view, 0, B3D_ANIM_DAMAGE, 0);
    g_view.battlers[0].hp -= 12.0f;
    if (g_view.battlers[0].hp < 0.0f)
        g_view.battlers[0].hp = 0.0f;
    snprintf(g_view.status_line, sizeof(g_view.status_line), "The wild Pokémon used a move!");
    if (g_view.battlers[0].hp <= 0.0f)
        b3d_view_play(&g_view, 0, B3D_ANIM_FAINT, 0);
}

int main(int argc, char **argv)
{
    int player = 6;
    int foe = 25;
    const char *model_dir = "assets/baked";
    if (argc > 1)
        player = atoi(argv[1]);
    if (argc > 2)
        foe = atoi(argv[2]);
    if (argc > 3)
        model_dir = argv[3];

    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(g_width, g_height, "FireRed Battle — X/Y 3D", NULL, NULL);
    if (!win) {
        fprintf(stderr, "window failed (need OpenGL 3.3)\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(win, on_resize);

    if (!b3d_renderer_init(g_width, g_height))
        return 1;

    b3d_view_init(&g_view, 0);
    b3d_view_set_battler(&g_view, 0, player, 0, 1, 0, model_dir);
    b3d_view_set_battler(&g_view, 1, foe, 0, 0, 0, model_dir);
    if (!g_view.battlers[0].occupied || !g_view.battlers[1].occupied) {
        fprintf(stderr, "Could not load baked models from %s (need %d.pk3m and %d.pk3m)\n", model_dir, player, foe);
        fprintf(stderr, "Run: python3 tools/bake_glb.py <glb...> -o assets/baked\n");
        return 1;
    }
    g_view.battlers[0].hp = g_view.battlers[0].max_hp = 120;
    g_view.battlers[1].hp = g_view.battlers[1].max_hp = 95;

    double last = glfwGetTime();
    double foe_timer = -1.0;
    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.05f)
            dt = 0.05f;

        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, 1);
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS && !g_turn_lock && g_view.battlers[0].hp > 0 &&
            g_view.battlers[1].hp > 0) {
            player_attack();
            foe_timer = now + 1.15;
        }
        if (g_turn_lock && foe_timer > 0.0 && now >= foe_timer) {
            if (g_view.battlers[1].hp > 0 && g_view.battlers[0].hp > 0)
                foe_attack();
            foe_timer = -1.0;
            g_turn_lock = 0;
        }

        b3d_view_update(&g_view, dt);
        b3d_renderer_draw(&g_view);
        glfwSwapBuffers(win);
    }
    b3d_renderer_shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
