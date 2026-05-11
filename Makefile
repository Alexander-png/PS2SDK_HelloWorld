# ── Environment checkup ─────────────────────────────────────────
ifeq ($(strip $(PS2SDK)),)
  $(error "PS2SDK is not exported. Execute: export PS2SDK=/path/to/ps2sdk")
endif
ifeq ($(strip $(PS2DEV)),)
  $(error "PS2DEV is not exported. Execute: export PS2DEV=/path/to/ps2dev")
endif

# ---- Build configuration -------------------------------------------------
# Select with: make BUILD=debug  |  make BUILD=release
BUILD ?= debug

ifeq ($(BUILD),debug)
  EE_OPTFLAGS   = -O0
  BUILD_CFLAGS  = -g -DDEBUG -DLOG_SCREEN
  EE_BIN        = app_debug.elf
else ifeq ($(BUILD),release)
  EE_OPTFLAGS   = -O2
  BUILD_CFLAGS  = -DNDEBUG -DLOG_DISABLE
  EE_BIN        = app_release.elf
else
  $(error Unknown BUILD='$(BUILD)'. Use 'debug' or 'release')
endif

# ---- Paths ---------------------------------------------------------------
SRC_DIR    = src
OBJ_DIR    = obj/$(BUILD)
AUDSRV_IRX   = $(PS2SDK)/iop/irx/audsrv.irx
AUDSRV_IRX_C = $(SRC_DIR)/audsrv_irx.c
BIN2C        = $(PS2SDK)/bin/bin2c

# ---- Sources -------------------------------------------------------------
EE_STATIC_SRCS = $(SRC_DIR)/main.c \
                 $(SRC_DIR)/boot.c \
                 $(SRC_DIR)/game_app.c \
                 $(SRC_DIR)/engine/platform/platform_ps2.c \
                 $(SRC_DIR)/engine/logging/log.c \
                 $(SRC_DIR)/engine/audio/audio_driver_ps2.c \
                 $(SRC_DIR)/engine/audio/audio.c \
                 $(SRC_DIR)/engine/audio/audio_mixer_stream.c \
                 $(SRC_DIR)/game/states/test/audio_test_state.c


EE_GEN_SRCS    = $(AUDSRV_IRX_C)             

EE_SRCS        = $(EE_STATIC_SRCS) $(EE_GEN_SRCS)

EE_OBJS    = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(EE_SRCS)) \
             $(AUDSRV_IRX_OBJ)

EE_DEPS    = $(EE_OBJS:.o=.d)

# Sanity-check that every listed source actually exists.
$(foreach src,$(filter-out $(AUDSRV_IRX_C),$(EE_SRCS)),$(if $(wildcard $(src)),,$(error Source not found: $(src))))

# ---- Compile flags -------------------------------------------------------
EE_INCS   += -I$(SRC_DIR) \
             -I$(SRC_DIR)/engine/platform \
             -I$(SRC_DIR)/engine/logging \
             -I$(SRC_DIR)/engine/audio \
             -I$(SRC_DIR)/game/audio

# -MMD -MP: generate .d dependency files alongside each .o
EE_CFLAGS += $(BUILD_CFLAGS) -MMD -MP

EE_LIBS    = -lmpeg -ldraw -lgraph -lpacket -ldma -laudsrv -lpatches -lc -ldebug -lkernel

# ---- Targets -------------------------------------------------------------
all: $(EE_BIN)

# Convenience targets
debug:
	$(MAKE) BUILD=debug

release:
	$(MAKE) BUILD=release

clean:
	rm -rf obj app_debug.elf app_release.elf $(AUDSRV_IRX_C)

distclean: clean

.PHONY: all debug release clean distclean

# # Launch on real PS2 via ps2client (DECI2 through network)
# PS2_IP = 192.168.1.10
# run: all
# 	ps2client -h $(PS2_IP) execee host:$(EE_BIN)

# # Launch in PCSX2
# sim: all
# 	PCSX2 -elf $(CURDIR)/$(EE_BIN) -nogui

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

# Generate embedded IRX source
$(AUDSRV_IRX_C): $(AUDSRV_IRX)
	$(BIN2C) $< $@ audsrv_irx

# IMPORTANT: define our pattern rule AFTER the includes so it overrides
# ps2sdk's default `%.o: %.c` rule. Last definition wins in make.
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

# $(OBJ_DIR)/audsrv_irx.o: $(AUDSRV_IRX)
# 	@mkdir -p $(dir $@)
# 	$(EE_OBJCOPY) -I binary -O elf32-littlemips -B mips $< $@  

# Pull in dependency files. The leading dash silences errors on first
# build (when no .d files exist yet) — make ignores missing -include files.
-include $(EE_DEPS)