#include "words.h"

#include <string.h>

const char *error_words(const char *code)
{
    static const struct {
        const char *code;
        const char *words;
    } WORDS[] = {
        {"unknown_disc", "This isn't a disc this edition uses."},
        {"wrong_version", "This is a different version of the game."},
        {"image_incomplete", "The disc image is incomplete (it ends early). Make the image again from the disc."},
        {"missing_disc", "The edition needs its base disc."},
        {"no_space", "There isn't enough free space on the hard drive for the new image."},
        {"not_writable", "The builder can't write the new image in PS2ISO."},
        {"cancelled", "The build was cancelled."},
    };
    for (size_t i = 0; code != NULL && i < sizeof WORDS / sizeof WORDS[0]; i++) {
        if (strcmp(WORDS[i].code, code) == 0) {
            return WORDS[i].words;
        }
    }
    return "Something went wrong in the installer.";
}

const char *region_name(const char *region)
{
    if (region == NULL) {
        return "";
    }
    if (strcmp(region, "PAL") == 0) {
        return "PAL (European)";
    }
    if (strcmp(region, "USA") == 0 || strcmp(region, "NTSC") == 0) {
        return "US";
    }
    if (strcmp(region, "JPN") == 0) {
        return "Japanese";
    }
    return region;
}

const Feature FEATURES[] = {
    {"charge_boots", "Charge Boots",
     "Going Commando's dash on a double tap of R1, unlocked by a cheat in Challenge Mode.",
     "Going Commando's dash on a double tap of R1, unlocked by a move-combo cheat in Challenge Mode."},
    {"strafing", "Strafing", "L2 and R2 strafe, with Going Commando's flips and backpedal.",
     "L2 and R2 strafe, with Going Commando's flips and backpedal. Players can still turn it off on the in-game "
     "Controls page."},
    {"quick_select_pages", "Quick Select pages", "Three rings, switched with R1 and L1.",
     "Three Quick Select rings, switched with R1 and L1. Off keeps retail's single ring."},
    {NULL, "Quick Select pause", "The action pauses while the ring is held open.", NULL},
    {NULL, "16:9 widescreen", "Levels, menus, cutscenes and the title screen.", NULL},
    {NULL, "Free movement after the wrench throw", "Run or jump out of the Comet-Strike.", NULL},
    {NULL, "Metal Detector", "10x bolts in Challenge Mode.", NULL},
    {NULL, "Randomizer", "In Goodies on the title screen, unlocked by beating Drek.", NULL},
    {NULL, "Movie volume", "Its own row on the Sound page.", NULL},
    {NULL, "Fixes", "Oltanis's lightning and Kalebo III's Gadgetron sign.", NULL},
};
const size_t FEATURE_COUNT = sizeof FEATURES / sizeof FEATURES[0];

const Feature *feature_for_switch(const char *id)
{
    for (size_t i = 0; id != NULL && i < FEATURE_COUNT; i++) {
        if (FEATURES[i].switch_id != NULL && strcmp(FEATURES[i].switch_id, id) == 0) {
            return &FEATURES[i];
        }
    }
    return NULL;
}

const Economy *economy_named(const char *id)
{
    static const Economy ECONOMIES[] = {
        {"skill", "Skill", "Drops as in a first playthrough, plus a bolt multiplier for kills without taking a hit."},
        {"classic", "Classic", "Retail's doubled drops in Challenge Mode."},
    };
    for (size_t i = 0; id != NULL && i < sizeof ECONOMIES / sizeof ECONOMIES[0]; i++) {
        if (strcmp(ECONOMIES[i].id, id) == 0) {
            return &ECONOMIES[i];
        }
    }
    return NULL;
}

const char *disc_purpose(const char *key)
{
    if (key != NULL && strcmp(key, "rac1") == 0) {
        return "Required. The edition is built from this.";
    }
    if (key != NULL && strcmp(key, "rac2") == 0) {
        return "Optional. Adds the Charge Boots and strafing.";
    }
    return "";
}

const char *const DESIGNED_NOTE = "The Executive Edition is designed to be played with everything here as it comes. "
                                  "Change these only if you want a different game.";
