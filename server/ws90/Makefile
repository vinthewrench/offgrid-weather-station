# Makefile for ws90_api
#
# Layout assumed:
#   ws90_stack/
#     src/ws90_api.cpp
#     src/json.hpp
#     bin/ws90_api

# Tools
CXX      := g++
# Release flags by default
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS  :=

# Directories
SRC_DIR   := src
BIN_DIR   := bin
BUILD_DIR := build

# Targets
TARGET := $(BIN_DIR)/ws90_api
SRCS   := $(SRC_DIR)/ws90_api.cpp
OBJS   := $(BUILD_DIR)/ws90_api.o

.PHONY: all debug clean run docker-build

all: $(TARGET)

# Debug build: no optimizations, with symbols
debug: CXXFLAGS := -std=c++17 -g -O0 -Wall -Wextra -pedantic
debug: clean $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)/ws90_api

# Optional: rebuild Docker image after building ws90_api
docker-build: $(TARGET)
	docker compose build
