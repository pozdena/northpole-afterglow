# North Pole: an After Dark module for Afterglow.
#
#   make            build "build/North Pole" (68k code resource + resources, with its
#                   resource fork) and "build/North Pole.hqx" (BinHex, for sharing), and
#                   the same pair for "North Pole Preview", a variant for testing
#                   and "North Pole Moon", for trying the moon's phases (local only)
#   make preview    render build/preview.mp4 and build/preview.gif on the desktop
#   make sheet      contact sheet of every sprite: build/sheet.png
#   make check      the preview's frame-by-frame diff check, a few scenes
#
# Needs: brew install m68k-elf-gcc m68k-elf-binutils ffmpeg; Xcode command line tools (Rez).

CC68    = m68k-elf-gcc
OBJCOPY = m68k-elf-objcopy
CFLAGS68 = -m68000 -Os -mpcrel -ffixed-a5 -ffreestanding -fno-builtin -nostdlib \
           -fno-common -fvisibility=hidden -fomit-frame-pointer -fno-asynchronous-unwind-tables \
           -Wall -Wextra -Wno-unused-function
MODULE  = build/North Pole
PREVIEW_MODULE = build/North Pole Preview

SRC = src/module.c src/scene.c src/scene.h src/present.c src/mac.h src/sprites_gen.h

all: build/North\ Pole.hqx build/North\ Pole\ Preview.hqx build/North\ Pole\ Moon

src/sprites_gen.h: art/sprites.txt tools/gen_sprites.py
	python3 tools/gen_sprites.py art/sprites.txt $@

build/module.o: $(SRC) | build
	$(CC68) $(CFLAGS68) -c src/module.c -o $@

build/pv/module.o: $(SRC) | build
	mkdir -p build/pv
	$(CC68) $(CFLAGS68) -DNP_PREVIEW -c src/module.c -o $@

build/mn/module.o: $(SRC) | build
	mkdir -p build/mn
	$(CC68) $(CFLAGS68) -DNP_MOON -c src/module.c -o $@

build/entry.o: src/entry.S | build
	$(CC68) -m68000 -c src/entry.S -o $@

build/lib68.o: src/lib68.S | build
	$(CC68) -m68000 -c src/lib68.S -o $@

# Link twice at different bases: a position-independent blob comes out identical.
# $(call link,<dir>): <dir>/module.o -> <dir>/code.bin
define link
	$(CC68) -m68000 -nostdlib -Wl,--defsym=BASE=0 -Wl,-T,tools/code.ld \
	    build/entry.o $(1)/module.o build/lib68.o -o $(1)/code.elf
	$(CC68) -m68000 -nostdlib -Wl,--defsym=BASE=0x10000 -Wl,-T,tools/code.ld \
	    build/entry.o $(1)/module.o build/lib68.o -o $(1)/code_hi.elf
	$(OBJCOPY) -O binary -j .text $(1)/code.elf $(1)/code.bin
	$(OBJCOPY) -O binary -j .text $(1)/code_hi.elf $(1)/code_hi.bin
	@cmp -s $(1)/code.bin $(1)/code_hi.bin || { echo "code is NOT position independent"; rm -f $(1)/code.bin; exit 1; }
	@python3 tools/check_sections.py $(1)/code.elf
	@echo "code resource: $$(wc -c < $(1)/code.bin) bytes"
endef

build/code.bin: build/entry.o build/module.o build/lib68.o tools/code.ld
	$(call link,build)

build/pv/code.bin: build/entry.o build/pv/module.o build/lib68.o tools/code.ld
	$(call link,build/pv)

build/mn/code.bin: build/entry.o build/mn/module.o build/lib68.o tools/code.ld
	$(call link,build/mn)

# The module files have spaces in their names: as make targets the spaces are escaped,
# and in the recipes $@ is quoted.
build/North\ Pole: build/code.bin src/module.r
	rm -f "$@"
	Rez src/module.r -o "$@"
	SetFile -t ADgm -c ADrk "$@"
	@echo "built $@"

build/North\ Pole.hqx: build/North\ Pole tools/binhex.py
	python3 tools/binhex.py "$(MODULE)" "$@"

build/North\ Pole\ Preview: build/pv/code.bin src/module.r
	rm -f "$@"
	Rez -d PREVIEW src/module.r -o "$@"
	SetFile -t ADgm -c ADrk "$@"
	@echo "built $@"

build/North\ Pole\ Preview.hqx: build/North\ Pole\ Preview tools/binhex.py
	python3 tools/binhex.py "$(PREVIEW_MODULE)" "$@"

# the moon test module: any phase of the moon, for trying the moon out (never published)
build/North\ Pole\ Moon: build/mn/code.bin src/module.r
	rm -f "$@"
	Rez -d MOON src/module.r -o "$@"
	SetFile -t ADgm -c ADrk "$@"
	@echo "built $@"

build:
	mkdir -p build

build/preview: tools/preview.c src/scene.c src/scene.h src/present.c src/sprites_gen.h | build
	cc -O2 -Wall -Wextra -Wno-unused-function -o $@ tools/preview.c

sheet: src/sprites_gen.h | build
	python3 tools/gen_sprites.py art/sprites.txt src/sprites_gen.h --sheet build/sheet.png

# The README's animation (docs/preview.gif): seed 1 on 25 November 2026, a full moon and
# no twelve days. From 34 s: clouds roll in over the moon, snow settles on the trees, an
# elf goes into the workshop, Santa and Rudolph walk off and the sleigh flies over.
preview: build/preview
	./build/preview 82 1 4 70 0 60 1 20261125 | ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 \
	    -s 640x480 -r 30 -i - -c:v libx264 -pix_fmt yuv420p build/preview.mp4
	ffmpeg -loglevel error -y -ss 34 -t 46 -i build/preview.mp4 \
	    -vf "fps=15,scale=480:-1:flags=neighbor,split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none" \
	    build/preview.gif

check: build/preview
	for seed in 1 2 3 4 5 6 7 8; do ./build/preview 130 $$seed 1 $$((seed*12)) 0 $$((seed*12)) $$((seed%2)) > /dev/null || exit 1; done

clean:
	rm -rf build

.PHONY: all preview sheet check clean
