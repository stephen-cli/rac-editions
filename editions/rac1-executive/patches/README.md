# Patches against OpenRAC

Base: OpenRAC master commit 121a20688bd3f6584b13bedb89fc21b55ac190d3
Branch: executive-edition

What the edition adds to OpenRAC, as one diff against the base:

     games/rac1/src/game/transition.c |  12 +--
     games/rac1/tools/boot_elf.py     | 120 ++++++++++++++++++++++++
     games/rac1/tools/iso_patch.py    | 191 +++++++++++++++++++++++++++++++++++++++
     3 files changed, 317 insertions(+), 6 deletions(-)

Apply onto an OpenRAC checkout of master (the base or later) with:

    git -C OpenRAC apply ../rac1-executive-edition/editions/rac1-executive/patches/openrac-executive-edition.patch
