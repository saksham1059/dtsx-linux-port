CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wshadow -fno-fast-math -fPIC -MMD -MP
CPPFLAGS = -Iinclude -Isrc
LDLIBS  ?= -lm

BUILD   := build
OBJDIR  := $(BUILD)/obj
SRC_CORE := src/core/biquad.c src/core/config.c src/core/chain.c \
            src/filters/crossover.c src/bass/truhdx.c \
            src/dynamics/mbhl.c src/dynamics/limiter.c \
            src/loudness/loudness.c src/spatial/spatial.c src/io/wav.c \
            src/live/live.c
OBJ     := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC_CORE))
BIN     := $(BUILD)/dtsdsp
PLUGIN  := $(BUILD)/dtsx_ladspa.so
TESTBIN := $(BUILD)/test_dsp

.PHONY: all clean test plugin install-check

all: $(BIN) $(PLUGIN)

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

-include $(OBJDIR)/core/*.d $(OBJDIR)/filters/*.d $(OBJDIR)/bass/*.d \
         $(OBJDIR)/dynamics/*.d $(OBJDIR)/loudness/*.d $(OBJDIR)/spatial/*.d \
         $(OBJDIR)/io/*.d $(OBJDIR)/live/*.d $(OBJDIR)/cli/*.d $(OBJDIR)/ladspa/*.d

$(BIN): $(OBJ) $(OBJDIR)/cli/dtsdsp.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(PLUGIN): $(OBJ) $(OBJDIR)/ladspa/dtsx_ladspa.o
	$(CC) $(CFLAGS) -shared -fPIC $^ -o $@ $(LDLIBS)

$(OBJDIR)/ladspa/dtsx_ladspa.o: src/ladspa/dtsx_ladspa.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c $< -o $@

$(TESTBIN): $(OBJ) tests/test_dsp.c
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_dsp.c $(OBJ) -o $@ $(LDLIBS)

$(BUILD)/test_ladspa: tests/test_ladspa.c $(PLUGIN)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_ladspa.c -o $@ -ldl -lm

test: $(BIN) $(TESTBIN) $(BUILD)/test_ladspa
	./$(TESTBIN)
	./$(BIN) selftest
	DTSX_LIVE_NAMESPACE=make-test DTSX_CONFIG=configs/speakers-music.json ./$(BUILD)/test_ladspa $(PLUGIN)
	DTSX_LIVE_NAMESPACE=make-test DTSX_CONFIG=configs/headphones-music.json ./$(BUILD)/test_ladspa $(PLUGIN)

clean:
	rm -rf $(BUILD)

# Non-destructive installation preview: prints the exact files and commands.
install-check: all
	@echo "binary : $(BIN)"
	@echo "plugin : $(PLUGIN)"
	@echo "Installs to ~/.local/bin/dtsdsp and ~/.local/lib/ladspa/dtsx_ladspa.so"
	@echo "Use scripts/install.sh --enable to activate the PipeWire sink."
