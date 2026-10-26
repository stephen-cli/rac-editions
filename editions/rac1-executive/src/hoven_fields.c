/* Hoven's force fields block Ratchet.
 *
 * Three force fields on Hoven (class 1345, update func_L12_00309590,
 * OpenRAC's update_moby_draw_and_monitor_linked_state) each watch a set of
 * timed activation pads (class 886, the tri-pads; the pad's moby index is
 * the field's pvars word 0). While the pad is not done (state != 7) the
 * field stays in state 1: each frame it registers its draw callback
 * (draw_hoven_jittered_quad_layers, func_L12_00309730: five electric bars at
 * heights 1 to 5 of its own frame, each from y -3 to 3 in the plane x = 0)
 * when IsBSphereVisibleScaled(m, 48.0) (func_L01_00276680) says it is in
 * view. Once the pad reaches state 7 it clears its collision (moby + 0x94)
 * and goes to state 2. So the field is meant to block until the pads are
 * done, but in retail it never did: the class's collision data is a box
 * whose eight vertices all sit at z = +32767, which leaves a flat square, and
 * the class scale (1/4800) makes even that 1.3 cm across. Moby collision
 * vertices are 16-bit, in world units of scale / 1024 (the collision kernels
 * multiply the moby's rows by its scale, + 0x2C, against a position x 1024),
 * so no vertex values reach a 6 x 5 unit wall at that scale.
 *
 * The field's visibility call is redirected here (tools/overlay_patch.py
 * feature hoven_fields). On a field's first call the class's collision box
 * (shared by the three fields) is rewritten around the bars, keeping its
 * face list and face types (the crate's box topology: vertices 0-3 the top
 * ring, 4-7 the bottom one), and the field's scale becomes 0.25, which puts
 * 4096 vertex units in a world unit: x +-0.25, y +-3, z -0.5 to 5.5. Its
 * local bounding sphere (+ 0xF0, in the same units) is set to cover the box:
 * refresh_moby_spatial_bounds (func_0020ED48) builds the world sphere and the
 * moby grid cells the collision queries look in from it. The bars are drawn
 * by the callback from the field's position and rows alone, so the scale
 * does not change them. The class's own mesh, which MobyProc draws at the
 * moby's scale, is a 1 cm placeholder quad, invisible in retail; at 0.25 it
 * became a flickering grey slab about 12 units across, so the field also
 * gets mode bit 0x01 (+ 0x34), with which MobyProc does not draw a moby
 * (the parked ship uses it): the collision, the bounds and the callback are
 * not affected. When
 * the pads are done the field clears its collision as before and Ratchet
 * passes. Nothing is saved: the level's next load reads the disc data and
 * the hook installs the wall again. */
#include "modseg.h"

/* IsBSphereVisibleScaled(moby, scale): func_L01_00276680, Hoven's copy (the
 * function is in only some levels, so it has no LVL table). */
typedef s32 (*IsVisibleFn)(u32 moby, f32 scale);
#define HOVEN_IS_VISIBLE    ((IsVisibleFn)0x0027A230)
#define MOBY_SCALE          0x2C
#define MOBY_MODE           0x34       /* u16 mode bits */
#define MODE_HIDDEN         0x01       /* MobyProc does not draw the moby */
#define MOBY_COLLISION      0x94       /* the class's collision blob, cleared once the pads are done */
#define MOBY_LOCAL_SPHERE   0xF0       /* x, y, z, radius in vertex units */

#define FIELD_SCALE         0.25f
#define UNITS               4096.0f    /* vertex units a world unit at FIELD_SCALE (1024 / 0.25) */
#define HALF_THICKNESS      0.25f      /* local x */
#define HALF_WIDTH          3.0f       /* local y: the bars' length */
#define BOTTOM              (-0.5f)    /* local z: below the floor, up past the top bar (5) */
#define TOP                 5.5f

/* The blob: +4 primitive bytes, +8 face bytes, +0xC vertex bytes, then from
 * +0x10 the primitives, the vertices (x, y, z, 0 as s16) and the faces. */
#define BLOB_PRIM_BYTES     0x4
#define BLOB_FACE_BYTES     0x8
#define BLOB_VERTEX_BYTES   0xC
#define BLOB_DATA           0x10
#define BOX_VERTEX_BYTES    (8 * 8)
#define BOX_FACE_BYTES      (12 * 4)

static void install_box(u32 blob)
{
    s16 x = (s16)(HALF_THICKNESS * UNITS);
    s16 y = (s16)(HALF_WIDTH * UNITS);
    s16 lo = (s16)(BOTTOM * UNITS);
    s16 hi = (s16)(TOP * UNITS);
    const s16 box[8][4] = {
        { -x, -y, hi, 0 }, { x, -y, hi, 0 }, { x, y, hi, 0 }, { -x, y, hi, 0 },
        { x, y, lo, 0 }, { -x, y, lo, 0 }, { x, -y, lo, 0 }, { -x, -y, lo, 0 },
    };
    volatile s16 *v = (volatile s16 *)(blob + BLOB_DATA + *(volatile s32 *)(blob + BLOB_PRIM_BYTES));
    s32 i, k;
    for (i = 0; i < 8; i++) {
        for (k = 0; k < 4; k++) {
            v[4 * i + k] = box[i][k];
        }
    }
}

s32 ee_hoven_field_visible(u32 moby, f32 scale)
{
    volatile f32 *moby_scale = (volatile f32 *)(moby + MOBY_SCALE);
    u32 blob = *(volatile u32 *)(moby + MOBY_COLLISION);

    if (blob != 0 && *moby_scale != FIELD_SCALE
        && *(volatile s32 *)(blob + BLOB_VERTEX_BYTES) == BOX_VERTEX_BYTES
        && *(volatile s32 *)(blob + BLOB_FACE_BYTES) == BOX_FACE_BYTES) {
        volatile f32 *sphere = (volatile f32 *)(moby + MOBY_LOCAL_SPHERE);
        f32 half_height = (TOP - BOTTOM) / 2.0f;
        install_box(blob);
        *(volatile u16 *)(moby + MOBY_MODE) |= MODE_HIDDEN;
        *moby_scale = FIELD_SCALE;
        sphere[0] = 0.0f;
        sphere[1] = 0.0f;
        sphere[2] = (BOTTOM + half_height) * UNITS;
        /* half the box's diagonal: sqrt(0.25^2 + 3^2 + 3^2) = 4.25 units */
        sphere[3] = 4.25f * UNITS;
    }
    return HOVEN_IS_VISIBLE(moby, scale);
}
