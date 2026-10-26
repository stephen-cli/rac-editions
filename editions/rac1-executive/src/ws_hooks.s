/* q4: 16:9 entry points (src/widescreen.c). Numeric register names: the SN
 * assembler takes no aliases. */
.set noreorder
.set noat
.text

/* Entry of every level's UpdateViewContext (func_001F3140), patched to
 * `j ee_view_hook` with `addiu $1, $0, LEVEL` in the delay slot
 * (tools/overlay_patch.py widescreen). The function takes no arguments and
 * returns nothing. ee_view_pre_c(level) prepares the view record and
 * returns the level's UpdateViewContext + 8; the hook runs the two words
 * the patch replaced (addiu $sp,$sp,-0x60; lui $v0,0x16, the same in all
 * 19 levels), calls the rest of the function with its return address
 * pointing back here, and lets ee_view_post_c put the record's field of
 * view back before returning to the caller. */
.globl ee_view_hook
.extern ee_view_pre_c
.extern ee_view_post_c
ee_view_hook:
    addiu $29, $29, -16
    sd    $31, 0($29)
    jal   ee_view_pre_c
    addu  $4, $1, $0                /* the level id from the patch's delay slot */
    addu  $25, $2, $0               /* UpdateViewContext + 8 */
    lui   $2, 0x16                  /* replaced word 1 */
    jalr  $25
    addiu $29, $29, -0x60           /* replaced word 0, in the delay slot */
    jal   ee_view_post_c
    nop
    ld    $31, 0($29)
    jr    $31
    addiu $29, $29, 16

/* DrawWorld's calls of HudDraw, draw_help, the letterbox bars and the
 * subtitles: none takes an argument, so the return address goes in $4. */
.globl ee_ws_hud
.extern ee_ws_hud_c
ee_ws_hud:
    j     ee_ws_hud_c
    addu  $4, $31, $0

.globl ee_ws_help
.extern ee_ws_help_c
ee_ws_help:
    j     ee_ws_help_c
    addu  $4, $31, $0

.globl ee_ws_letterbox
.extern ee_ws_letterbox_c
ee_ws_letterbox:
    j     ee_ws_letterbox_c
    addu  $4, $31, $0

/* The pause renderer's panel draw, func_0020E180(moby, count): the return
 * address goes in $6. */
.globl ee_ws_panel_draw
.extern ee_ws_panel_draw_c
ee_ws_panel_draw:
    j     ee_ws_panel_draw_c
    addu  $6, $31, $0

/* setImageTag(tags, frame, width, height) from the movie decoder: four
 * arguments in $4-$7, the return address goes in $8. */
.globl ee_ws_movie_tag
.extern ee_ws_movie_tag_c
ee_ws_movie_tag:
    j     ee_ws_movie_tag_c
    addu  $8, $31, $0

.globl ee_ws_pillars
.extern ee_ws_pillars_c
ee_ws_pillars:
    j     ee_ws_pillars_c
    addu  $4, $31, $0

/* The projection of a panel's or a vendor monitor's corners,
 * func_00238D90(first, opposite, &w, &h, &x, &y): six arguments, $4-$9;
 * the return address goes in $10 and which caller in $11 (0 the pause
 * renderer, 1 the vendor's monitors). */
.globl ee_ws_panel_project
.extern ee_ws_panel_project_c
ee_ws_panel_project:
    addiu $11, $0, 0
    j     ee_ws_panel_project_c
    addu  $10, $31, $0

.globl ee_ws_vendor_project
ee_ws_vendor_project:
    addiu $11, $0, 1
    j     ee_ws_panel_project_c
    addu  $10, $31, $0

/* A bracketed call: a per-site stub below puts its site number in $25 and
 * jumps here in place of the call. The arguments ($4-$11, $f12-$f15) and
 * the return address go to a static area, not the stack, so that a callee
 * with arguments on the stack (DrawTexturedQuad has ten) finds them where
 * the caller put them. ee_ws_bracket_begin_c(site, return address) returns
 * the function to call and notes the VIF chain's cursor; the call is made
 * with the arguments put back; ee_ws_bracket_end_c squeezes what it drew.
 * Not reentrant: no bracketed call runs inside another. */
