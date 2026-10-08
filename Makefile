# Compiler
.DEFAULT_GOAL := all
CC = gcc
PREFIX ?= /usr/local
INPUT_HELPER_SETGID ?= 1
INPUT_HELPER_PATH ?= $(PREFIX)/lib/herdcat/herdcat-input
ifneq ($(INPUT_HELPER_WRAPPER_PATH),)
BASE_CFLAGS += -DHERDCAT_INPUT_WRAPPER_PATH='"$(INPUT_HELPER_WRAPPER_PATH)"'
endif

# Build type (debug or release)
BUILD_TYPE ?= release

# Base flags (using c2x for C23 compatibility on GCC 9+)
BASE_CFLAGS += -std=c2x -Iinclude -Ilib -Iprotocols
BASE_CFLAGS += -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-prototypes
BASE_CFLAGS += -Wmissing-prototypes -Wold-style-definition -Wredundant-decls
BASE_CFLAGS += -Wnested-externs -Wmissing-include-dirs -Wlogical-op
BASE_CFLAGS += -Wjump-misses-init -Wdouble-promotion -Wshadow
BASE_CFLAGS += -fstack-protector-strong
TEXT_CFLAGS := $(shell pkg-config --cflags freetype2 fontconfig)
TEXT_LIBS := $(shell pkg-config --libs freetype2 fontconfig)
BASE_CFLAGS += $(TEXT_CFLAGS)
BASE_CFLAGS += -DHERDCAT_INPUT_HELPER_PATH='"$(INPUT_HELPER_PATH)"'

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
SOURCES = $(shell find $(SRCDIR) -name "*.c" ! -path "src/input/*")
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

# Tests must not inherit the agent that happens to run them.
unexport CLAUDE_PID

.PHONY: all clean distclean protocols embed-assets format format-check lint

all: $(TARGET) $(BUILDDIR)/herdcat-input

# Prefix/wrapper changes must rebuild the selector, including an install with
# a different PREFIX after a local build. Keep helper-location state separate
# from the privileged program's flags and sources.
.PHONY: input-helper-path-check
$(BUILDDIR)/input-helper-paths: input-helper-path-check | $(OBJDIR)
	@printf '%s\n' '$(INPUT_HELPER_PATH)' '$(INPUT_HELPER_WRAPPER_PATH)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(OBJDIR)/platform/input.o: $(BUILDDIR)/input-helper-paths

# Separate hardened libc-only executable, even in debug builds. Sanitizer
# runtimes and the renderer libraries must never enter the privileged helper.
$(BUILDDIR)/herdcat-input: src/input/input_helper.c include/platform/input_protocol.h | $(OBJDIR)
	$(CC) -std=c2x -Iinclude -Wall -Wextra -Wpedantic -O2 -fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=2 $< -o $@ -pie -Wl,-z,relro,-z,now,-z,noexecstack

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

