# Patches against OpenRAC

Base: OpenRAC master commit 9fc860d5ce34b6524e928ca1b6ed2a94ff03e5f2
Branch: executive-edition

What the edition adds to OpenRAC, as one diff against the base:

     games/rac1/src/game/system/loaders.c |  10 +-
     games/rac1/tools/boot_elf.py         | 120 ++++++++++++++++++++++
     games/rac1/tools/gcc323.py           |  11 +-
     games/rac1/tools/iso_patch.py        | 191 +++++++++++++++++++++++++++++++++++
     tools/shared.py                      |   2 +-
     5 files changed, 327 insertions(+), 7 deletions(-)

Apply onto an OpenRAC checkout of master (the base or later) with:

    git -C OpenRAC apply ../rac-editions/editions/rac1-executive/patches/openrac-executive-edition.patch
