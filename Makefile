# Compiler
.DEFAULT_GOAL := all
CC = gcc
PREFIX ?= /usr/local

# Build type (debug or release)
BUILD_TYPE ?= release

# Base flags (using c2x for C23 compatibility on GCC 9+)
BASE_CFLAGS = -std=c2x -Iinclude -Ilib -Iprotocols
BASE_CFLAGS += -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-prototypes
BASE_CFLAGS += -Wmissing-prototypes -Wold-style-definition -Wredundant-decls
BASE_CFLAGS += -Wnested-externs -Wmissing-include-dirs -Wlogical-op
BASE_CFLAGS += -Wjump-misses-init -Wdouble-promotion -Wshadow
BASE_CFLAGS += -fstack-protector-strong
TEXT_CFLAGS := $(shell pkg-config --cflags freetype2 fontconfig)
TEXT_LIBS := $(shell pkg-config --libs freetype2 fontconfig)
BASE_CFLAGS += $(TEXT_CFLAGS)

# Debug flags
DEBUG_CFLAGS = $(BASE_CFLAGS) -g3 -O0 -DDEBUG -fsanitize=address -fsanitize=undefined
DEBUG_LDFLAGS = -fsanitize=address -fsanitize=undefined

# Release flags
RELEASE_CFLAGS = $(BASE_CFLAGS) -O3 -DNDEBUG -flto -fPIE -D_FORTIFY_SOURCE=2

# Set flags based on build type
ifeq ($(BUILD_TYPE),debug)
    CFLAGS = $(DEBUG_CFLAGS)
    LDFLAGS = -lwayland-client -lm -lpthread $(DEBUG_LDFLAGS)
else
    CFLAGS = $(RELEASE_CFLAGS)
    LDFLAGS = -lwayland-client -lm -lpthread -flto -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack
endif

LDFLAGS += $(TEXT_LIBS)

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build
OBJDIR = $(BUILDDIR)/$(BUILD_TYPE)/obj
PROTOCOLDIR = protocols

# Source files (including embedded assets which are now committed)
SOURCES = $(shell find $(SRCDIR) -name "*.c")
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Embedded assets (now committed to git, use embed_assets.sh manually when assets change)
EMBED_SCRIPT = scripts/embed_assets.sh
EMBEDDED_ASSETS_H = $(INCDIR)/graphics/embedded_assets.h
EMBEDDED_ASSETS_C = $(SRCDIR)/graphics/embedded_assets.c

# Protocol files
C_PROTOCOL_SRC = $(PROTOCOLDIR)/zwlr-layer-shell-v1-protocol.c $(PROTOCOLDIR)/xdg-shell-protocol.c $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-protocol.c $(PROTOCOLDIR)/xdg-output-unstable-v1-protocol.c $(PROTOCOLDIR)/fractional-scale-v1-protocol.c $(PROTOCOLDIR)/viewporter-protocol.c $(PROTOCOLDIR)/cursor-shape-v1-protocol.c $(PROTOCOLDIR)/tablet-unstable-v2-protocol.c
H_PROTOCOL_HDR = $(PROTOCOLDIR)/zwlr-layer-shell-v1-client-protocol.h $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-client-protocol.h $(PROTOCOLDIR)/xdg-output-unstable-v1-client-protocol.h $(PROTOCOLDIR)/fractional-scale-v1-client-protocol.h $(PROTOCOLDIR)/viewporter-client-protocol.h $(PROTOCOLDIR)/cursor-shape-v1-client-protocol.h $(PROTOCOLDIR)/tablet-unstable-v2-client-protocol.h
PROTOCOL_OBJECTS = $(C_PROTOCOL_SRC:$(PROTOCOLDIR)/%.c=$(OBJDIR)/%.o)

# Target executable
TARGET = $(BUILDDIR)/herdcat
BUILD_TARGET = $(BUILDDIR)/$(BUILD_TYPE)/herdcat
DEPS = $(OBJECTS:.o=.d) $(PROTOCOL_OBJECTS:.o=.d)
-include $(DEPS)

.PHONY: all clean distclean protocols embed-assets format format-check lint

all: $(TARGET)