.globl ee_ws_bracket
.extern ee_ws_bracket_begin_c
.extern ee_ws_bracket_end_c
.extern ee_ws_saved_args
ee_ws_bracket:
    lui   $1, %hi(ee_ws_saved_args)
    addiu $1, $1, %lo(ee_ws_saved_args)
    sd    $4, 0($1)
    sd    $5, 8($1)
    sd    $6, 16($1)
    sd    $7, 24($1)
    sd    $8, 32($1)
    sd    $9, 40($1)
    sd    $10, 48($1)
    sd    $11, 56($1)
    swc1  $f12, 64($1)
    swc1  $f13, 68($1)
    swc1  $f14, 72($1)
    swc1  $f15, 76($1)
    sw    $31, 80($1)
    lw    $5, 80($1)
    jal   ee_ws_bracket_begin_c
    addu  $4, $25, $0
    lui   $1, %hi(ee_ws_saved_args)
    addiu $1, $1, %lo(ee_ws_saved_args)
    addu  $25, $2, $0
    ld    $4, 0($1)
    ld    $5, 8($1)
    ld    $6, 16($1)
    ld    $7, 24($1)
    ld    $8, 32($1)
    ld    $9, 40($1)
    ld    $10, 48($1)
    ld    $11, 56($1)
    lwc1  $f12, 64($1)
    lwc1  $f13, 68($1)
    lwc1  $f14, 72($1)
    jalr  $25
    lwc1  $f15, 76($1)
    lui   $1, %hi(ee_ws_saved_args)
    addiu $1, $1, %lo(ee_ws_saved_args)
    sd    $2, 88($1)
    jal   ee_ws_bracket_end_c
    sd    $3, 96($1)
    lui   $1, %hi(ee_ws_saved_args)
    addiu $1, $1, %lo(ee_ws_saved_args)
    ld    $2, 88($1)
    ld    $3, 96($1)
    lw    $31, 80($1)
    jr    $31
    nop

/* The bracketed sites (src/widescreen.c WS_SITE_*). */
.globl ee_ws_br_pause_fill
ee_ws_br_pause_fill:
    j     ee_ws_bracket
    addiu $25, $0, 1
.globl ee_ws_br_pause_direct
ee_ws_br_pause_direct:
    j     ee_ws_bracket
    addiu $25, $0, 2
.globl ee_ws_br_pause_quad
ee_ws_br_pause_quad:
    j     ee_ws_bracket
    addiu $25, $0, 3
.globl ee_ws_br_pause_last
ee_ws_br_pause_last:
    j     ee_ws_bracket
    addiu $25, $0, 4
.globl ee_ws_br_vendor_quad
ee_ws_br_vendor_quad:
    j     ee_ws_bracket
    addiu $25, $0, 5
.globl ee_ws_br_vendor_hud
ee_ws_br_vendor_hud:
    j     ee_ws_bracket
    addiu $25, $0, 6
.globl ee_ws_br_screens_quad_a
ee_ws_br_screens_quad_a:
    j     ee_ws_bracket
    addiu $25, $0, 7
.globl ee_ws_br_screens_quad_b
ee_ws_br_screens_quad_b:
    j     ee_ws_bracket
    addiu $25, $0, 8
.globl ee_ws_br_quit_prompt
ee_ws_br_quit_prompt:
    j     ee_ws_bracket
    addiu $25, $0, 9
.globl ee_ws_br_credits_name
ee_ws_br_credits_name:
    j     ee_ws_bracket
    addiu $25, $0, 10
.globl ee_ws_br_planet_name
ee_ws_br_planet_name:
    j     ee_ws_bracket
    addiu $25, $0, 11
.globl ee_ws_br_story_card
ee_ws_br_story_card:
    j     ee_ws_bracket
    addiu $25, $0, 12
.globl ee_ws_br_title
ee_ws_br_title:
    j     ee_ws_bracket
    addiu $25, $0, 13
.globl ee_ws_br_reticle
ee_ws_br_reticle:
    j     ee_ws_bracket
    addiu $25, $0, 14

/* The pause renderer's render target, func_001F7A50(a, b, flag, f) at
 * func_L00_002781D8 + 0x458: the return address goes in $7 ($f12 keeps f). */
.globl ee_ws_panel_target
.extern ee_ws_panel_target_c
ee_ws_panel_target:
    j     ee_ws_panel_target_c
    addu  $7, $31, $0

/* The pause renderer's texture pass, jalr $v0 (func_L00_002781D8 + 0x49C):
 * the panel in $4, its callback in $2; the return address goes in $6. */
