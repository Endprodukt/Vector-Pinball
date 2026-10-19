# Vector Pinball fuer Vectrex + UVMC2
#
#   make host      Windows-Simulator (SDL2, gcc aus MSYS2 MINGW64)      -> build/pinball_host.exe
#   make uvm2      .um2 fuer die Konsole (Arm-Toolchain, Rust, CMake)    -> build_uvm2/pinball.um2
#   make data      Tischdaten neu erzeugen (aus ../app/src/main/assets/tables)
#   make sounds    Originalaufnahmen -> sdcard/pinball/snd/*.wav (Buchse des UVMC2, Simulator)
#   make test      Dauerlauf des Spielkerns ohne Grafik (alle Tische)
#
# Vorher einmal:  source tools/env.sh   (Pfade der Werkzeuge)
# Das Starter Kit wird in uvmc2-starter-kit/ erwartet; liegt es dort nicht, wird das des
# Dracula-Ports benutzt (siehe docs/SETUP.md).

UVMC2_KIT   ?= $(firstword $(wildcard uvmc2-starter-kit) $(wildcard F:/DraculaVectrexPort/uvmc2-starter-kit))
UVMC2_KIT   := $(abspath $(UVMC2_KIT))
UVM2_SDK    ?= $(UVMC2_KIT)/sdk/uvm2-sdk
VPY_C_SDK   ?= $(UVMC2_KIT)/sdk/vpy-c
PITREX_INC  ?= $(UVMC2_KIT)/sdk/pitrex-sim/include

CORE_SRCS   = core/vp_field.c core/vp_phys.c core/vp_table1.c core/vp_table5.c core/vp_table7.c core/vp_table9.c core/vp_table10.c \
              core/gen/table_data.c core/gen/star_data.c core/gen/lid_data.c
GAME_SRCS   = game/main.c game/gfx.c game/font.c game/render.c game/settings.c game/hiscore.c game/sound.c game/aysynth.c game/pcm.c game/mp3.c game/intro.c
INCS        = -Icore -Igame -Iplatform -Ithird_party/minimp3 -I$(VPY_C_SDK)/include -I$(PITREX_INC)

# ---- Host ---------------------------------------------------------------
HOST_CC    ?= gcc
SDL_INC    ?= -IC:/msys64/mingw64/include/SDL2 -IC:/msys64/mingw64/include
WARN        = -Wall -Wextra -Wno-unused-parameter -Wno-comment
host: build/pinball_host.exe
build/pinball_host.exe: $(GAME_SRCS) $(CORE_SRCS) platform/host_sdl.c platform/plat.h game/*.h core/*.h
	@mkdir -p build
	$(HOST_CC) -O2 -g $(WARN) -std=gnu11 $(INCS) $(SDL_INC) \
	    $(GAME_SRCS) $(CORE_SRCS) platform/host_sdl.c $(VPY_C_SDK)/vpy.c -lm -lSDL2 -o $@
	@cp -n C:/msys64/mingw64/bin/SDL2.dll build/ 2>/dev/null || true

# ---- Daten, Tests -------------------------------------------------------
sounds:
	python tools/audio2pcm.py

data:
	python tools/tables2c.py

# Deckel der Tische neu bestimmen (braucht numpy und opencv-python); Vorschaubilder nach shots/
lids:
	python tools/lids.py --preview shots

test: tools/soak.exe
	./tools/soak.exe 300
tools/soak.exe: tools/soak.c $(CORE_SRCS) core/*.h
	$(HOST_CC) -O2 $(WARN) -std=gnu11 -Icore tools/soak.c $(CORE_SRCS) -lm -o $@
tools/savertest.exe: tools/savertest.c $(CORE_SRCS) core/*.h
	$(HOST_CC) -O2 $(WARN) -std=gnu11 -Icore tools/savertest.c $(CORE_SRCS) -lm -o $@
tools/trace.exe: tools/trace.c $(CORE_SRCS) core/*.h
	$(HOST_CC) -O2 $(WARN) -std=gnu11 -Icore tools/trace.c $(CORE_SRCS) -lm -o $@
# Sind alle Schuesse zu schaffen? Dauerspiel mit zielendem Automaten und Schusskarte (SHOT_MAP=1); tools/shotsweep.py
# stellt mehrere Abstimmungen nebeneinander.  tools/shottest.exe <tisch> [flipper %] [neigung %] [stunden] [real 0|1]
tools/shottest.exe: tools/shottest.c $(CORE_SRCS) core/*.h
	$(HOST_CC) -O2 $(WARN) -std=gnu11 -Icore tools/shottest.c $(CORE_SRCS) -lm -o $@

# Flipper vermessen: Zielen (Rueckhand?), Halten, Post Pass.  tools/fliptest.exe [real 0|1]
tools/fliptest.exe: tools/fliptest.c $(CORE_SRCS) core/*.h
	$(HOST_CC) -O2 $(WARN) -std=gnu11 -Icore tools/fliptest.c $(CORE_SRCS) -lm -o $@

# ---- UVMC2 (.um2) -------------------------------------------------------
UVM2_NAME    = pinball
UVM2_SRCS    = $(GAME_SRCS) $(CORE_SRCS) platform/uvmc2.c $(VPY_C_SDK)/vpy.c
UVM2_CC      = arm-none-eabi-gcc
UVM2_CXX     = arm-none-eabi-gcc
UVM2_OBJCOPY = arm-none-eabi-objcopy
UVM2_CFLAGS  = -mthumb -mcpu=cortex-m33 -ffreestanding -O2 -std=gnu11 \
               -ffunction-sections -fdata-sections -DVPY_RP2350 $(INCS) -I$(UVM2_SDK) -I$(UVMC2_KIT)/sdk/third_party/fatfs \n               -DUVM2_ROMZIP_MAX=1024 -DVPY_SB_MAX=600 -DUVM2_REPLAY_LAST
# REPLAY_LAST (Kit ab 2026-10-02): kommt kein neues Bild rechtzeitig, zeichnet Kern 1 das letzte noch einmal, statt den
# Strahl auszulassen - kurze Haenger von Kern 0 (Kartenzugriff, Tisch laden) sind dann kein dunkles Bild mehr.
# Nur im Vorspann eingeschaltet (plat_redraw_last): im Spiel kostet jedes Wiederholbild ein ganzes Bild Wartezeit.
# ROMZIP_MAX: das Spiel hat kein ROM-Archiv, der Puffer des Kits dafuer (sonst 24 KB RAM) darf winzig sein.
# VPY_SB_MAX: Strichpuffer von libvpy; mehr als ~500 Striche zeichnet das Spiel nie.
# CMD_CAPACITY: Befehlsliste des Kits (zwei Puffer zu 3 Byte je Befehl, ~12 Befehle je Strich): 6656 reichen fuer ~560
# Striche und geben 9 KB RAM fuer den MP3-Dekoder frei. Kontrolle: INFO ON, X muss 0 bleiben.
UVM2_CMD_CAPACITY = 6656
UVM2_LDLIBS  = -lm
UVM2_HZ     ?= 0
# Das PSRAM hochfahren: dort liegen die Tonaufnahmen fuer die Buchse (ohne das bleibt die Buchse bei Chip-Effekten)
UVM2_PSRAM_START = 1
-include $(UVM2_SDK)/uvm2.mk

clean:
	rm -rf build build_uvm2 tools/*.exe
.PHONY: host data sounds test clean