# Generate embedded assets (manual target - run when assets change)
embed-assets: 
	./$(EMBED_SCRIPT)

# Create build directories
$(OBJDIR):
	mkdir -p $(OBJDIR)
	mkdir -p $(OBJDIR)/core
	mkdir -p $(OBJDIR)/graphics
	mkdir -p $(OBJDIR)/platform
	mkdir -p $(OBJDIR)/config
	mkdir -p $(OBJDIR)/utils
	mkdir -p $(BUILDDIR)

# Compile source files (protocol headers are committed to git)
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Compile protocol files
$(OBJDIR)/%.o: $(PROTOCOLDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_TARGET): $(OBJECTS) $(PROTOCOL_OBJECTS)
	$(CC) $(OBJECTS) $(PROTOCOL_OBJECTS) -o $@ $(LDFLAGS)

.PHONY: $(TARGET)
$(TARGET): $(BUILD_TARGET)
	ln -sfn $(BUILD_TYPE)/herdcat $@

# Regenerate Wayland protocol bindings from XML sources (requires wayland-scanner).
# The generated files are committed to git, so this target only needs to be run
# manually when the protocol XML sources are updated.
protocols:
	@command -v wayland-scanner >/dev/null 2>&1 || { \
		echo "ERROR: wayland-scanner not found."; \
		echo "Install it with your package manager (e.g. 'pacman -S wayland', 'apt install libwayland-bin')."; \
		exit 1; \
	}
	wayland-scanner private-code $(PROTOCOLDIR)/xdg-shell.xml $(PROTOCOLDIR)/xdg-shell-protocol.c
	wayland-scanner private-code $(PROTOCOLDIR)/wlr-layer-shell-unstable-v1.xml $(PROTOCOLDIR)/zwlr-layer-shell-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/wlr-layer-shell-unstable-v1.xml $(PROTOCOLDIR)/zwlr-layer-shell-v1-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/wlr-foreign-toplevel-management-unstable-v1.xml $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/wlr-foreign-toplevel-management-unstable-v1.xml $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-client-protocol.h
	wayland-scanner client-header $(PROTOCOLDIR)/xdg-output-unstable-v1.xml $(PROTOCOLDIR)/xdg-output-unstable-v1-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/xdg-output-unstable-v1.xml $(PROTOCOLDIR)/xdg-output-unstable-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/fractional-scale-v1.xml $(PROTOCOLDIR)/fractional-scale-v1-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/fractional-scale-v1.xml $(PROTOCOLDIR)/fractional-scale-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/viewporter.xml $(PROTOCOLDIR)/viewporter-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/viewporter.xml $(PROTOCOLDIR)/viewporter-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/cursor-shape-v1.xml $(PROTOCOLDIR)/cursor-shape-v1-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/cursor-shape-v1.xml $(PROTOCOLDIR)/cursor-shape-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/tablet-unstable-v2.xml $(PROTOCOLDIR)/tablet-unstable-v2-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/tablet-unstable-v2.xml $(PROTOCOLDIR)/tablet-unstable-v2-protocol.c

clean:
	rm -rf $(BUILDDIR)

# Full clean including generated protocol files (requires wayland-scanner to rebuild)
distclean: clean
	rm -f $(C_PROTOCOL_SRC) $(H_PROTOCOL_HDR)

# Development targets
debug:
	$(MAKE) BUILD_TYPE=debug