.globl ee_ws_panel_texture
.extern ee_ws_panel_texture_c
ee_ws_panel_texture:
    addu  $5, $2, $0
    j     ee_ws_panel_texture_c
    addu  $6, $31, $0

/* The credits' picture, func_001F7680(source): one argument, the return
 * address goes in $5. */
.globl ee_ws_credits_picture
.extern ee_ws_credits_picture_c
ee_ws_credits_picture:
    j     ee_ws_credits_picture_c
    addu  $5, $31, $0
/* The HUDs drawn by a draw callback (the turrets', the ship levels'): site 32 + the level. */
.globl ee_ws_br_hud_8
ee_ws_br_hud_8:
    j     ee_ws_bracket
    addiu $25, $0, 40
.globl ee_ws_br_hud_11
ee_ws_br_hud_11:
    j     ee_ws_bracket
    addiu $25, $0, 43
.globl ee_ws_br_hud_12
ee_ws_br_hud_12:
    j     ee_ws_bracket
    addiu $25, $0, 44
.globl ee_ws_br_hud_13
ee_ws_br_hud_13:
    j     ee_ws_bracket
    addiu $25, $0, 45
.globl ee_ws_br_hud_17
ee_ws_br_hud_17:
    j     ee_ws_bracket
    addiu $25, $0, 49

/* The Trespasser puzzle's ring draw callback: site 64 + the level. */
.globl ee_ws_br_tres_2
ee_ws_br_tres_2:
    j     ee_ws_bracket
    addiu $25, $0, 66
.globl ee_ws_br_tres_4
ee_ws_br_tres_4:
    j     ee_ws_bracket
    addiu $25, $0, 68
.globl ee_ws_br_tres_6
ee_ws_br_tres_6:
    j     ee_ws_bracket
    addiu $25, $0, 70
.globl ee_ws_br_tres_8
ee_ws_br_tres_8:
    j     ee_ws_bracket
    addiu $25, $0, 72
.globl ee_ws_br_tres_11
ee_ws_br_tres_11:
    j     ee_ws_bracket
    addiu $25, $0, 75
.globl ee_ws_br_tres_13
ee_ws_br_tres_13:
    j     ee_ws_bracket
    addiu $25, $0, 77
.globl ee_ws_br_tres_18
ee_ws_br_tres_18:
    j     ee_ws_bracket
    addiu $25, $0, 82

/* The countdowns' draw callbacks (Blarg Station's self-destruct, Quartu,
 * Drek's Fleet), registered as these: the moby in $4, the level in $5; the
 * C notes them and draws the countdown after the HUD (src/widescreen.c). */
.globl ee_ws_timer_6
.extern ee_ws_timer_defer_c
ee_ws_timer_6:
    j     ee_ws_timer_defer_c
    addiu $5, $0, 6
.globl ee_ws_timer_15
ee_ws_timer_15:
    j     ee_ws_timer_defer_c
    addiu $5, $0, 15
.globl ee_ws_timer_17
ee_ws_timer_17:
    j     ee_ws_timer_defer_c
    addiu $5, $0, 17

/* The vendor render's call of VendorDrawScreens(m): the return address goes in $5. */
.globl ee_ws_vendor_screens
.extern ee_ws_vendor_screens_c
ee_ws_vendor_screens:
    j     ee_ws_vendor_screens_c
    addu  $5, $31, $0

/* VendorDrawScreens' projections, func_00238F98(a, b, &w, &h, &x, &y):
 * six arguments, the return address goes in $10. */
.globl ee_ws_screens_project
.extern ee_ws_screens_project_c
ee_ws_screens_project:
    j     ee_ws_screens_project_c
    addu  $10, $31, $0

.globl ee_ws_subtitles
.extern ee_ws_subtitles_c
ee_ws_subtitles:
    j     ee_ws_subtitles_c
    addu  $4, $31, $0

/* The title screen's calls of the game-state reset (func_00209DC0), in the
 * boot executable and in frontbin (tools/gen_frontend.py): the reset keeps
 * the 16:9 setting (src/widescreen.c, ee_fe_reset_c, given the program). */
.globl ee_fe_reset_19
.extern ee_fe_reset_c
ee_fe_reset_19:
    j     ee_fe_reset_c
    addiu $4, $0, 19
.globl ee_fe_reset_20
ee_fe_reset_20:
    j     ee_fe_reset_c
    addiu $4, $0, 20
