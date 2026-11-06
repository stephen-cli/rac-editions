/* The features a player may leave out (the builder's Edition settings; tools/payload.py SWITCHES).
 * The build has them all; the installer writes this word for the image it makes, with the bits of
 * the features that are in. A feature left out has no hooks in the level programs and no bytes
 * from Going Commando in this segment (its tables stay zero), so the code the other hooks share
 * asks here before it reaches any of it. */
#include "modseg.h"

const u32 ee_features = EE_FEATURE_CHARGE_BOOTS | EE_FEATURE_STRAFING | EE_FEATURE_QS_PAGES;
