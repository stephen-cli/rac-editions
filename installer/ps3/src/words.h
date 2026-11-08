/*
 * What the builder says, as the desktop builder says it (rac-editions-web apps/builder/src/editions/copy.ts and
 * stores/session.ts): the installer's errors in the player's words, the regions, and the Executive Edition's
 * features and settings. The installer says which features exist; what they are called is the builder's to say.
 */
#ifndef RAC_PS3_WORDS_H
#define RAC_PS3_WORDS_H

#include <stddef.h>

typedef struct {
    const char *switch_id;      /* the installer's switch, for a feature the player may leave out; else NULL */
    const char *name;
    const char *text;           /* one line for the review */
    const char *detail;         /* the fuller line in Edition settings, for a switchable feature */
} Feature;

typedef struct {
    const char *id;
    const char *label;
    const char *text;
} Economy;

/* The installer's error CODE in words; NULL or an unknown code is "Something went wrong". */
const char *error_words(const char *code);

/* A region as players say it: "PAL (European)", "US". */
const char *region_name(const char *region);

/* The Executive Edition's features, in the review's order. */
extern const Feature FEATURES[];
extern const size_t FEATURE_COUNT;

/* The feature for switch ID, or NULL. */
const Feature *feature_for_switch(const char *id);

/* Challenge Mode's economy ID ("skill", "classic"), or NULL. */
const Economy *economy_named(const char *id);

/* What disc KEY is for: "Required. The edition is built from this." */
const char *disc_purpose(const char *key);

/* The note at the top of Edition settings. */
extern const char *const DESIGNED_NOTE;

#endif
