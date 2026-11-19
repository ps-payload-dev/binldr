# Copyright (C) 2026 ItsJokerZz
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not, see
# <http://www.gnu.org/licenses/>.

PS4_PAYLOAD_SDK ?= /opt/ps-payload-sdk/ps4
ELFLDR_REPO ?= https://github.com/ps-payload-dev/elfldr.git
PTRACE_REPO ?= https://github.com/ps-payload-dev/lib-ptrace.git
DEPS_DIR ?= deps
ELFLDR_DIR ?= $(DEPS_DIR)/elfldr
PTRACE_DIR ?= $(DEPS_DIR)/lib-ptrace
BUILD_DIR ?= build

ELFLDR_BIN := $(ELFLDR_DIR)/build/ps4/elfldr-ps4.bin
BINRUNNER := $(BUILD_DIR)/binrunner.elf
PTRACE_HEADER := $(PTRACE_DIR)/include/ptrace.h
PTRACE_LIB := $(PTRACE_DIR)/build/ps4/libptrace.a
ELFLDR_NOTIFY := $(ELFLDR_DIR)/shared/notify.c

include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk
.DEFAULT_GOAL := all
CFLAGS := -O2 -Wall -Wextra -fPIC -I$(PTRACE_DIR)/include -I$(ELFLDR_DIR)/shared
CXXFLAGS := $(CFLAGS) -std=c++17 -pthread
LDFLAGS := -pthread -lunwind -lc++abi -lc++ -lc -lSceLibcInternal -lSceNet -lkernel_web -lSceSysmodule
LDLIBS :=

LIBRARY := $(BUILD_DIR)/lib-binldr.a
PAYLOAD := $(BUILD_DIR)/binldr.elf
BOOTSTRAPPER := $(BUILD_DIR)/binldr.bin
LIB_OBJECTS := $(BUILD_DIR)/server.o $(BUILD_DIR)/elfldr_blob.o \
	$(BUILD_DIR)/binrunner_blob.o
PAYLOAD_OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/notify.o

.PHONY: all clean deps
all: $(LIBRARY) $(PAYLOAD) $(BOOTSTRAPPER)

deps: $(ELFLDR_BIN)

$(ELFLDR_DIR)/.git:
	@mkdir -p $(DEPS_DIR)
	git clone --depth 1 $(ELFLDR_REPO) $(ELFLDR_DIR)

$(PTRACE_HEADER): | $(ELFLDR_DIR)/.git
	@if [ ! -d "$(PTRACE_DIR)/.git" ]; then git clone --depth 1 $(PTRACE_REPO) $(PTRACE_DIR); fi

$(PTRACE_LIB): $(PTRACE_HEADER)
	$(MAKE) -C $(PTRACE_DIR) PLATFORM=ps4 PS4_PAYLOAD_SDK=$(PS4_PAYLOAD_SDK) all

$(ELFLDR_BIN): $(ELFLDR_DIR)/.git $(PTRACE_LIB)
	$(MAKE) -C $(ELFLDR_DIR) ps4 PS4_PAYLOAD_SDK=$(PS4_PAYLOAD_SDK) PTRACE_DIR=$(abspath $(PTRACE_DIR))

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/server.o: server.cpp binldr.h $(PTRACE_HEADER) $(BINRUNNER) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/elfldr_blob.o: elfldr_blob.S $(ELFLDR_BIN) | $(BUILD_DIR)
	$(CC) -fPIC -DBINLDR_ELFLDR_BIN='"$(abspath $(ELFLDR_BIN))"' -c $< -o $@

$(BINRUNNER): binrunner.c sysmodule_ids.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -std=gnu11 -pthread $< -pthread -lSceSysmodule -lkernel_web \
		-lSceLibcInternal -lc -o $@

$(BUILD_DIR)/binrunner_blob.o: binrunner_blob.S $(BINRUNNER) | $(BUILD_DIR)
	$(CC) -fPIC -DBINLDR_BINRUNNER_ELF='"$(abspath $(BINRUNNER))"' -c $< -o $@

$(LIBRARY): $(LIB_OBJECTS)
	$(AR) rcs $@ $^

$(BUILD_DIR)/main.o: main.cpp binldr.h $(ELFLDR_DIR)/shared/notify.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/notify.o: $(ELFLDR_NOTIFY) $(ELFLDR_DIR)/shared/notify.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(PAYLOAD): $(PAYLOAD_OBJECTS) $(LIBRARY) $(PTRACE_LIB)
	$(CXX) $(PAYLOAD_OBJECTS) $(LIBRARY) $(PTRACE_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/bootstrap-bin.o: bootstrap-bin.c asset.h $(PAYLOAD) | $(BUILD_DIR)
	$(CC) -fPIC -Wall -Wextra -fno-stack-protector -c $< -o $@

$(BUILD_DIR)/bootstrap-bin.elf: $(BUILD_DIR)/bootstrap-bin.o bin_x86_64.x
	$(LD) -T bin_x86_64.x -o $@ $<

$(BOOTSTRAPPER): $(BUILD_DIR)/bootstrap-bin.elf
	$(OBJCOPY) -O binary --only-section=.text $< $@

clean:
	rm -rf $(BUILD_DIR)
