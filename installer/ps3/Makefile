# The PS3 builder and its tests, built in the toolchain container (Containerfile) from installer/ps3:
#
#   make smoke PAYLOAD=<payload folder>     build/smoke/: a game folder that builds the edition headless
#
# A game folder holds PARAM.SFO and USRDIR/EBOOT.BIN, with the edition's payload in USRDIR/payload; copied to
# dev_hdd0/game/<APPID>, the PS3 (or RPCS3) lists and runs it.

CORE := ../core
BUILD := build

CC := ppu-gcc
STRIP := ppu-strip
# gnu11: the toolchain's newlib declares strtoll, strtoull and nanosleep only outside strict ISO C. GCC 7 also
# warns of a variable named main (moby.c), which is not the function.
# -MMD -MP: each object's headers in a .d file beside it (included below), so a changed header (a struct's layout)
# rebuilds every object that uses it.
CFLAGS := -std=gnu11 -O2 -mcpu=cell -mhard-float -ffunction-sections -fdata-sections \
	-Wall -Wextra -Wpedantic -Wno-main -Werror -ffp-contract=off -MMD -MP \
	-I$(PSL1GHT)/ppu/include -I$(CORE)/src -I$(CORE)
# The printf family wrapped by src/lv2_printf.c, for C99's %zu, which the toolchain's newlib lacks, and for the log;
# puts too, which GCC makes of a printf of a plain line.
WRAP_PRINTF := -Wl,--wrap=snprintf,--wrap=vsnprintf,--wrap=printf,--wrap=fprintf,--wrap=vfprintf,--wrap=puts
LDFLAGS := -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib -Wl,--gc-sections $(WRAP_PRINTF)
LIBS := -lsysfs -lsysutil -lsysmodule -lrt -llv2 -lm

