# The PS3 builder

A PS3 homebrew app that makes an edition on the console itself, from the
player's own PS2 disc images on the PS3's hard drive. It links the
installer core (`installer/core`), so the image it writes is the same,
byte for byte, as the desktop builder's and `tools/install.py`'s.

- **Discs:** the images in `/dev_hdd0/PS2ISO`, where webMAN MOD and
  multiMAN list PS2 games. Only an image whose boot file the edition knows
  (`edition.json`'s `boot_files`) is hashed; the rest of a game library is
  left alone. The edition's discs are picked from what is found.
- **The new image:** written to `/dev_hdd0/PS2ISO` too, named as the
  desktop builder names it (`Ratchet & Clank Executive Edition.iso`), so it
  shows up beside the original. The internal drive holds a 4.22 GB image; a
  USB drive in FAT32 cannot.
- **Its cover:** webMAN MOD shows an image with the picture of the same
  name beside it (`.JPG`, `.PNG`, `.jpg` or `.png`, looked for in that
  order). The builder copies the first disc's cover to the new image's
  name, keeping its extension, unless the new image has a cover already.
- **Edition settings:** Challenge Mode's economy and the features a player
  may switch off, as in the desktop builder, on the review screen.
- **Controls:** the controller only, no text entry. Cross or Circle confirms
  as the console is set.
- **Version:** the payload's, which the top bar shows and the XMB reads from
  `PARAM.SFO`'s `APP_VER`: a release's tag (`v0.3.0`), or between releases
  the commit. The XMB's form is two digits and two digits, so it shows the
  major and minor numbers (`00.03`) and leaves out the patch number; a
  build between releases shows `00.00`.
- **Updates:** a release build asks GitHub for the latest rac-editions
  release at start and offers a newer one on the first screen (Triangle).
  Its `EditionsBuilder.pkg` is downloaded to `/dev_hdd0/packages`, for
  Package Manager to install over this one.

| Path | What it is |
|---|---|
| `src/` | the app: `main.c`, the screens, the session (its state and the commands it sends), the in-process client, drawing, the words |
| `src/platform_ps3.c` | the controller (io/pad), the PS button's Quit Game (sysutil), the folders |
| `src/platform_pc.c` | the preview: the same app in a window on a computer, for working on the screens |
| `src/script.c` | test scripts: the app driven without a player, with screenshots |
| `src/lv2_printf.c` | C99's `%zu` for the toolchain's printf, and the log (`USRDIR/log.txt`) |
| `src/update.c`, `src/net.h`, `src/net_ps3.c`, `src/net_pc.c` | the update check and download; HTTPS through libcurl and mbedTLS on the PS3, none in the preview |
| `src/spu.c`, `src/wad_spu.c`, `src/sha_spu.c`, `spu/` | the core's level compression and SHA-256 on the SPUs |
| `src/draw.h`, `src/draw_rsx.c`, `src/draw_sdl.c` | drawing: the RSX on the PS3, SDL's renderer in the preview |
| `tests/smoke.c` | the core on the PS3, headless: identify, plan and build through the in-process server, to the TTY |
| `data/` | the fonts (Chakra Petch, Barlow; SIL OFL), the XMB icon (`tools/icon.c` draws it), and `cacert.pem`, Mozilla's root certificates as curl publishes them (MPL 2.0) |
| `tools/embed.py` | an SPU program's ELF as a C array, linked into the app |
| `Containerfile`, `Makefile` | the toolchain and the PS3 build |
| `CMakeLists.txt` | the preview and the icon tool, on a computer with SDL2 |

## How it is built

The toolchain is ps3dev's (ps3toolchain's PPU GCC 7.2, PSL1GHT and
ps3libraries) in a container pinned by `Containerfile`; the prebuilt
compiler needs Ubuntu 24.04's glibc.

