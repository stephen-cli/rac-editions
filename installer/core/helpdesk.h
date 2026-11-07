/*
 * The edition's HelpDesk lines: made from the player's own disc and put on the image (the installer's patch step;
 * tools/gen_helpdesk.py and tools/splice_helpdesk.py are the Python's).
 */
#ifndef RAC_HELPDESK_H
#define RAC_HELPDESK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "installer.h"
#include "iso.h"
#include "job.h"

/* The lines audio/helpdesk.json's cut lists (RECIPES, SIZE bytes; the payload's helpdesk.json) make from the
 * English HelpDesk lines on the PAL disc RAC1, but for the lines whose "use" is in LEFT_OUT_USES (a NULL-ended
 * list, or NULL), written at the end of IMAGE and entered in its help-audio table. Progress: "patch", a step per
 * line made and per line written. */
bool patch_helpdesk(IsoImage *image, const char *rac1, const uint8_t *recipes, size_t size,
                    const char *const *left_out_uses, Job *job, InstallError *error);

#endif