CORE_SOURCES := $(filter-out $(CORE)/src/main.c,$(wildcard $(CORE)/src/*.c)) \
	$(CORE)/wad.c $(CORE)/vag.c $(CORE)/helpdesk.c $(CORE)/levels.c $(CORE)/sign.c $(CORE)/moby.c \
	$(CORE)/moby_mesh.c
CORE_OBJECTS := $(patsubst $(CORE)/%.c,$(BUILD)/core/%.o,$(CORE_SOURCES))

$(BUILD)/core/%.o: $(CORE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libcore.a: $(CORE_OBJECTS)
	rm -f $@
	ppu-ar rcs $@ $^

# The app's version in PARAM.SFO (APP_VER, which the XMB shows): the payload's, when it is a release's tag, as its
# major and minor numbers, two digits each (v1.2.3 is 01.02; the patch number is left out); 00.00 (the template's)
# between releases.
PAYLOAD_VERSION := $(if $(PAYLOAD),$(shell sed -n 's/^ *"version": "\(.*\)",*$$/\1/p' $(PAYLOAD)/edition.json))
APP_VER := $(shell echo '$(PAYLOAD_VERSION)' | sed -n 's/^v\([0-9]\{1,2\}\)\.\([0-9]\{1,2\}\)\.[0-9]\{1,\}$$/\1 \2/p' \
	| xargs -r printf '%02d.%02d')
ifneq ($(filter v%,$(PAYLOAD_VERSION)),)
ifeq ($(APP_VER),)
$(error $(PAYLOAD_VERSION) does not fit APP_VER's two-digit major and minor numbers)
endif
endif

# An ELF made into a game folder: APPID, TITLE, ICON0.PNG, the PARAM.SFO template, the EBOOT (a fake-signed SELF, which RPCS3 and HEN
# consoles run) and the payload.
define game_folder
	rm -rf $(1)
	mkdir -p $(1)/USRDIR
	$(STRIP) $(2) -o $(BUILD)/stripped.elf
	sprxlinker $(BUILD)/stripped.elf
	fself $(BUILD)/stripped.elf $(1)/USRDIR/EBOOT.BIN
	sed 's/^\(\s*\)00\.00$$/\1$(or $(APP_VER),00.00)/' $(6) > $(BUILD)/sfo.xml
	sfo --title "$(4)" --appid "$(3)" -f $(BUILD)/sfo.xml $(1)/PARAM.SFO
	cp $(5) $(1)/ICON0.PNG
	$(if $(PAYLOAD),cp -r $(PAYLOAD) $(1)/USRDIR/payload)
endef

# The core's heavy work on the SPUs (src/spu.c): a level's compression (src/wad_spu.c; spu/wad_job.c with the
# core's wad.c) and SHA-256 (src/sha_spu.c; spu/sha_job.c with the core's sha256.c). Each SPU program is embedded in
# the PPU's ELF (tools/embed.py), and the core's wad_compress and sha256_add are wrapped to use them.
SPU_CC := spu-gcc
SPU_CFLAGS := -std=gnu11 -O3 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -I$(PSL1GHT)/spu/include -I$(CORE) -Ispu
WRAP_SPU := -Wl,--wrap=wad_compress,--wrap=sha256_add
SPU_ELVES := $(BUILD)/spu/wad_job_elf.o $(BUILD)/spu/sha_job_elf.o
SPU_OBJECTS := $(BUILD)/src/spu.o $(BUILD)/src/wad_spu.o $(BUILD)/src/sha_spu.o $(SPU_ELVES)

$(BUILD)/spu/wad_job.elf: spu/wad_job.c spu/wad_spu_job.h $(CORE)/wad.c $(CORE)/wad.h
	@mkdir -p $(dir $@)
	$(SPU_CC) $(SPU_CFLAGS) spu/wad_job.c $(CORE)/wad.c -Wl,--gc-sections -L$(PSL1GHT)/spu/lib -lsputhread -o $@

$(BUILD)/spu/sha_job.elf: spu/sha_job.c spu/sha_spu_job.h $(CORE)/src/sha256.c $(CORE)/src/sha256.h
	@mkdir -p $(dir $@)
	$(SPU_CC) $(SPU_CFLAGS) -I$(CORE)/src spu/sha_job.c $(CORE)/src/sha256.c -Wl,--gc-sections -L$(PSL1GHT)/spu/lib -lsputhread -o $@

$(BUILD)/spu/%_elf.c: $(BUILD)/spu/%.elf tools/embed.py
	python3 tools/embed.py $< $*_elf $@

$(BUILD)/spu/%_elf.o: $(BUILD)/spu/%_elf.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src/wad_spu.o: spu/wad_spu_job.h
$(BUILD)/src/sha_spu.o: spu/sha_spu_job.h

$(BUILD)/smoke.elf: $(BUILD)/tests/smoke.o $(BUILD)/src/lv2_printf.o $(SPU_OBJECTS) $(BUILD)/libcore.a
	$(CC) $(LDFLAGS) $(WRAP_SPU) $^ $(LIBS) -o $@

# The builder: src/ but the preview's platform, on PSL1GHT's SDL2 and SDL2_ttf, and libcurl over mbedTLS for the
# update check.
APP_SOURCES := $(filter-out src/platform_pc.c src/draw_sdl.c src/net_pc.c,$(wildcard src/*.c))
APP_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(APP_SOURCES))
$(APP_OBJECTS): CFLAGS += -isystem $(PS3DEV)/portlibs/ppu/include -isystem $(PS3DEV)/portlibs/ppu/include/SDL2
APP_LIBS := -lSDL2_ttf -lfreetype -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lSDL2 -ltiny3d -lgcm_sys -lrsx -lio \
	-laudio -lnet $(LIBS)

$(BUILD)/app.elf: $(APP_OBJECTS) $(SPU_ELVES) $(BUILD)/libcore.a
	$(CC) $(LDFLAGS) $(WRAP_SPU) $^ $(APP_LIBS) -o $@

.PHONY: app pkg smoke clean
# build/app/: the builder's game folder, with its fonts and root certificates; APPID is the PS3's id for it
APPID ?= RACE00001
app: $(BUILD)/app.elf
	$(call game_folder,$(BUILD)/app,$<,$(APPID),Editions Builder,data/ICON0.PNG,data/sfo.xml)
	mkdir -p $(BUILD)/app/USRDIR/fonts
	cp data/fonts/*.ttf data/fonts/OFL-*.txt $(BUILD)/app/USRDIR/fonts/
	cp data/cacert.pem $(BUILD)/app/USRDIR/

# build/EditionsBuilder.pkg: the game folder as a package for the PS3's Package Manager, its EBOOT signed as NPDRM
# (HEN and CFW consoles install it), as PSL1GHT's ppu_rules make one
CONTENTID := UP0001-$(APPID)_00-0000000000000000
pkg: app
	rm -rf $(BUILD)/pkg
	cp -r $(BUILD)/app $(BUILD)/pkg
	$(STRIP) $(BUILD)/app.elf -o $(BUILD)/pkg-stripped.elf
	sprxlinker $(BUILD)/pkg-stripped.elf
	make_self_npdrm $(BUILD)/pkg-stripped.elf $(BUILD)/pkg/USRDIR/EBOOT.BIN $(CONTENTID) > /dev/null
	pkg --contentid $(CONTENTID) $(BUILD)/pkg/ $(BUILD)/EditionsBuilder.pkg > /dev/null
	cp $(BUILD)/EditionsBuilder.pkg $(BUILD)/EditionsBuilder.gnpdrm.pkg
	package_finalize $(BUILD)/EditionsBuilder.gnpdrm.pkg

smoke: $(BUILD)/smoke.elf
	$(call game_folder,$(BUILD)/smoke,$<,RACT00001,Executive Edition smoke test,$(PS3DEV)/bin/ICON0.PNG,$(PS3DEV)/bin/sfo.xml)

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
