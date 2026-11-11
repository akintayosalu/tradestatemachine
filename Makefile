# ---------------------------------------------------------------------------
# C++ build
#
#   make                 build the app (debug by default)
#   make run             build and run the app
#   make release         optimized build  (same as: make BUILD=release)
#   make test            build and run all tests
#   make test FILTER=x   run only tests whose name contains "x"
#   make test-list       list test names
#   make test-asan       run tests with AddressSanitizer + UBSan (BUILD=asan)
#   make clean           remove build output
#
# Layout:
#   src/main.cpp   program entry point (app only)
#   src/**.cpp     everything else: engine code, linked into BOTH app and tests
#   include/       headers
#   tests/**.cpp   tests (see tests/README.md), linked into the test binary only
# ---------------------------------------------------------------------------

CXX      ?= c++
BUILD    ?= debug

SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests
OUT_DIR  := build/$(BUILD)
OBJ_DIR  := $(OUT_DIR)/obj

APP_BIN  := $(OUT_DIR)/app
TEST_BIN := $(OUT_DIR)/test_runner

# Pass extra flags without editing this file, e.g.
#   make EXTRA_CXXFLAGS=-DSOMETHING
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS  ?=

CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -I$(INC_DIR) -MMD -MP $(EXTRA_CXXFLAGS)
LDFLAGS  := $(EXTRA_LDFLAGS)

ifeq ($(BUILD),release)
  CXXFLAGS += -O3 -DNDEBUG
else ifeq ($(BUILD),asan)
  SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
  CXXFLAGS += -O1 -g $(SAN)
  LDFLAGS  += $(SAN)
else
  CXXFLAGS += -O0 -g
endif

APP_MAIN  := $(SRC_DIR)/main.cpp
SRCS      := $(shell find $(SRC_DIR) -name '*.cpp')
LIB_SRCS  := $(filter-out $(APP_MAIN),$(SRCS))
TEST_SRCS := $(shell find $(TEST_DIR) -name '*.cpp' 2>/dev/null)

APP_MAIN_OBJ := $(APP_MAIN:%.cpp=$(OBJ_DIR)/%.o)
LIB_OBJS     := $(LIB_SRCS:%.cpp=$(OBJ_DIR)/%.o)
TEST_OBJS    := $(TEST_SRCS:%.cpp=$(OBJ_DIR)/%.o)
DEPS         := $(APP_MAIN_OBJ:.o=.d) $(LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

.PHONY: all run release test test-list test-asan clean FORCE

all: $(APP_BIN)

# Each binary also depends on a file recording its object list. The file is
# rewritten only when the list changes, so deleting or adding a source file
# forces a relink (otherwise a deleted test would keep running).
$(OUT_DIR)/%.objs: FORCE
	@mkdir -p $(@D)
	@echo '$(OBJS_FOR_$*)' | cmp -s - $@ || echo '$(OBJS_FOR_$*)' > $@

OBJS_FOR_app         = $(APP_MAIN_OBJ) $(LIB_OBJS)
OBJS_FOR_test_runner = $(TEST_OBJS) $(LIB_OBJS)

$(APP_BIN): $(APP_MAIN_OBJ) $(LIB_OBJS) $(OUT_DIR)/app.objs
	@mkdir -p $(@D)
	$(CXX) $(filter %.o,$^) $(LDFLAGS) -o $@

$(TEST_BIN): $(TEST_OBJS) $(LIB_OBJS) $(OUT_DIR)/test_runner.objs
	@mkdir -p $(@D)
	$(CXX) $(filter %.o,$^) $(LDFLAGS) -o $@

# Engine and app sources.
$(OBJ_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Test sources can also include headers from tests/ (e.g. test_framework.hpp).
$(OBJ_DIR)/$(TEST_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(TEST_DIR) -c $< -o $@

run: $(APP_BIN)
	./$(APP_BIN)

release:
	$(MAKE) BUILD=release

test: $(TEST_BIN)
	./$(TEST_BIN) '$(FILTER)'

test-list: $(TEST_BIN)
	./$(TEST_BIN) --list

test-asan:
	$(MAKE) BUILD=asan test

clean:
	rm -rf build

-include $(DEPS)
