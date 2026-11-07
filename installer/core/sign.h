/*
 * Kalebo III's rotating sign, reading GADGETRON again as on the NTSC disc: moby class 1891 rebuilt from the PAL
 * disc's own GALACTRON sign (the installer's patch step; tools/gen_sign.py is the Python's).
 */
#ifndef RAC_SIGN_H
#define RAC_SIGN_H

#include <stddef.h>
#include <stdint.h>

#include "installer.h"

/*
 * The Gadgetron sign's class core (a new array of *SIZE bytes), made from Kalebo III's class 1891 on the PAL disc
 * RAC1 and read back to check it: the class patch_levels splices into Kalebo III. NULL with ERROR set when the disc's
 * class is not the PAL sign or the new class does not check.
 */
uint8_t *make_sign(const char *rac1, size_t *size, InstallError *error);

#endif
