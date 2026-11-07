/*
 * The edition's patch of the 19 levels' data (the installer's patch step; tools/gen_boots.py and
 * tools/patch_levels.py are the Python's).
 */
#ifndef RAC_LEVELS_H
#define RAC_LEVELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "build.h"
#include "installer.h"
#include "iso.h"
#include "job.h"

/*
 * The 19 levels' data on IMAGE, each level read, patched and written once: the seed code's glyphs in its three menu
 * fonts; the Charge Boots class, made from the discs, when FACTS' discs have Going Commando's and charge_boots is
 * not left out; Kalebo III's sign class SIGN (SIZE bytes, make_sign's; sign.h); the extra classes the Randomizer's
 * stand-ins need, where a level lacks them; then its core data compressed again. A level's new archive goes back in
 * its own sectors when it fits there, else at the image's end, and its header points at it. Progress: "patch", a
 * step per extra class read from its level and per level written.
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