install: $(TARGET) $(BUILDDIR)/herdcat-input
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/herdcat
	install -Dm755 $(BUILDDIR)/herdcat-input $(DESTDIR)$(PREFIX)/lib/herdcat/herdcat-input
	@if [ "$(INPUT_HELPER_SETGID)" != 0 ]; then \
		if [ "$$(id -u)" = 0 ]; then \
			chown root:input $(DESTDIR)$(PREFIX)/lib/herdcat/herdcat-input && \
			chmod 2755 $(DESTDIR)$(PREFIX)/lib/herdcat/herdcat-input || exit 1; \
		else \
			echo "Input helper installed without setgid; root:input mode 2755 or device ACL/input group access is required."; \
		fi; \
	fi
	install -Dm644 herdcat.conf.example $(DESTDIR)$(PREFIX)/share/herdcat/herdcat.conf.example
	install -Dm755 scripts/find_input_devices.sh $(DESTDIR)$(PREFIX)/bin/herdcat-find-devices
	install -Dm755 scripts/herdcat-setup $(DESTDIR)$(PREFIX)/bin/herdcat-setup
	install -Dm644 scripts/herdcat_setup_json.py $(DESTDIR)$(PREFIX)/share/herdcat/herdcat_setup_json.py
	install -d $(DESTDIR)$(PREFIX)/lib/systemd/user
	sed 's|@BINDIR@|$(PREFIX)/bin|g' packaging/systemd/herdcat.service > $(DESTDIR)$(PREFIX)/lib/systemd/user/herdcat.service
	chmod 644 $(DESTDIR)$(PREFIX)/lib/systemd/user/herdcat.service
	install -Dm644 completions/herdcat.bash $(DESTDIR)$(PREFIX)/share/bash-completion/completions/herdcat
	install -Dm644 completions/_herdcat $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_herdcat
	install -Dm644 completions/herdcat.fish $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/herdcat.fish
	@for file in integrations/hooks/* integrations/pi/herdcat.ts integrations/opencode/index.js integrations/kitty/* integrations/tmux/*; do \
		install -Dm644 $$file $(DESTDIR)$(PREFIX)/share/herdcat/$$file || exit 1; \
	done
	install -Dm644 man/herdcat.1 $(DESTDIR)$(PREFIX)/share/man/man1/herdcat.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/herdcat/herdcat-input
	-rmdir $(DESTDIR)$(PREFIX)/lib/herdcat
	rm -f $(DESTDIR)$(PREFIX)/bin/herdcat
	rm -f $(DESTDIR)$(PREFIX)/bin/herdcat-find-devices
	rm -f $(DESTDIR)$(PREFIX)/bin/herdcat-setup
	rm -f $(DESTDIR)$(PREFIX)/share/herdcat/herdcat_setup_json.py
	rm -f $(DESTDIR)$(PREFIX)/lib/systemd/user/herdcat.service
	rm -f $(DESTDIR)$(PREFIX)/share/bash-completion/completions/herdcat
	rm -f $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_herdcat
	rm -f $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/herdcat.fish
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
	@clang-tidy --quiet $(PROJECT_SOURCES) -- $(CFLAGS)
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

TEST_CFLAGS = $(BASE_CFLAGS) -g3 -O0 -DDEBUG -DTEST_BUILD
TEST_LDFLAGS = $(TEXT_LIBS) -lm -lpthread
# Set here rather than passed down: re-quoting flags on a command line
# loses the quotes inside string-valued -D options.
ifdef TEST_SANITIZE
TEST_CFLAGS += -fsanitize=address,undefined
TEST_LDFLAGS += -fsanitize=address,undefined
endif

# Keep test objects separate from both product build types.
TEST_OBJDIR = $(BUILDDIR)/test/obj
TEST_LIB = $(BUILDDIR)/test/libherdcat.a
TEST_SOURCES = $(filter-out src/core/main.c,$(SOURCES))
TEST_OBJECTS = $(TEST_SOURCES:src/%.c=$(TEST_OBJDIR)/%.o)
TEST_ARCHIVE_OBJECTS = $(TEST_OBJECTS)
TEST_FLAGS = $(BUILDDIR)/test/flags

# The compositor server is a fixture, not a standalone unit test.
TEST_SOURCES_C = $(filter-out tests/test_compositor.c \
  tests/test_compositor_pointer.c,$(wildcard tests/test_*.c))
# Preserve the existing order; newly added tests run after this list.
TEST_ORDER = test_input_helper_selection test_input_helper test_post_split test_subagent_badge \
  test_session_recovery test_sign_rows test_desk_offset test_agent_children \
  test_text_centering test_sign_names_render test_title_hooks test_title_focus \
  test_agent_title test_sign_names test_nameplate test_name_config \
  test_signs_desk_clear test_signs_detached test_overlay_below test_overlay_vertical \
  test_signs_below test_theme_auto test_compositor_backends test_theme_watch \
  test_overlay_pixels test_overlay_geometry test_font_panel_memory test_terminal_focus \
  test_sign_palette test_theme_pixels test_sign_cache test_buffer_damage \
  test_transcript test_agent_adapters test_overlay_signs test_font_panel \
  test_sign_draw test_signs test_text test_focus \
  test_focus_watch test_drag test_prefs test_agent_hook \
  test_agent_watch test_agent_sessions test_agent_state test_nanosvg \
  test_input test_animation test_hyprland test_runtime \
  test_config test_paw_frame test_scale test_fullscreen_state \
  test_session_store test_agent_discover test_agent_terminal
TEST_DISCOVERED = $(TEST_SOURCES_C:tests/%.c=$(BUILDDIR)/%)
TEST_BINARIES = $(filter $(TEST_DISCOVERED),$(addprefix $(BUILDDIR)/,$(TEST_ORDER))) \
  $(filter-out $(addprefix $(BUILDDIR)/,$(TEST_ORDER)),$(TEST_DISCOVERED))

.PHONY: test-flags-check
$(TEST_FLAGS): test-flags-check | $(BUILDDIR)/test
	$(file >$@.tmp,$(TEST_CFLAGS) $(TEST_LDFLAGS))
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(BUILDDIR)/test:
	mkdir -p $@

$(TEST_OBJDIR)/%.o: src/%.c $(TEST_FLAGS)
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) -MMD -MP -c $< -o $@

# Fakes replace whole translation units. All other objects remain shared.
TEST_REDRAW_LIB = $(BUILDDIR)/test/libherdcat-redraw.a
TEST_ANIMATION_LIB = $(BUILDDIR)/test/libherdcat-animation.a
TEST_PANEL_LIB = $(BUILDDIR)/test/libherdcat-panel.a
$(TEST_REDRAW_LIB): TEST_ARCHIVE_OBJECTS = $(filter-out \
  $(TEST_OBJDIR)/platform/overlay_requests.o,$(TEST_OBJECTS))
$(TEST_ANIMATION_LIB): TEST_ARCHIVE_OBJECTS = $(filter-out \
  $(TEST_OBJDIR)/platform/overlay_requests.o \
  $(TEST_OBJDIR)/platform/input.o,$(TEST_OBJECTS))
$(TEST_PANEL_LIB): TEST_ARCHIVE_OBJECTS = $(filter-out \
  $(TEST_OBJDIR)/platform/font_panel.o,$(TEST_OBJECTS))

.SECONDEXPANSION:
$(TEST_LIB) $(TEST_REDRAW_LIB) $(TEST_ANIMATION_LIB) $(TEST_PANEL_LIB): \
  $$(TEST_ARCHIVE_OBJECTS)
	@rm -f $@
	$(AR) rcs $@ $^

$(BUILDDIR)/test_%: tests/test_%.c $(TEST_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $< $(TEST_LIB) \
	  -o $@ $(TEST_LDFLAGS) $(TEST_WRAPS)

# Geometry integration tests reach the actual overlay/font-panel modules.
$(addprefix $(BUILDDIR)/,test_surface_tiers test_surface_tier_pixels): \
$(BUILDDIR)/%: tests/%.c $(TEST_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $< $(TEST_LIB) \
	  $(C_PROTOCOL_SRC) -o $@ $(TEST_LDFLAGS) -lwayland-client

# Dedicated link rules for tests with their own platform fakes.
$(BUILDDIR)/test_overlay_configure: tests/test_overlay_configure.c \
  src/platform/wayland.c $(TEST_LIB)
	$(CC) $(TEST_CFLAGS) -ffunction-sections -fdata-sections -MMD -MP \
	  -MF $@.d $< $(TEST_LIB) $(C_PROTOCOL_SRC) -o $@ $(TEST_LDFLAGS) \
	  -lwayland-client -Wl,--gc-sections \
	  -Wl,--wrap=wl_proxy_get_version -Wl,--wrap=wl_proxy_marshal_flags

$(BUILDDIR)/test_hyprland: tests/test_hyprland.c $(TEST_REDRAW_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $< $(TEST_REDRAW_LIB) \
	  -o $@ $(TEST_LDFLAGS)

$(addprefix $(BUILDDIR)/,test_animation test_overlay_pixels test_buffer_damage): \
$(BUILDDIR)/%: tests/%.c $(TEST_ANIMATION_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $< $(TEST_ANIMATION_LIB) \
	  -o $@ $(TEST_LDFLAGS) -lwayland-client

$(addprefix $(BUILDDIR)/,test_overlay_signs test_overlay_below test_session_recovery): \
$(BUILDDIR)/%: tests/%.c $(TEST_PANEL_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $< $(TEST_PANEL_LIB) \
	  -o $@ $(TEST_LDFLAGS) $(TEST_WRAPS)

# Wrappers need only their linker options; compilation still uses the library.
$(BUILDDIR)/test_input_helper_selection: TEST_WRAPS = -Wl,--wrap=access,--wrap=socketpair,--wrap=posix_spawn,--wrap=close,--wrap=kill,--wrap=waitpid
$(BUILDDIR)/test_input: TEST_WRAPS = -Wl,--wrap=ioctl,--wrap=stat
$(BUILDDIR)/test_theme_pixels: TEST_WRAPS = -Wl,--wrap=text_measure
$(BUILDDIR)/test_theme_watch: TEST_WRAPS = -Wl,--wrap=posix_spawnp
$(BUILDDIR)/test_compositor_backends: TEST_WRAPS = -Wl,--wrap=socket,--wrap=connect
$(BUILDDIR)/test_agent_children: TEST_WRAPS = -Wl,--wrap=openat
$(BUILDDIR)/test_title_hooks: TEST_WRAPS = -Wl,--wrap=control_request
$(BUILDDIR)/test_text_centering: TEST_WRAPS = -Wl,--wrap=FT_Get_Sfnt_Table,--wrap=FT_Get_Char_Index
$(BUILDDIR)/test_ellipsis: TEST_WRAPS = -Wl,--wrap=FT_Get_Char_Index
$(BUILDDIR)/test_session_recovery: TEST_WRAPS = -Wl,--wrap=signs_frame

# These tests embed a source implementation with their own defines/flags.
$(BUILDDIR)/test_input_helper: tests/test_input_helper.c $(TEST_LIB)
	$(CC) -std=c2x -Iinclude -Itests -g -O0 -Wall -Wextra \
	  -MMD -MP -MF $@.d $< $(TEST_LIB) -o $@ \
	  -Wl,--wrap=open,--wrap=openat,--wrap=fstat,--wrap=setresgid,--wrap=getresgid

$(BUILDDIR)/test_nanosvg: tests/test_nanosvg.c $(TEST_LIB)
	$(CC) -std=c2x -Ilib -Itests $(filter -fsanitize=%,$(TEST_CFLAGS)) \
	  -MMD -MP -MF $@.d $< $(TEST_LIB) -o $@ $(TEST_LDFLAGS)

-include $(TEST_OBJECTS:.o=.d) $(TEST_BINARIES:=.d)
-include $(addprefix $(BUILDDIR)/,agent_children_fixture.d \
  input_helper_fixture.d input_fallback_fixture.d)
-include $(BUILDDIR)/test/input_fallback.d

# The completion test asks the program itself for its options.
test: $(TEST_BINARIES) $(TARGET) $(BUILDDIR)/herdcat-input
	@echo "Running tests..."
	@ulimit -n 1024 2>/dev/null || :; \
	failures=0; \
	for t in $(TEST_BINARIES); do \
		echo "--- $$(basename $$t) ---"; \
		$$t || failures=$$((failures + 1)); \
	done; \
	echo "--- test_input_helper.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_input_helper.py || failures=$$((failures + 1)); \
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
	echo "--- test_surface_tier_runtime_geometry.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_surface_tier_runtime_geometry.py || \
		failures=$$((failures + 1)); \
	echo "--- test_setup.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_setup.py || \
		failures=$$((failures + 1)); \
	echo "--- test_completions.py ---"; \
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_completions.py || \
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
	$(MAKE) TEST_SANITIZE=1 test

# Optional protocol fixture; wayland-server is a test-only dependency.
.PHONY: compositor-test-build
compositor-test-build:
	mkdir -p $(BUILDDIR)/compositor
	wayland-scanner server-header protocols/wlr-layer-shell-unstable-v1.xml $(BUILDDIR)/compositor/layer-server.h
	wayland-scanner server-header protocols/viewporter.xml $(BUILDDIR)/compositor/viewport-server.h
	wayland-scanner server-header protocols/fractional-scale-v1.xml $(BUILDDIR)/compositor/scale-server.h
	wayland-scanner server-header protocols/wlr-foreign-toplevel-management-unstable-v1.xml $(BUILDDIR)/compositor/fullscreen-server.h
	$(CC) -std=c2x -g -Wall -Wextra -I$(BUILDDIR)/compositor tests/test_compositor.c tests/test_compositor_pointer.c protocols/zwlr-layer-shell-v1-protocol.c protocols/xdg-shell-protocol.c protocols/viewporter-protocol.c protocols/fractional-scale-v1-protocol.c protocols/wlr-foreign-toplevel-management-v1-protocol.c -o $(BUILDDIR)/compositor/server -lwayland-server

.PHONY: test-runtime
test-runtime: all compositor-test-build $(BUILDDIR)/test_focus $(BUILDDIR)/agent_children_fixture
	python3 scripts/test_sway_runtime.py
	python3 scripts/test_surface_tiers_runtime.py
	python3 scripts/test_subagent_badge_runtime.py
	python3 scripts/test_subagent_visibility_runtime.py
	python3 scripts/test_pointer_hover_runtime.py
	python3 scripts/test_agent_detached.py
	python3 scripts/test_sign_rows_runtime.py
	python3 scripts/test_agent_children_runtime.py
	python3 scripts/test_agent_children_hook.py
	python3 scripts/test_runtime.py
	python3 scripts/test_hook_client.py
	python3 scripts/test_transcript_runtime.py
	python3 scripts/test_agent_quiet_runtime.py
	python3 scripts/test_agent_quiet_runtime.py --agent grok
	python3 scripts/test_focus_client.py
	python3 scripts/test_focus_runtime.py
	python3 scripts/test_sign_options.py
	python3 scripts/test_drag_runtime.py --sign-style fan
	python3 scripts/test_drag_runtime.py --sign-style post
	python3 scripts/test_drag_runtime.py --sign-style off
	python3 scripts/test_font_panel_runtime.py
	python3 scripts/test_below_runtime.py

# A real xdg-shell client for headless Sway; no terminal emulator is needed.
.PHONY: sway-runtime-build
sway-runtime-build: $(BUILDDIR)/sway_toplevel_fixture

$(BUILDDIR)/sway-fixture/xdg-shell-client.h: protocols/xdg-shell.xml
	mkdir -p $(BUILDDIR)/sway-fixture
	wayland-scanner client-header $< $@

$(BUILDDIR)/sway_toplevel_fixture: tests/sway_toplevel_fixture.c protocols/xdg-shell-protocol.c $(BUILDDIR)/sway-fixture/xdg-shell-client.h
	mkdir -p $(BUILDDIR)
	$(CC) -std=c2x -g -Wall -Wextra -Wpedantic -I$(BUILDDIR)/sway-fixture \
	  tests/sway_toplevel_fixture.c protocols/xdg-shell-protocol.c \
	  -o $@ $$(pkg-config --cflags --libs wayland-client)

$(BUILDDIR)/agent_children_fixture: tests/agent_children_fixture.c $(TEST_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $< $(TEST_LIB) \
	  -o $@ $(TEST_LDFLAGS)

# Explicitly outside-sandbox acceptance; never part of make test.
.PHONY: input-helper-runtime-build
input-helper-runtime-build: $(BUILDDIR)/input_helper_fixture $(BUILDDIR)/input_fallback_fixture

$(BUILDDIR)/input_helper_fixture: tests/input_helper_fixture.c $(TEST_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $< $(TEST_LIB) \
	  -o $@ $(TEST_LDFLAGS)

# Rebuild only the selector with a deliberately unavailable helper path.
$(BUILDDIR)/test/input_fallback.o: src/platform/input.c $(TEST_FLAGS)
	$(CC) $(TEST_CFLAGS) -UHERDCAT_INPUT_HELPER_PATH \
	  -UHERDCAT_INPUT_WRAPPER_PATH \
	  -DHERDCAT_INPUT_HELPER_PATH='"/nonexistent/herdcat-input"' \
	  -MMD -MP -c $< -o $@

$(BUILDDIR)/input_fallback_fixture: tests/input_helper_fixture.c \
  $(BUILDDIR)/test/input_fallback.o $(TEST_LIB)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d $^ -o $@ $(TEST_LDFLAGS)
