# Patches against OpenRAC

Base: OpenRAC master commit 3f30fa97ef1df5b7a8fdd04f8154ba41ff2493d1
Branch: executive-edition

What the edition adds to OpenRAC, as one diff against the base:

     games/rac1/tools/boot_elf.py  | 120 ++++++++++++++++++++++++++
     games/rac1/tools/iso_patch.py | 191 ++++++++++++++++++++++++++++++++++++++++++
     2 files changed, 311 insertions(+)

Apply onto an OpenRAC checkout of master (the base or later) with:

    git -C OpenRAC apply ../rac1-executive-edition/patches/openrac-executive-edition.patch