```sh
podman build --platform linux/amd64 -t rac-ps3dev installer/ps3
podman run --rm -v "$PWD:/src" -v "<payload>:/payload:ro" -w /src/installer/ps3 rac-ps3dev make app PAYLOAD=/payload
```

`build/app/` is then the game folder (`PARAM.SFO`, `ICON0.PNG`,
`USRDIR/EBOOT.BIN`, `USRDIR/payload`, `USRDIR/fonts`,
`USRDIR/cacert.pem`); copied to
`dev_hdd0/game/RACE00001`, the PS3 or RPCS3 runs it. `make smoke` makes the
headless test the same way, as `RACT00001`.

The app talks to the core in-process: the core's `Server` (`src/serve.h`)
takes the protocol's command lines and sends its event lines to a
function, so the PS3 app drives the same commands (`editions`, `identify`,
`plan`, `build`, `cancel`) as the desktop builder does over
`rac-installer --serve`.

What the core needs from the PS3, beside what C gives:

| Need | On the PS3 |
|---|---|
| threads and locks (`src/thread.c`) | lv2 PPU threads and mutexes; 2 processors, the PPU's hardware threads |
| files (`src/files.c`) | stdio streams over lv2's own 64-bit file calls (`fopencookie`): the toolchain's stdio cuts a seek to a 4 GB image's end to 32 bits, and the stream then reads nothing |
| file sizes and times | `sysLv2FsStat`; lv2 keeps whole seconds |
| a path's real spelling (`file_resolve`) | the path as given: lv2 has no links |
| replacing a file | the old one removed first: lv2 renames only onto a free name |
| free space | lv2's `sys_fs_disk_free` (syscall 840), as webMAN MOD reads it: PSL1GHT's `sysFsGetFreeSize` is the sys_fs library's, which a console hangs in unless that library is loaded first |
| `%zu` | `src/lv2_printf.c`, linked with `--wrap` |

The toolchain's newlib also hides `strtoll`, `strtoull` and `nanosleep`
from strict ISO C, so the PS3 build is `-std=gnu11`. The main thread gets
a 1 MB stack (`SYS_PROCESS_PARAM`); PSL1GHT's default is 64 KB.

A file write waits behind a build's large writes to the same drive, for
seconds at a time, so the screen's thread writes no files: the log goes
through a thread of its own, the fonts are read whole at start, and the
images' boot files in `PS2ISO` are read on a thread of their own.

## The SPUs

The PPU's two hardware threads are slow at the core's heavy work, so the
app links the core with `--wrap` for two of its functions and does that
work on the SPUs, with the core's own C built for them (`spu-gcc`):

- **A level's compression** (`wad_compress`, `src/wad_spu.c`,
  `spu/wad_job.c`): wad.c already cuts a level into chunks of 16 KB, each
  with at most 48 KB of history before it, so a chunk fits an SPU's local
  store. The chunks are shared among 6 SPU threads, and wad_layout lays
  them out on the PPU as before.
- **SHA-256** (`sha256_add`, `src/sha_spu.c`, `spu/sha_job.c`): a large
  addition's whole blocks run on one SPU (a hash is a chain, block after
  block), which keeps up with the drive.

When the SPUs cannot be had, the core's own functions run on the PPU. An
SPU thread ends with `spu_thread_exit`; returning from `main` runs
newlib's exit, which stops the SPU with a code lv2 does not take for an
end. The bytes are the same either way.

## The log

What the app prints also goes to `USRDIR/log.txt`, each line after the
milliseconds since the start: the commands and the core's events, and
every 5 seconds how long frames take. It can be read over FTP, even after
a hang.

## The update check

