/*
 * The edition's patch of the 19 levels' data (the installer's patch step; tools/gen_boots.py and
 * tools/patch_levels.py are the Python's).
 */
#ifndef RAC_LEVELS_H
#define RAC_LEVELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "build.h"
#include "installer.h"
#include "iso.h"
#include "job.h"

#define LEVEL_COUNT 19

/*
 * The patch of a level's data: the seed code's glyphs in its three menu fonts; the Charge Boots class, made from the
 * discs, when FACTS' discs have Going Commando's and charge_boots is not left out; Kalebo III's sign class SIGN
 * (SIZE bytes, make_sign's; sign.h); the extra classes the Randomizer's stand-ins need, where a level lacks them;
 * then its core data compressed again. Progress: "patch", a step per extra class read from its level and per level
 * patched.
 *
 * A plan is what every level's patch shares: the levels' places, the boots and the extra classes, read from SOURCE,
 * an image whose levels are the disc's (the PAL disc itself, or a copy of it not yet patched). SOURCE and SIGN stay
 * the caller's and must outlive the plan. NULL with ERROR set when it cannot be made.
 */
typedef struct LevelsPlan LevelsPlan;

LevelsPlan *levels_plan(FILE *source, const BuildFacts *facts, const uint8_t *sign, size_t size, Job *job,
                        InstallError *error);
void levels_plan_free(LevelsPlan *plan);

/* Level NUMBER's span on the disc: its header sector up to the end of its archive, COUNT sectors from FIRST. The
 * levels are 0 to LEVEL_COUNT - 1; their spans do not overlap. */
void levels_span(const LevelsPlan *plan, int number, uint32_t *first, uint32_t *count);

/*
 * Level NUMBER patched in SPAN (SIZE bytes: its span as the disc has it, levels_span's): its new archive
 * (*ARCHIVE_SIZE bytes, whole sectors) in the archive's own sectors, the rest of them zeroed, and its header's
 * length set to it. Fails ("internal") when the new archive does not fit there, leaving SPAN as it was. The
 * payload's overlay.tsv words that fall in the span are the caller's to write into it first, as the build writes them
 * on the copied image before patch_levels: they are in the levels' code, which the patch carries over.
 */
bool levels_produce(LevelsPlan *plan, int number, uint8_t *span, size_t size, size_t *archive_size,
                    InstallError *error);

/*
 * Every level patched on IMAGE (a plan from it, then each level read, patched and written once). A level's new archive
 * goes back in its own sectors when it fits there, else at the image's end, and its header points at it.
 */
bool patch_levels(IsoImage *image, const BuildFacts *facts, const uint8_t *sign, size_t size, Job *job,
                  InstallError *error);

/*
 * Class ID's core (a new array of *SIZE bytes) as level NUMBER holds it on the PAL disc RAC1: from its blob's
 * start up to the next class's (or the core's resident part's end). *TEXTURE_COUNT: the textures its class table
 * entry lists, each checked to have its pixels and palette in the level. NULL with ERROR set when it cannot be read.
 */
uint8_t *disc_class(const char *rac1, int number, int32_t id, size_t *size, size_t *texture_count,
                    InstallError *error);

#endif
