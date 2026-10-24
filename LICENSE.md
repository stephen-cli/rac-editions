# Licensing

rac-editions is free software under the GNU General Public License, version 3
or (at your option) any later version ([COPYING](COPYING)), Copyright (c) 2026
Stephen Clinton and the rac-editions contributors. The one exception:

| Directory | License |
|---|---|
| [`common/openrac_editor`](common/openrac_editor/README.md) | MIT ([its LICENSE](common/openrac_editor/LICENSE)) for OpenRAC's disc readers at its top, carried unchanged; its `tools/openrac_common` and `games/*/game.json`, carried from OpenRAC with them, are GPL-3.0-or-later like the rest. |

Why the GPL: the editions are built from and with GPL code. Their code is
compiled into [OpenRAC](https://github.com/stephen-cli/OpenRAC)'s Ratchet &
Clank PAL decompilation (GPLv3), their patches against OpenRAC change that
code, and the installer carries [wrench](https://github.com/stephen-cli/wrench)
(GPLv3) to convert another game's data. Anyone who passes on the installer or
an edition's tools passes on their source with them, as the GPL requires; the
commits in [deps.json](deps.json) name the OpenRAC and wrench source each
release is built from.

None of this covers the games. rac-editions holds no game code or assets, and
nothing it releases may contain any: you make an edition from your own discs.