Once the payload has said its version, `src/update.c` asks GitHub's API
for the latest rac-editions release, on a thread of its own, and compares
the two tags. A newer release with an `EditionsBuilder.pkg` shows as
"Get v0.3.0" on the first screen; Triangle downloads the package to
`/dev_hdd0/packages/EditionsBuilder.pkg` (through a `.part` file, its size
checked against the release's), and the screen says how to install it. A
running app cannot replace itself, so Package Manager installs it.

A build between releases (its version a commit) does not ask. A check that
fails, with no network or GitHub out of reach, says nothing on screen,
only in the log: the builder works offline as before. A failed download
says why and can be tried again.

HTTPS is the toolchain's libcurl (7.64) over mbedTLS (2.28), which has no
certificate store of its own: `data/cacert.pem` is copied beside the app
and given to libcurl. It is curl's extract of Mozilla's roots
(<https://curl.se/docs/caextract.html>); its date is in its first lines,
and a newer one is dropped in as it is. In RPCS3 the app needs the network
on ("Internet enabled: Connected" in its configuration), and RPCS3 refuses
to make `/dev_hdd0/packages`, so that folder is made by hand there; on a
console it is the folder Package Manager reads.

## Drawing

The screens are laid out on a 1280x720 screen and drawn by the graphics
chip at the TV's own mode (`draw.h`): on the PS3 the RSX through tiny3d
(`draw_rsx.c`), in the preview SDL's renderer (`draw_sdl.c`, its window
size set by `RAC_PS3_DISPLAY`, e.g. `1920x1080`). Everything is blended
triangles in display pixels: panels are polygons, circles fans, the warp's
streaks fade along their length. Text comes from one atlas of glyphs,
rendered by SDL_ttf at the size they are seen (the printable ASCII at
start, others when first drawn) and tinted by each vertex's colour. A
frame takes about 17 ms in RPCS3 at 1080p.

PSL1GHT's SDL renderer is not used: it copies textures with the RSX's
blitter, without alpha blending or colour modulation, and its blended
shapes crash. `tiny3d_Flip` starts its vertex buffer over as soon as it
sends the flip, while the RSX may still be drawing from it: on a console
that showed as flashes of garbage across the screen. So `draw_end` waits
for the flip (`gcmGetFlipStatus`) before the next frame; tiny3d's own
`waitFlip` would reset the status its next flip waits on, and the
installed tiny3d has no `tiny3d_WaitRSX`. In RPCS3, a test
script's screenshots need "Write Color Buffers" on for the app
(`config/custom_configs/config_RACE00001.yml`), or they come out black.

## Plan

1. **Big-endian proof (done).** The core cross-built for big-endian
   64-bit PowerPC Linux (Debian's `powerpc64-linux-gnu-gcc`, static) and
   run under qemu-user passes its tests and builds the edition from the
   v0.1.0 payload and the real discs into the same image as on Windows
   (SHA-256 `b932f7d2…`).
2. **Toolchain (done).** `Containerfile`.
3. **The core on lv2 (done).** Threads and files as above; `tests/smoke.c`
   in RPCS3 identifies both discs, plans, and builds the image with
   SHA-256 `b932f7d2…`, the same as on Windows.
4. **The app (done).** The screens of the desktop builder's wizard, drawn
   in its look (the dark theme's colours, Chakra Petch and Barlow):
   Edition, Discs, Review and build (with the edition settings), the
   build, and Done with how to start the new image. In RPCS3, driven by a test script,
   it finds both discs in `PS2ISO`, builds the image (`b932f7d2…`, checked
   again from Windows), and cancels a build, deleting the unfinished image.
5. **Packaging (done).** `make pkg` makes `build/EditionsBuilder.pkg`
   (NPDRM EBOOT, `RACE00001`), which Package Manager installs on HEN and
   CFW consoles.
6. **On a console (done).** On a CECHA with webMAN MOD, the app finds the
   discs among a library of 40 images in `PS2ISO`, checks a disc it has
   not seen before at the drive's speed (about 50 MB/s, 80 s for the PAL
   disc), and builds the image (`b932f7d2…`) in about 7 minutes, the time
   it takes the drive to write 4.2 GB while reading the disc. The screen
   stays at 60 frames a second throughout.
7. **Version and updates.** The release's tag on screen and in the XMB, and
   the update check above.
