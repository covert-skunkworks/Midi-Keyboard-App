# PC MIDI Keyboard -- on-screen piano keyboard (25..128 keys) that sends MIDI.
#
#   Windows (MSYS2 MinGW-w64):  mingw32-make  -> build/pc_midi_keyboard.exe
#   Linux:   sudo apt install build-essential pkg-config libsdl2-dev libasound2-dev
#            make                             -> build/pc_midi_keyboard
#   macOS:   xcode-select --install; brew install sdl2 pkg-config
#            make                             -> build/pc_midi_keyboard
#
# Only dependency is SDL2; the MIDI output uses the OS API on each platform.

SRC     := src/main.c src/piano.c src/font.c src/midi_out.c
BUILD   := build
CFLAGS  += -std=c99 -O2 -Wall -Wextra -pedantic

# An MSYS2 login shell (as on GitHub's Windows runners) drops OS=Windows_NT,
# so also recognise Windows from uname.
ifeq ($(OS),Windows_NT)
  WINDOWS := 1
else
  UNAME := $(shell uname -s)
  ifneq ($(filter MINGW% MSYS%,$(UNAME)),)
    WINDOWS := 1
  endif
endif

ifdef WINDOWS
  # make's built-in default "cc" is not what MSYS2 users mean.
  ifeq ($(origin CC),default)
    CC := gcc
  endif

  # mingw32-make claims SHELL=sh.exe but silently uses cmd.exe when no sh is
  # on PATH, so ask the shell itself: only cmd expands %OS%.
  CMD_SHELL := $(filter Windows_NT,$(shell echo %OS%))
  ifeq ($(CMD_SHELL),)
    CC_BIN := $(firstword $(shell command -v $(CC) 2>/dev/null))
    # Native mingw32-make under Git Bash / MSYS sees a Windows-style PATH,
    # where a /c/... entry is meaningless and the fix would silently fail.
    ifneq ($(findstring ;,$(PATH)),)
      CC_BIN := $(firstword $(shell cygpath -m '$(CC_BIN)' 2>/dev/null) $(CC_BIN))
    endif
  else
    CC_BIN := $(subst \,/,$(firstword $(shell where $(CC) 2>nul)))
  endif
  ifneq ($(CC_BIN),)
    CC_DIR := $(patsubst %/,%,$(dir $(CC_BIN)))
    ifneq ($(findstring ;,$(PATH)),)
      export PATH := $(CC_DIR);$(PATH)
    else
      export PATH := $(CC_DIR):$(PATH)
    endif
  endif

  TARGET  := $(BUILD)/pc_midi_keyboard.exe
  # The .exe icon (Explorer, taskbar) is a resource; windres comes with gcc.
  WINDRES ?= windres
  RES     := $(BUILD)/icon_res.o
  SDL_DIR ?= $(patsubst %/bin,%,$(CC_DIR))
  CFLAGS  += -I$(SDL_DIR)/include/SDL2 -Dmain=SDL_main
  # Fully static, so the .exe runs on any Windows PC without SDL2.dll or
  # the MinGW runtime DLLs next to it. -mwindows: GUI app, no console.
  LDLIBS  += -static -L$(SDL_DIR)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows \
             -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 \
             -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 \
             -ldinput8
  ifeq ($(CMD_SHELL),)
    MKDIR := mkdir -p $(BUILD)
    RM    := rm -rf $(BUILD)
  else
    MKDIR := if not exist $(BUILD) mkdir $(BUILD)
    RM    := if exist $(BUILD) rmdir /s /q $(BUILD)
  endif
else
  TARGET := $(BUILD)/pc_midi_keyboard
  RES    :=
  # pkg-config first; sdl2-config as the fallback for older setups.
  SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags)
  SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs)
  CFLAGS += $(SDL_CFLAGS)
  LDLIBS += $(SDL_LIBS) -lm
  ifeq ($(UNAME),Darwin)
    # CoreMIDI marks the MIDIPacketList calls deprecated in favour of
    # MIDIEventList (macOS 11+); they still work and keep 10.x support.
    CFLAGS += -Wno-deprecated-declarations
    LDLIBS += -framework CoreMIDI -framework CoreFoundation
  else
    # Strict -std=c99 hides alloca() and struct timespec, which the ALSA
    # headers rely on; _DEFAULT_SOURCE brings them back on glibc.
    CFLAGS += -D_DEFAULT_SOURCE
    ASOUND_LIBS := $(shell pkg-config --libs alsa 2>/dev/null || echo -lasound)
    LDLIBS += $(ASOUND_LIBS)
  endif
  MKDIR := mkdir -p $(BUILD)
  RM    := rm -rf $(BUILD)
endif

all: $(TARGET)

$(TARGET): $(SRC) src/*.h $(RES)
	$(MKDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(RES) $(LDLIBS)

$(BUILD)/icon_res.o: pc_midi_keyboard.rc icon.ico
	$(MKDIR)
	$(WINDRES) pc_midi_keyboard.rc -O coff -o $@

clean:
	$(RM)

.PHONY: all clean
