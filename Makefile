# =============================================================================
#  cbundle -- a small CommonJS module bundler written in C
# =============================================================================
#
#  Quick start
#  -----------
#      make              # build ./build/cbundle (release settings)
#      make BUILD=debug  # build with -O0 -g and assertions enabled
#      make test         # build, then run the test suite
#      make help         # list every target with a one-line description
#
#  Layout
#  ------
#      include/          public headers, added to the include path
#      src/              implementation; every .c here is compiled and linked
#      build/<mode>/     object files and dependency files (never committed)
#      build/cbundle     the linked executable
#      examples/         a sample project used by `make example`
#      tests/            shell-driven test suite used by `make test`
#
#  Customisation
#  -------------
#  Every variable below is assigned with `?=` or `+=`, so all of them can be
#  overridden on the command line without editing this file:
#
#      make CC=gcc-14
#      make CFLAGS='-O3 -march=native'
#      make PREFIX=$HOME/.local install
#      make V=1                       # echo the commands being run
#
# =============================================================================

# -----------------------------------------------------------------------------
# Project identity
# -----------------------------------------------------------------------------
NAME    := cbundle
VERSION := 0.1.0

# -----------------------------------------------------------------------------
# Toolchain
#
# `?=` means "use this only if the variable is not already set", so an
# environment variable or a command-line assignment always wins.
# -----------------------------------------------------------------------------
CC      ?= cc
INSTALL ?= install
RM      ?= rm -f

# -----------------------------------------------------------------------------
# Build mode: release (default) or debug.
#
#   BUILD=release  -O2, no assertions -- what you ship
#   BUILD=debug    -O0 -g3, assertions on, frame pointers kept for profilers
#
# Objects for each mode live in their own directory, so switching modes never
# links stale objects and never forces a full rebuild of the other mode.
# -----------------------------------------------------------------------------
BUILD ?= release

ifeq ($(BUILD),debug)
  OPTFLAGS := -O0 -g3 -fno-omit-frame-pointer
  MODEDEFS := -DDEBUG
else ifeq ($(BUILD),release)
  OPTFLAGS := -O2
  MODEDEFS := -DNDEBUG
else
  $(error BUILD must be 'debug' or 'release', got '$(BUILD)')
endif

# -----------------------------------------------------------------------------
# Compiler flags
#
#   -std=c99 -D_POSIX_C_SOURCE=200809L
#         The code targets C99 plus the POSIX 2008 pieces it actually uses
#         (getcwd, stat, strdup-free string handling). Declaring the feature
#         test macro explicitly keeps the build identical on Linux and macOS.
#   -Wall -Wextra -Wpedantic
#         The baseline warning set. This project builds clean under it.
#   -Wshadow -Wconversion ... 
#         Extra warnings that catch the classes of bug most likely in string
#         and buffer code: shadowed locals, silent narrowing, bad printf args.
#   -MMD -MP
#         Emit a .d file next to each .o listing that object's header
#         dependencies (-MMD skips system headers; -MP adds phony targets so a
#         deleted header does not break the build). These files are included at
#         the bottom of this Makefile, which is what makes `make` rebuild a .c
#         file when a header it includes changes.
# -----------------------------------------------------------------------------
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
            -Wmissing-prototypes -Wpointer-arith -Wwrite-strings \
            -Wno-unused-parameter

CPPFLAGS ?=
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L -DCBUNDLE_VERSION='"$(VERSION)"' $(MODEDEFS)

CFLAGS ?=
CFLAGS += -std=c99 $(OPTFLAGS) $(WARNINGS) -MMD -MP

LDFLAGS ?=
LDLIBS  ?=

# -----------------------------------------------------------------------------
# Installation paths (GNU conventions; DESTDIR supports staged installs)
# -----------------------------------------------------------------------------
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=

# -----------------------------------------------------------------------------
# Sources and derived file names
#
# $(wildcard) globs at parse time, so adding a new src/*.c needs no edit here.
# Each src/foo.c maps to build/<mode>/foo.o and build/<mode>/foo.d.
# -----------------------------------------------------------------------------
SRC_DIR := src
OBJ_DIR := build/$(BUILD)
BIN_DIR := build

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)
TARGET  := $(BIN_DIR)/$(NAME)

# -----------------------------------------------------------------------------
# Command echoing
#
# Recipes are quiet by default and print a short "  CC  src/foo.c" line
# instead. `make V=1` restores the raw commands, which is what you want when
# debugging a flag problem.
# -----------------------------------------------------------------------------
V ?= 0
ifeq ($(V),1)
  Q :=
  say = @true
else
  Q := @
  say = @printf '  %-6s %s\n' '$(1)' '$(2)'
endif

