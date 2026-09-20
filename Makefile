# mayara_pi — local macOS development convenience.
#
# The real cross-platform builds run in CI via the FE2 ci/ scripts; this
# Makefile is just for fast local iterate-and-test on macOS:
#
#   make            # build the universal (arm64+x86_64) wx3.2 dylib
#   make dev        # build + copy the dylib into OpenCPN (then restart OpenCPN)
#   make tarball    # build an importable OpenCPN plugin tarball
#   make restart    # relaunch OpenCPN
#   make clean
#
# The unit tests are separate and need neither wxWidgets nor OpenCPN, so they
# build and run anywhere a C++17 compiler does:
#
#   make test       # build + run the unit tests (ctest)
#   make test-asan  # the same, under AddressSanitizer + UBSan
#   make coverage   # line coverage for the tested sources
#   make coverage-html
#   make clean-test
#
# Override any of these on the command line, e.g. `make WX_CONFIG=/path/wx-config`.

WX_CONFIG        ?= /usr/local/bin/wx-config
OCPN_PLUGIN_DIR  ?= $(HOME)/Library/Application Support/OpenCPN/Contents/PlugIns
GETTEXT_PREFIX   ?= $(shell brew --prefix gettext 2>/dev/null)
BUILD_DIR        ?= build
ARCHS            ?= arm64;x86_64
DEPLOY           ?= 10.13
JOBS             ?= 4
DYLIB            := $(BUILD_DIR)/libmayara_pi.dylib

export PATH        := $(GETTEXT_PREFIX)/bin:$(PATH)
export WX_VER      := 32
export OCPN_TARGET := macos

CMAKE_FLAGS := \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$(ARCHS)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=$(DEPLOY) \
  -DwxWidgets_CONFIG_EXECUTABLE=$(WX_CONFIG) \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

TEST_BUILD_DIR     ?= build-test
ASAN_BUILD_DIR     ?= build-test-asan
COVERAGE_BUILD_DIR ?= build-coverage

.PHONY: all build dev install tarball restart configure reconfigure clean \
        test test-asan coverage coverage-html clean-test

all: build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -B $(BUILD_DIR) $(CMAKE_FLAGS)

configure: $(BUILD_DIR)/CMakeCache.txt

reconfigure:
	rm -rf $(BUILD_DIR)
	cmake -B $(BUILD_DIR) $(CMAKE_FLAGS)

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

# Fast iteration: rebuild and drop the dylib into OpenCPN. Restart OpenCPN after.
dev install: build
	cp -f "$(DYLIB)" "$(OCPN_PLUGIN_DIR)/"
	@echo "Installed -> $(OCPN_PLUGIN_DIR)/libmayara_pi.dylib  (restart OpenCPN to load)"

# Build an OpenCPN-importable tarball (metadata.xml injected at the root).
tarball: build
	@bash tools/mac-package.sh "$(BUILD_DIR)"

restart:
	-killall OpenCPN 2>/dev/null; sleep 1; open -a OpenCPN

clean:
	rm -rf $(BUILD_DIR)

# --- unit tests ------------------------------------------------------------
# test/ is its own CMake project (see test/CMakeLists.txt): none of the flags
# above apply, because none of the sources under test use wx or the OpenCPN
# plugin API. Deliberately a native build, not a universal one -- a fat binary
# has no coverage profile and takes twice as long to compile.
TEST_CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

test:
	cmake -B $(TEST_BUILD_DIR) -S test $(TEST_CMAKE_FLAGS)
	cmake --build $(TEST_BUILD_DIR) -j$(JOBS)
	ctest --test-dir $(TEST_BUILD_DIR) --output-on-failure

# The decoder's bounds checks are only really proven with a sanitizer watching.
test-asan:
	cmake -B $(ASAN_BUILD_DIR) -S test $(TEST_CMAKE_FLAGS) -DMAYARA_SANITIZE=ON
	cmake --build $(ASAN_BUILD_DIR) -j$(JOBS)
	ctest --test-dir $(ASAN_BUILD_DIR) --output-on-failure

coverage:
	@bash test/coverage.sh $(COVERAGE_BUILD_DIR)

coverage-html:
	@bash test/coverage.sh $(COVERAGE_BUILD_DIR) --html

clean-test:
	rm -rf $(TEST_BUILD_DIR) $(ASAN_BUILD_DIR) $(COVERAGE_BUILD_DIR)
