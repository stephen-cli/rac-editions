# Ratchet & Clank editions

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
editions/   one folder per edition: its code (src/), pipeline (tools/), data, docs
common/     tools the editions share: apply_patches.py (writes sector patches into an image),
            rac2_anim.py (Going Commando's packed animations), wsl_linux_run.sh (runs Linux
            assemblers through WSL)
```

An edition can use another game's data, converted at build time from that
game's disc: the Executive Edition takes the Charge Boots and Ratchet's
strafing animations from Going Commando. A feature like that is only built
when that disc is supplied; without it the feature is left out entirely.

## Building

Each edition's README lists the discs and software it needs and the steps.
OpenRAC and the edition's fork of [wrench](https://github.com/stephen-cli/wrench)
sit beside this repository:

```
C:\rac\OpenRAC
C:\rac\wrench
C:\rac\rac1-executive-edition
```

A tool that builds an edition straight from your disc images, without the
development setup, is planned.

## Rules

Same as OpenRAC's: your own disc is the evidence; no Sony SDK source,
samples or headers; no leaked material; never commit disc images,
executables, extracted data or build output.
