#!/bin/sh
# Build the host-side screenshot harness (not part of the .nro).
# Needs: sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
set -e
cd "$(dirname "$0")/../.."
gcc -O2 -o /tmp/hostshot \
    tools/hostshot/hostshot.c source/gfx.c source/ui.c source/settings.c source/cJSON.c \
    -Iinclude $(sdl2-config --cflags) $(sdl2-config --libs) -lSDL2_ttf -lSDL2_image -lm
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software /tmp/hostshot "${1:-docs}"
