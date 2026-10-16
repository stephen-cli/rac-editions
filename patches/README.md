# Patches against OpenRAC

Base: OpenRAC master commit 29eaa3c13e6dd231d6bbf107e4c17936ca6d9db9
Branch: executive-edition

Apply onto a fresh OpenRAC checkout at the base commit with:

    git -C OpenRAC switch -c executive-edition && git -C OpenRAC am ../rac1-executive-edition/patches/*.patch

- 0001-feat-rac1-pal-bootable-executable-and-in-place-disc-.patch
