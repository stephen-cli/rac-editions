# OpenRAC's disc readers

Copied unchanged from OpenRAC at commit `a4edca52f5f9568b63f4819b3f4aa097f331abc0`
(https://github.com/stephen-cli/OpenRAC): the readers in its `editor/`, at the top here, under the
MIT license ([LICENSE](LICENSE)); the `tools/openrac_common` helpers they
import and the `games/*/game.json` manifests they read, under the GNU GPL v3
or later, as rac-editions is. The editions' tools import these instead of an
OpenRAC checkout, so making an edition from a player's discs needs no OpenRAC.
The `STANDALONE` note marks this directory as the readers' OpenRAC root, as
in the launcher's bundled copy.

Do not edit them here: change them in OpenRAC, move the pin
(`python common/deps.py lock`) and copy them again
(`python common/deps.py vendor`). `python common/deps.py check` reports a
copy that differs from the pinned commit's.
