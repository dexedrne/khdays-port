/* [video] camera_zoom: the game's 3D field of view, the projection's x and y taken smaller (the camera
 * further away), the HUD's 3D and an orthographic picture left as they are.
 *
 * A triangle is sent through the G3D command path with a perspective projection, once at the game's own
 * field of view (khdays_g3d_set_zoom 1) and once wider (0.87: [video] camera_zoom = 115), and the
 * screen-space vertices the port builds are compared: the wider one sits closer to the screen's middle by
 * the factor, its w (the depth) the same. Then the same with the HUD's 3D flag, and with an orthographic
 * projection (every w 1: a screen-space picture), which must not move at all. khdays_g3d_matrices, what a
 * mod's model and a KH2 enemy's draw use, returns the projection the same way. */
#include "../gfx/g3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failed;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                                 \
            ++failed;                                                                                                  \
        }                                                                                                              \
    } while (0)

/* x' = 1.5x, y' = 2y, w' = -z (a perspective projection, as the field draws with) */
static const uint32_t perspective[16] = {6144, 0, 0, 0, 0, 8192, 0, 0, 0, 0, -4096, -4096, 0, 0, 0, 0};
/* every w 1 and the x and y halved, so (1, 1) lies inside it: a screen-space picture */
static const uint32_t orthographic[16] = {2048, 0, 0, 0, 0, 2048, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096};

/* One triangle at (1, 1, -10), (0, 0, -10), (0, 1, -10), through the game's own vertex path, with
 * `projection` loaded and the port's `hud`/`zoom` as given; returns the first vertex the list holds
 * (screen space: x and y in DS dots, w the clip w; all 0 when nothing was kept). */
static KhdaysG3dVertex quad(const uint32_t projection[16], int32_t z, int hud, float zoom)
{
    uint32_t mode = 0, attr = 0xc0u | (31u << 16), type = 0, swap = 0;
    static const uint32_t xy[3] = {4096u | (4096u << 16), 0u | (0u << 16), 0u | (4096u << 16)};
    KhdaysG3dVertex none;
    memset(&none, 0, sizeof(none));
    khdays_g3d_reset();
    khdays_g3d_set_zoom(zoom);
    khdays_g3d_set_hud(hud);
    khdays_g3d_command(0x10, &mode);      /* MTX_MODE: projection */
    khdays_g3d_command(0x16, projection); /* MTX_LOAD_4x4 */
    khdays_g3d_command(0x29, &attr);      /* POLYGON_ATTR: both faces, opaque */
    khdays_g3d_command(0x40, &type);      /* BEGIN_VTXS: triangles */
    for (int i = 0; i < 3; ++i) {
        uint32_t words[2] = {xy[i], (uint32_t)z}; /* VTX_16 */
        khdays_g3d_command(0x23, words);
    }
    khdays_g3d_command(0x41, NULL);  /* END_VTXS */
    khdays_g3d_command(0x50, &swap); /* SwapBuffers */
    khdays_g3d_vblank();
    {
        const KhdaysG3dList *list = khdays_g3d_shown();
        return list != NULL && list->nverts > 0 ? list->verts[0] : none;
    }
}

int main(void)
{
    const KhdaysG3dVertex own = quad(perspective, -32768, 0, 1.0f);
    const KhdaysG3dVertex wide = quad(perspective, -32768, 0, 0.87f);
    const KhdaysG3dVertex hud = quad(perspective, -32768, 1, 0.87f);
    const KhdaysG3dVertex flat_own = quad(orthographic, 0, 0, 1.0f);
    const KhdaysG3dVertex flat_wide = quad(orthographic, 0, 0, 0.87f);
    float pos[16], vec[16], proj[16];

    printf("own %.3f,%.3f w %.0f; wide %.3f,%.3f w %.0f; hud %.3f,%.3f; flat %.3f,%.3f w %.0f\n", (double)own.x,
           (double)own.y, (double)own.w, (double)wide.x, (double)wide.y, (double)wide.w, (double)hud.x, (double)hud.y,
           (double)flat_own.x, (double)flat_own.y, (double)flat_own.w);

    /* the vertex is there, at the game's own field of view: (c.x + w) * 256 / (2w) =
     * (6144 + 32768) * 256 / 65536 = 152 across, and 192 - (8192 + 32768) * 192 / 65536 = 72 down;
     * w is the clip w in the port's 20.12 (32768: 8.0, the depth) */
    CHECK(fabsf(own.w - 32768.0f) < 0.5f);
    CHECK(fabsf(own.x - 152.0f) < 0.1f);
    CHECK(fabsf(own.y - 72.0f) < 0.1f);
    /* the depth is untouched: only x and y are the field of view's */
    CHECK(fabsf(own.w - wide.w) < 0.001f);
    /* the wider camera's vertex, the same factor nearer the middle of the screen (128, 96) */
    CHECK(fabsf((wide.x - 128.0f) - (own.x - 128.0f) * 0.87f) < 0.1f);
    CHECK(fabsf((wide.y - 96.0f) - (own.y - 96.0f) * 0.87f) < 0.1f);

    /* the HUD's own 3D keeps its place on the screen */
    CHECK(fabsf(hud.x - own.x) < 0.01f);
    CHECK(fabsf(hud.y - own.y) < 0.01f);
    CHECK(fabsf(hud.w - own.w) < 0.01f);

    /* an orthographic projection (a screen-space picture) covers the screen whole: nothing moves */
    CHECK(fabsf(flat_own.w - 4096.0f) < 0.5f); /* w 1 in 20.12 */
    CHECK(fabsf(flat_wide.x - flat_own.x) < 0.01f);
    CHECK(fabsf(flat_wide.y - flat_own.y) < 0.01f);

    /* a mod's model, an enemy's draw: khdays_g3d_matrices' projection */
    {
        uint32_t mode = 0;
        khdays_g3d_reset();
        khdays_g3d_set_zoom(1.0f);
        khdays_g3d_command(0x10, &mode);
        khdays_g3d_command(0x16, perspective);
        khdays_g3d_matrices(pos, vec, proj);
        CHECK(fabsf(proj[0] - 1.5f) < 0.001f && fabsf(proj[5] - 2.0f) < 0.001f);
        CHECK(fabsf(proj[11] + 1.0f) < 0.001f && fabsf(proj[15]) < 0.001f);
        khdays_g3d_set_zoom(0.87f);
        khdays_g3d_matrices(pos, vec, proj);
        CHECK(fabsf(proj[0] - 1.5f * 0.87f) < 0.01f);
        CHECK(fabsf(proj[5] - 2.0f * 0.87f) < 0.01f);
        CHECK(fabsf(proj[9] - 0.0f) < 0.001f);  /* the z output is the projection's own */
        CHECK(fabsf(proj[11] + 1.0f) < 0.001f); /* and so is w */
        /* the HUD's 3D is not the field of view's */
        khdays_g3d_set_hud(1);
        khdays_g3d_matrices(pos, vec, proj);
        CHECK(fabsf(proj[0] - 1.5f) < 0.001f && fabsf(proj[5] - 2.0f) < 0.001f);
        khdays_g3d_set_hud(0);
        khdays_g3d_command(0x16, orthographic);
        khdays_g3d_matrices(pos, vec, proj);
        CHECK(fabsf(proj[0] - 0.5f) < 0.001f && fabsf(proj[5] - 0.5f) < 0.001f);
    }

    if (failed != 0) {
        fprintf(stderr, "g3d zoom: %d failed\n", failed);
        return 1;
    }
    printf("g3d zoom: passed\n");
    return 0;
}