# -----------------------------------------------------------------------------
# Targets
#
# .PHONY marks targets that are commands rather than files, so make never
# skips them because a same-named file happens to exist.
# -----------------------------------------------------------------------------
.PHONY: all help clean distclean install uninstall test example graph \
        debug release rebuild sanitize analyze

# The first target is the default one, so a bare `make` builds the binary.
all: $(TARGET) ## Build the bundler (default target)

# --- link -------------------------------------------------------------------
$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(call say,LINK,$@)
	$(Q)$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

# --- compile ----------------------------------------------------------------
# The `| $(OBJ_DIR)` prerequisite is order-only: the directory must exist
# first, but its modification time must not trigger recompiles.
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(call say,CC,$<)
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	$(Q)mkdir -p $@

# --- convenience mode switches ----------------------------------------------
debug: ## Build with debug settings (alias for BUILD=debug)
	$(Q)$(MAKE) BUILD=debug

release: ## Build with release settings (alias for BUILD=release)
	$(Q)$(MAKE) BUILD=release

rebuild: clean all ## Remove build output, then build from scratch

# --- checks -----------------------------------------------------------------
sanitize: ## Build and run the tests under AddressSanitizer + UBSan
	$(Q)$(MAKE) BUILD=debug \
	    CFLAGS='-std=c99 -O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer $(WARNINGS) -MMD -MP' \
	    LDFLAGS='-fsanitize=address,undefined' test

analyze: ## Run the compiler's static analyzer over the sources
	$(Q)for f in $(SOURCES); do \
	        echo "  ANLZ   $$f"; \
	        $(CC) --analyze $(CPPFLAGS) -std=c99 -Iinclude $$f -o /dev/null || exit 1; \
	    done
	$(Q)echo "analyzer: no findings"

test: $(TARGET) ## Build, then run the test suite in tests/
	$(Q)CBUNDLE=$(abspath $(TARGET)) sh tests/run_tests.sh

# --- demos ------------------------------------------------------------------
example: $(TARGET) ## Bundle examples/app and run the result with node
	$(Q)./$(TARGET) examples/app/index.js -o build/example.bundle.js
	$(call say,GEN,build/example.bundle.js)
	$(Q)if command -v node >/dev/null 2>&1; then \
	        echo '--- node build/example.bundle.js ---'; \
	        node build/example.bundle.js; \
	    else \
	        echo 'node not found; bundle written to build/example.bundle.js'; \
	    fi

graph: $(TARGET) ## Print the dependency graph of examples/app
	$(Q)./$(TARGET) examples/app/index.js --graph

# --- install ----------------------------------------------------------------
install: $(TARGET) ## Install the binary into $(DESTDIR)$(BINDIR)
	$(Q)mkdir -p $(DESTDIR)$(BINDIR)
	$(call say,INSTALL,$(DESTDIR)$(BINDIR)/$(NAME))
	$(Q)$(INSTALL) -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME)

uninstall: ## Remove the installed binary
	$(call say,RM,$(DESTDIR)$(BINDIR)/$(NAME))
	$(Q)$(RM) $(DESTDIR)$(BINDIR)/$(NAME)

# --- cleaning ---------------------------------------------------------------
clean: ## Remove objects and binaries for the current BUILD mode
	$(call say,CLEAN,$(OBJ_DIR))
	$(Q)$(RM) -r $(OBJ_DIR) $(TARGET)

distclean: ## Remove the entire build/ directory and generated test output
	$(call say,CLEAN,build)
	$(Q)$(RM) -r build tests/tmp

# --- help -------------------------------------------------------------------
# Scans this file for lines of the form "target: ... ## description" and prints
# them as a table, so the help text can never drift from the target list.
# $(firstword ...) is required: MAKEFILE_LIST also contains every included .d
# file, and grep would then prefix each match with a filename.
help: ## Show this help
	$(Q)echo '$(NAME) $(VERSION) -- a CommonJS bundler in C'
	$(Q)echo
	$(Q)echo 'Targets:'
	$(Q)grep -E '^[a-zA-Z_-]+:.*## ' $(firstword $(MAKEFILE_LIST)) \
	    | sort \
	    | awk 'BEGIN { FS = "## " } \
	           { split($$1, t, ":"); printf "  %-12s %s\n", t[1], $$2 }'
	$(Q)echo
	$(Q)echo 'Variables:'
	$(Q)echo '  BUILD=debug|release   build mode (current: $(BUILD))'
	$(Q)echo '  CC=<compiler>         C compiler (current: $(CC))'
	$(Q)echo '  PREFIX=<dir>          install prefix (current: $(PREFIX))'
	$(Q)echo '  V=1                   echo the commands being run'

# -----------------------------------------------------------------------------
# Header dependency tracking
#
# Pull in the .d files generated by -MMD. The leading '-' suppresses the
# "no such file" error on the very first build, when none of them exist yet.
# -----------------------------------------------------------------------------
-include $(DEPS)