release:
	$(MAKE) BUILD_TYPE=release

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/herdcat
	install -Dm644 herdcat.conf.example $(DESTDIR)$(PREFIX)/share/herdcat/herdcat.conf.example
	install -Dm755 scripts/find_input_devices.sh $(DESTDIR)$(PREFIX)/bin/herdcat-find-devices
	install -Dm755 scripts/herdcat-setup $(DESTDIR)$(PREFIX)/bin/herdcat-setup
	install -Dm644 scripts/herdcat_setup_json.py $(DESTDIR)$(PREFIX)/bin/herdcat_setup_json.py
	@for file in integrations/hooks/* integrations/pi/herdcat.ts integrations/opencode/index.js integrations/kitty/* integrations/tmux/*; do \
		install -Dm644 $$file $(DESTDIR)$(PREFIX)/share/herdcat/$$file || exit 1; \
	done
	install -Dm644 man/herdcat.1 $(DESTDIR)$(PREFIX)/share/man/man1/herdcat.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/herdcat
	rm -f $(DESTDIR)$(PREFIX)/bin/herdcat-find-devices
	rm -f $(DESTDIR)$(PREFIX)/bin/herdcat-setup
	rm -f $(DESTDIR)$(PREFIX)/bin/herdcat_setup_json.py
	@for file in integrations/hooks/* integrations/pi/herdcat.ts integrations/opencode/index.js integrations/kitty/* integrations/tmux/*; do \
		rm -f $(DESTDIR)$(PREFIX)/share/herdcat/$$file; \
	done
	-rmdir $(DESTDIR)$(PREFIX)/share/herdcat/integrations/hooks $(DESTDIR)$(PREFIX)/share/herdcat/integrations/pi $(DESTDIR)$(PREFIX)/share/herdcat/integrations/opencode $(DESTDIR)$(PREFIX)/share/herdcat/integrations/kitty $(DESTDIR)$(PREFIX)/share/herdcat/integrations/tmux $(DESTDIR)$(PREFIX)/share/herdcat/integrations
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/herdcat.1
	rm -f $(DESTDIR)$(PREFIX)/share/herdcat/herdcat.conf.example
	-rmdir $(DESTDIR)$(PREFIX)/share/herdcat

# Memory check (requires valgrind)
memcheck: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

# Release with -march=native (local builds only, not for distribution)
release-local:
	$(MAKE) BUILD_TYPE=release RELEASE_CFLAGS="$(RELEASE_CFLAGS) -march=native"

# ThreadSanitizer build for detecting data races
tsan:
	$(MAKE) BUILD_TYPE=debug DEBUG_CFLAGS="$(BASE_CFLAGS) -g3 -O1 -DDEBUG -fsanitize=thread" DEBUG_LDFLAGS="-fsanitize=thread"

# Performance profiling
profile: release
	perf record -g ./$(TARGET)
	perf report

.PHONY: debug release release-local tsan install uninstall analyze memcheck profile format format-check lint test

# =============================================================================
# CODE QUALITY TARGETS
# =============================================================================

# Find all project source files (exclude lib/ and protocols/)
PROJECT_SOURCES = $(shell find $(SRCDIR) -name '*.c' ! -path '*/embedded_assets.c')
PROJECT_HEADERS = $(shell find $(INCDIR) $(SRCDIR) -name '*.h')
ALL_PROJECT_FILES = $(PROJECT_SOURCES) $(PROJECT_HEADERS) $(wildcard tests/*.c tests/*.h)

# Format all project source files
format:
	@echo "Formatting source files..."
	@clang-format -i $(ALL_PROJECT_FILES)
	@echo "Done! Formatted $(words $(ALL_PROJECT_FILES)) files."

# Check if formatting is correct (for CI)
format-check:
	@echo "Checking code formatting..."
	@clang-format --dry-run --Werror $(ALL_PROJECT_FILES)
	@echo "All files are properly formatted."

# Static analysis with clang-tidy (uses .clang-tidy config)
lint:
	@echo "Running static analysis..."
	@clang-tidy $(PROJECT_SOURCES) -- $(CFLAGS)
	@echo "Static analysis complete."

# Alias for lint
analyze: lint

# Generate compile_commands.json for IDE support (requires bear)
# Run: make compiledb
compiledb: clean
	@echo "Generating compile_commands.json..."
	@bear -- $(MAKE) all 2>/dev/null || (echo "Note: 'bear' not installed. Install with: sudo pacman -S bear" && false)
	@echo "compile_commands.json generated!"

# =============================================================================
# TEST TARGETS
# =============================================================================

TESTDIR = tests
TEST_CFLAGS = $(BASE_CFLAGS) -g3 -O0 -DDEBUG -DTEST_BUILD
TEST_LDFLAGS = $(TEXT_LIBS) -lm -lpthread

COMPOSITOR_TEST_DEPS = src/platform/compositor.c src/platform/compositor_niri.c src/platform/compositor_niri_json.c src/platform/compositor_niri_windows.c src/platform/compositor_hyprland.c src/platform/compositor_sway.c src/platform/compositor_stream.c src/utils/json.c

OVERLAY_SIGNS_TEST_DEPS = src/graphics/text.c src/config/nameplate.c src/graphics/sign_names.c src/graphics/sign_nameplate.c src/platform/overlay_signs.c src/platform/overlay_menu.c src/platform/overlay_signs_geometry.c
SIGNS_TEST_DEPS = src/graphics/text.c src/config/nameplate.c src/graphics/sign_names.c src/graphics/sign_nameplate.c src/graphics/sign_palette.c src/graphics/signs.c src/graphics/signs_fan.c src/graphics/signs_post.c src/graphics/signs_menu.c src/graphics/signs_transform.c

# Source files needed by test_config
CONFIG_MODULE_SOURCES = src/config/nameplate.c src/config/config.c src/config/config_parse.c src/config/config_validate.c
CONFIG_TEST_DEPS = $(CONFIG_MODULE_SOURCES) src/utils/error.c

$(BUILDDIR)/test_config: $(TESTDIR)/test_config.c $(CONFIG_TEST_DEPS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_paw_frame: $(TESTDIR)/test_paw_frame.c | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_scale: $(TESTDIR)/test_scale.c | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_fullscreen_state: $(TESTDIR)/test_fullscreen_state.c | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_runtime: $(TESTDIR)/test_runtime.c src/core/control.c src/config/config_watcher.c $(CONFIG_TEST_DEPS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_terminal_focus: tests/test_terminal_focus.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c src/platform/focus_watch.c src/platform/focus_current.c src/platform/agent_watch.c src/platform/focus.c src/platform/focus_windows.c $(COMPOSITOR_TEST_DEPS) src/platform/command_job.c src/platform/focus_json.c src/platform/agent_terminal.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/agent_adapters.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_focus: tests/test_focus.c src/platform/focus_watch.c src/platform/focus_current.c src/platform/agent_watch.c src/platform/focus.c src/platform/focus_windows.c $(COMPOSITOR_TEST_DEPS) src/platform/command_job.c src/platform/focus_json.c src/platform/agent_terminal.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/agent_adapters.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_focus_watch: tests/test_focus_watch.c src/platform/focus_watch.c src/platform/focus_current.c src/platform/agent_watch.c src/platform/focus.c src/platform/focus_windows.c $(COMPOSITOR_TEST_DEPS) src/platform/command_job.c src/platform/focus_json.c src/platform/agent_terminal.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/agent_adapters.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_text: tests/test_text.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_signs: tests/test_signs.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_sign_draw: tests/test_sign_draw.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_sign_cache: tests/test_sign_cache.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_buffer_damage: tests/test_buffer_damage.c src/platform/shm_buffer.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/text.c src/graphics/animation.c src/graphics/embedded_assets.c src/core/agent_state.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS) -lwayland-client

$(BUILDDIR)/test_font_panel: tests/test_font_panel.c src/graphics/font_panel.c src/graphics/sign_palette.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_font_panel_memory: tests/test_font_panel_memory.c src/graphics/font_panel.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_overlay_signs: tests/test_overlay_signs.c $(CONFIG_MODULE_SOURCES) $(OVERLAY_SIGNS_TEST_DEPS) $(SIGNS_TEST_DEPS) src/graphics/text.c src/core/agent_adapters.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c src/platform/drag.c src/platform/focus_watch.c src/platform/focus_current.c src/platform/agent_watch.c src/platform/focus.c src/platform/focus_windows.c $(COMPOSITOR_TEST_DEPS) src/platform/command_job.c src/platform/focus_json.c src/platform/agent_terminal.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/control.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_theme_pixels: tests/test_theme_pixels.c src/graphics/font_panel.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS) -Wl,--wrap=text_measure

$(BUILDDIR)/test_sign_palette: tests/test_sign_palette.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

THEME_TEST_DEPS = src/platform/theme_watch.c src/platform/command_job.c src/platform/agent_watch.c src/graphics/sign_palette.c src/utils/json.c

$(BUILDDIR)/test_theme_watch: tests/test_theme_watch.c $(THEME_TEST_DEPS) $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) -Wl,--wrap=posix_spawnp

$(BUILDDIR)/test_compositor_backends: tests/test_compositor_backends.c $(COMPOSITOR_TEST_DEPS) src/platform/focus_watch.c src/platform/focus_current.c src/platform/focus.c src/platform/focus_windows.c src/platform/command_job.c src/platform/focus_json.c src/platform/agent_watch.c src/platform/agent_terminal.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/agent_adapters.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) -Wl,--wrap=socket,--wrap=connect

$(BUILDDIR)/test_theme_auto: tests/test_theme_auto.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

STAGE24_SESSION_DEPS = src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/core/agent_state.c src/platform/transcript_path.c src/utils/json.c

$(BUILDDIR)/test_stage24: tests/test_stage24.c $(STAGE24_SESSION_DEPS) $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/platform/agent_watch.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS) -Wl,--wrap=openat

TEST_BINARIES = $(BUILDDIR)/test_stage24 $(BUILDDIR)/test_stage23 $(BUILDDIR)/test_stage22_names $(BUILDDIR)/test_title_hooks $(BUILDDIR)/test_title_focus $(BUILDDIR)/test_agent_title $(BUILDDIR)/test_sign_names $(BUILDDIR)/test_nameplate $(BUILDDIR)/test_name_config $(BUILDDIR)/test_signs_desk_clear $(BUILDDIR)/test_signs_detached $(BUILDDIR)/test_overlay_below $(BUILDDIR)/test_overlay_vertical $(BUILDDIR)/test_signs_below $(BUILDDIR)/test_theme_auto $(BUILDDIR)/test_compositor_backends $(BUILDDIR)/test_theme_watch $(BUILDDIR)/test_overlay_pixels $(BUILDDIR)/test_overlay_geometry $(BUILDDIR)/test_font_panel_memory $(BUILDDIR)/test_terminal_focus $(BUILDDIR)/test_sign_palette $(BUILDDIR)/test_theme_pixels $(BUILDDIR)/test_sign_cache $(BUILDDIR)/test_buffer_damage $(BUILDDIR)/test_transcript $(BUILDDIR)/test_agent_adapters $(BUILDDIR)/test_overlay_signs $(BUILDDIR)/test_font_panel $(BUILDDIR)/test_sign_draw $(BUILDDIR)/test_signs $(BUILDDIR)/test_text $(BUILDDIR)/test_focus $(BUILDDIR)/test_focus_watch $(BUILDDIR)/test_drag $(BUILDDIR)/test_prefs $(BUILDDIR)/test_agent_hook $(BUILDDIR)/test_agent_watch $(BUILDDIR)/test_agent_sessions $(BUILDDIR)/test_agent_state $(BUILDDIR)/test_nanosvg $(BUILDDIR)/test_input $(BUILDDIR)/test_animation $(BUILDDIR)/test_hyprland $(BUILDDIR)/test_runtime $(BUILDDIR)/test_config $(BUILDDIR)/test_paw_frame $(BUILDDIR)/test_scale $(BUILDDIR)/test_fullscreen_state $(BUILDDIR)/test_session_store $(BUILDDIR)/test_agent_discover $(BUILDDIR)/test_agent_terminal

$(BUILDDIR)/test_overlay_pixels: tests/test_overlay_pixels.c src/platform/overlay_geometry.c src/graphics/font_panel.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/text.c src/graphics/animation.c src/graphics/embedded_assets.c src/core/agent_state.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_overlay_geometry: tests/test_overlay_geometry.c src/platform/overlay_geometry.c src/platform/drag.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_drag: tests/test_drag.c src/platform/drag.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_prefs: tests/test_prefs.c src/platform/prefs.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_hook: tests/test_agent_hook.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/platform/agent_terminal.c src/core/agent_adapters.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_watch: tests/test_agent_watch.c src/platform/agent_watch.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_sessions: tests/test_agent_sessions.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_session_store: tests/test_session_store.c src/platform/session_store.c src/platform/agent_terminal.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c src/core/agent_transcript.c src/platform/transcript_watch.c src/platform/agent_watch.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/agent_adapters.c src/core/control.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_discover: tests/test_agent_discover.c src/platform/agent_discover.c src/platform/agent_terminal.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c src/core/agent_adapters.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_terminal: tests/test_agent_terminal.c src/platform/agent_terminal.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_state: tests/test_agent_state.c src/core/agent_state.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(TEST_BINARIES): $(PROJECT_HEADERS) tests/test_helpers.h

test: $(TEST_BINARIES)
	@echo "Running tests..."
	@failures=0; \
	for t in $(TEST_BINARIES); do \
		echo "--- $$(basename $$t) ---"; \
		$$t || failures=$$((failures + 1)); \
	done; \
	echo "--- test_theme_watch.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_theme_watch.py || failures=$$((failures + 1)); \
	echo "--- test_terminal_commands.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_terminal_commands.py || \
		failures=$$((failures + 1)); \
	echo "--- test_kitty_watcher.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_kitty_watcher.py || \
		failures=$$((failures + 1)); \
	echo "--- test_measure_scenarios.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_measure_scenarios.py || \
		failures=$$((failures + 1)); \
	echo "--- test_runtime_helpers.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_runtime_helpers.py || \
		failures=$$((failures + 1)); \
	echo "--- test_setup.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_setup.py || \
		failures=$$((failures + 1)); \
	if command -v node >/dev/null 2>&1; then \
		node tests/test_opencode_titles.mjs || failures=$$((failures + 1)); \
	else \
		echo "SKIP test_opencode_titles.mjs: node is unavailable"; \
	fi; \
	if [ $$failures -gt 0 ]; then \
		echo "$$failures test suite(s) failed"; \
		exit 1; \
	fi; \
	echo "All tests passed."

.PHONY: compiledb test test-sanitize

test-sanitize:
	$(MAKE) clean
	$(MAKE) TEST_CFLAGS="$(TEST_CFLAGS) -fsanitize=address,undefined" TEST_LDFLAGS="$(TEST_LDFLAGS) -fsanitize=address,undefined" test

# Optional protocol fixture; wayland-server is a test-only dependency.
.PHONY: compositor-test-build
compositor-test-build:
	mkdir -p $(BUILDDIR)/compositor
	wayland-scanner server-header protocols/wlr-layer-shell-unstable-v1.xml $(BUILDDIR)/compositor/layer-server.h
	wayland-scanner server-header protocols/viewporter.xml $(BUILDDIR)/compositor/viewport-server.h
	wayland-scanner server-header protocols/fractional-scale-v1.xml $(BUILDDIR)/compositor/scale-server.h
	wayland-scanner server-header protocols/wlr-foreign-toplevel-management-unstable-v1.xml $(BUILDDIR)/compositor/fullscreen-server.h
	$(CC) -std=c2x -g -Wall -Wextra -I$(BUILDDIR)/compositor tests/test_compositor.c tests/test_compositor_pointer.c protocols/zwlr-layer-shell-v1-protocol.c protocols/xdg-shell-protocol.c protocols/viewporter-protocol.c protocols/fractional-scale-v1-protocol.c protocols/wlr-foreign-toplevel-management-v1-protocol.c -o $(BUILDDIR)/compositor/server -lwayland-server

$(BUILDDIR)/test_animation: tests/test_animation.c src/core/agent_state.c src/graphics/animation.c src/graphics/embedded_assets.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_hyprland: tests/test_hyprland.c src/platform/hyprland.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

.PHONY: test-runtime
test-runtime: all compositor-test-build $(BUILDDIR)/test_focus $(BUILDDIR)/stage24_runtime_fixture
	python3 scripts/test_stage24_runtime.py
	python3 scripts/test_stage24_hook.py
	python3 scripts/test_runtime.py
	python3 scripts/test_hook_client.py
	python3 scripts/test_transcript_runtime.py
	python3 scripts/test_focus_client.py
	python3 scripts/test_focus_runtime.py
	python3 scripts/test_sign_options.py
	python3 scripts/test_drag_runtime.py --sign-style fan
	python3 scripts/test_drag_runtime.py --sign-style post
	python3 scripts/test_drag_runtime.py --sign-style off
	python3 scripts/test_font_panel_runtime.py
	python3 scripts/test_below_runtime.py

$(BUILDDIR)/test_input: tests/test_input.c src/platform/input.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) -Wl,--wrap=ioctl,--wrap=stat

$(BUILDDIR)/test_nanosvg: tests/test_nanosvg.c lib/nanosvg.h lib/nanosvgrast.h tests/test_helpers.h | $(OBJDIR)
	$(CC) -std=c2x -Ilib -Itests $(filter -fsanitize=%,$(TEST_CFLAGS)) $< -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_adapters: tests/test_agent_adapters.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/platform/agent_terminal.c src/core/agent_adapters.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_transcript: tests/test_transcript.c src/core/agent_transcript.c src/platform/transcript_watch.c src/platform/agent_watch.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/platform/agent_terminal.c src/core/agent_adapters.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_overlay_vertical: tests/test_overlay_vertical.c src/platform/overlay_vertical.c src/platform/overlay_geometry.c src/platform/drag.c src/graphics/font_panel.c src/graphics/text.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_signs_below: tests/test_signs_below.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c src/graphics/text.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_overlay_below: tests/test_overlay_below.c $(CONFIG_MODULE_SOURCES) $(OVERLAY_SIGNS_TEST_DEPS) $(SIGNS_TEST_DEPS) src/graphics/text.c src/core/agent_adapters.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c src/platform/drag.c src/platform/focus_watch.c src/platform/focus_current.c src/platform/agent_watch.c src/platform/focus.c src/platform/focus_windows.c $(COMPOSITOR_TEST_DEPS) src/platform/command_job.c src/platform/focus_json.c src/platform/agent_terminal.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/control.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_signs_detached: tests/test_signs_detached.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c src/graphics/text.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_signs_desk_clear: tests/test_signs_desk_clear.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_agent_title: tests/test_agent_title.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/core/agent_state.c src/platform/transcript_path.c src/utils/json.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_sign_names: tests/test_sign_names.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_nameplate: tests/test_nameplate.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_name_config: tests/test_name_config.c $(CONFIG_TEST_DEPS) $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_title_focus: tests/test_title_focus.c src/platform/focus_windows.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/agent_adapters.c src/platform/agent_terminal.c src/core/control.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_title_hooks: tests/test_title_hooks.c src/platform/session_store.c src/platform/agent_terminal.c src/core/agent_sessions.c src/core/agent_session_records.c src/core/agent_session_titles.c src/core/agent_title.c src/core/agent_title_kimi.c src/core/agent_title_copilot.c src/utils/json.c src/platform/transcript_path.c src/core/agent_state.c src/core/agent_transcript.c src/platform/transcript_watch.c src/platform/agent_watch.c src/core/agent_hook.c src/core/agent_hook_scan.c src/core/agent_hook_prompt.c src/core/agent_adapters.c src/core/control.c src/utils/error.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) -Wl,--wrap=control_request

$(BUILDDIR)/test_stage22_names: tests/test_stage22_names.c $(CONFIG_TEST_DEPS) $(SIGNS_TEST_DEPS) src/core/agent_adapters.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/post_text_layout.c src/graphics/nameplate_layout.c src/graphics/text.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) $(TEXT_LIBS)

$(BUILDDIR)/test_stage23: tests/test_stage23.c src/graphics/post_text_layout.c src/graphics/sign_draw.c src/graphics/sign_draw_text.c src/graphics/nameplate_layout.c $(SIGNS_TEST_DEPS) src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS) -Wl,--wrap=FT_Get_Sfnt_Table,--wrap=FT_Get_Char_Index

$(BUILDDIR)/stage24_runtime_fixture: tests/stage24_runtime_fixture.c $(STAGE24_SESSION_DEPS) src/platform/agent_watch.c src/graphics/sign_names.c src/core/agent_adapters.c $(PROJECT_HEADERS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $(sort $(filter %.c,$^)) -o $@ $(TEST_LDFLAGS)
