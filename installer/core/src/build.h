/*
 * build: an edition's image made from a release payload and the player's discs, as tools/install.py's build makes
 * it. In order: the discs are checked by hash; the mod segment gets its holes filled from the discs (fill.h), the
 * features word and the bytes of the features left out, and goes into the boot executable as a second loadable
 * segment, with the executable's own changed words; the image is a copy of the base disc with that executable in
 * it and the level programs' changed words written in place; then the patch stage (the levels' data and the
 * HelpDesk lines) runs on the image. Updating the base disc's image in place makes the image beside it first,
 * then writes the restore file and moves the image over the original.
 */
#ifndef RAC_BUILD_H
#define RAC_BUILD_H

#include <stdbool.h>

#include "hash_cache.h"
#include "installer.h"
#include "iso.h"
#include "job.h"
#include "json.h"

/* What the patch stage is told of the build. */
typedef struct {
    const Payload *payload;
    const Json *discs;                  /* disc ids to image paths */
    const char *const *left_out;        /* the switch ids left out, NULL-ended */
    const char *const *left_out_lines;  /* the HelpDesk uses left out (the switches' helpdesk), NULL-ended */
} BuildFacts;

/* The patch stage, run on the open image after its executable and words; reports in stage "patch". */
typedef bool BuildPatch(void *context, IsoImage *image, const BuildFacts *facts, Job *job, InstallError *error);

/*
 * The edition's patch stage, a BuildPatch: the levels' data (levels.h), then the HelpDesk lines (the switches'
 * left-out uses not made), in one "patch" progress of their shares. CONTEXT is unused.
 */
bool build_patch(void *context, IsoImage *image, const BuildFacts *facts, Job *job, InstallError *error);

/*
 * A build with DISCS, OPTIONS and OUTPUT as the protocol's build command asks for it: its answer is
 * {"path", "sha256"} of the image made. PATCH (with CONTEXT) is the patch stage; NULL leaves it out, which makes
 * the image as it stands before that stage.
 */
bool command_build(const Payload *payload, HashCache *cache, const Json *discs, const Json *options,
                   const Json *output, BuildPatch *patch, void *context, Job *job, Text *answer, InstallError *error);

#endif
