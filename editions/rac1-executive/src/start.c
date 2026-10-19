/* The segment's first bytes: a marker the tools and a memory viewer can
 * recognise, and an entry symbol for the linker. Nothing jumps here. */
#include "modseg.h"

const char modseg_start[16] = "EXEC-ED MODSEG\0";
