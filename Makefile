# ---------------------------------------------------------------------------
# C++ build
#
#   make            build (debug by default)
#   make run        build and run
#   make release    optimized build  (same as: make BUILD=release)
#   make clean      remove build output
#
# Any .cpp under src/ is picked up automatically; headers go in include/.
# ---------------------------------------------------------------------------

CXX      ?= c++
BUILD    ?= debug
TARGET   := app

SRC_DIR  := src
INC_DIR  := include
OUT_DIR  := build/$(BUILD)
OBJ_DIR  := $(OUT_DIR)/obj
BIN      := $(OUT_DIR)/$(TARGET)

# Pass extra flags without editing this file, e.g.
#   make EXTRA_CXXFLAGS=-fsanitize=address EXTRA_LDFLAGS=-fsanitize=address
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS  ?=

CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -I$(INC_DIR) -MMD -MP $(EXTRA_CXXFLAGS)
LDFLAGS  := $(EXTRA_LDFLAGS)

ifeq ($(BUILD),release)
  CXXFLAGS += -O3 -DNDEBUG
else
  CXXFLAGS += -O0 -g
endif

SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all run release clean

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(@D)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

release:
	$(MAKE) BUILD=release

clean:
	rm -rf build

-include $(DEPS)
