# Patches against OpenRAC

Base: OpenRAC master commit 07c23356bfe6da8cc7b24377d3b7c61f39517fd0
Branch: executive-edition

What the edition adds to OpenRAC, as one diff against the base:

     games/rac1/tools/boot_elf.py  | 120 ++++++++++++++++++++++++++
     games/rac1/tools/iso_patch.py | 191 ++++++++++++++++++++++++++++++++++++++++++
     tools/shared.py               |   2 +-
     3 files changed, 312 insertions(+), 1 deletion(-)

Apply onto an OpenRAC checkout of master (the base or later) with:

    git -C OpenRAC apply ../rac-editions/editions/rac1-executive/patches/openrac-executive-edition.patch
