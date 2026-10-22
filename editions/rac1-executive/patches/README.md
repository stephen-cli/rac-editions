# Patches against OpenRAC

Base: OpenRAC master commit 041d19911e50974932f65aed149fd79778f1b71c
Branch: executive-edition

What the edition adds to OpenRAC, as one diff against the base:

     games/rac1/src/game/transition.c |  12 +--
     games/rac1/tools/boot_elf.py     | 120 ++++++++++++++++++++++++
     games/rac1/tools/iso_patch.py    | 191 +++++++++++++++++++++++++++++++++++++++
     tools/shared.py                  |   2 +-
     4 files changed, 318 insertions(+), 7 deletions(-)

Apply onto an OpenRAC checkout of master (the base or later) with:

    git -C OpenRAC apply ../rac-editions/editions/rac1-executive/patches/openrac-executive-edition.patch
