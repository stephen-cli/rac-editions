# rac-editions

Definitive-style editions of the PlayStation 2 Ratchet & Clank games, within
PS2 hardware limits, built on the [OpenRAC](https://github.com/OpenRAC/OpenRAC)
decompilations. You build them yourself from your own discs: this repository
holds only the editions' code, tools and notes, never game data.

| Edition | Game | Folder | State |
|---|---|---|---|
| Executive Edition | Ratchet & Clank (PAL) | [editions/rac1-executive](editions/rac1-executive/README.md) | in development |
| Loaded Edition | Ratchet & Clank: Going Commando | `editions/rac2-loaded` | planned |
| Titanium Edition | Ratchet & Clank 3 | `editions/rac3-titanium` | planned |

## Layout

```
editions/    one folder per edition: its code (src/), pipeline (tools/), data, docs
common/      tools the editions share: apply_patches.py (writes sector patches into an image),
             rac2_anim.py (Going Commando's packed animations), wsl_linux_run.sh (runs Linux
             assemblers through WSL)
installer/   the end-user tool that makes an edition from your disc images (planned)
```

## The installer

[installer/](installer/README.md) will hold the tool players run. You give it
your own disc images and it writes the edition's image, setting itself up as
far as it can: no compilers, decompilation build or development setup on your
side. The editions are built here; the installer applies them to your discs.

What you get depends on the discs you give it. An edition can use another
game's data, converted from that game's disc while the installer runs: the
Executive Edition takes the Charge Boots and Ratchet's strafing animations
from Going Commando. Give it only Ratchet & Clank and those features are left
out entirely; give it Going Commando as well and they are built in.

## Building an edition (development)

Each edition's README lists the discs and software it needs and the steps.
OpenRAC and the editions' fork of [wrench](https://github.com/stephen-cli/wrench)
sit beside this repository:

```
C:\rac\OpenRAC
C:\rac\wrench
C:\rac\rac-editions
```

## Rules

Same as OpenRAC's: your own disc is the evidence; no Sony SDK source,
samples or headers; no leaked material; never commit disc images,
executables, extracted data or build output. Nothing the installer ships
may contain any game's data.
