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

/* A line made: its VAG file and where it goes on the image. */
typedef struct {
    int slot;
    char *name;
    uint8_t *vag;               /* padded to whole sectors */
    size_t size;
    uint32_t sector;            /* its first sector, helpdesk_place's */
    uint64_t entry;             /* the image's byte offset of its help-audio table word, which holds SECTOR (u32 LE) */
} HelpdeskLine;

typedef struct {
    HelpdeskLine *lines;        /* in slot order */
    size_t count;
} HelpdeskLines;

/* The lines audio/helpdesk.json's cut lists (RECIPES, SIZE bytes; the payload's helpdesk.json) make from the
 * English HelpDesk lines on the PAL disc RAC1, but for the lines whose "use" is in LEFT_OUT_USES (a NULL-ended
 * list, or NULL), in OUT (none when every line is left out). Progress: "patch", a step per line made. */
bool helpdesk_make(const char *rac1, const uint8_t *recipes, size_t size, const char *const *left_out_uses, Job *job,
                   HelpdeskLines *out, InstallError *error);

/* LINES placed one after another from FIRST_SECTOR, each line's SECTOR set; returns the sectors they take. */
uint32_t helpdesk_place(HelpdeskLines *lines, uint32_t first_sector);

void helpdesk_free(HelpdeskLines *lines);

/* The lines made (helpdesk_make), written at the end of IMAGE and entered in its help-audio table. Progress:
 * "patch", a step per line made and per line written. */
bool patch_helpdesk(IsoImage *image, const char *rac1, const uint8_t *recipes, size_t size,
                    const char *const *left_out_uses, Job *job, InstallError *error);

#endif
