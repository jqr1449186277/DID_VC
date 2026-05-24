.RECIPEPREFIX := >
SHELL := /bin/bash

ROOT ?= $(CURDIR)
CPP_DIR ?= $(ROOT)/cpp
BUILD_DIR ?= $(ROOT)/build
OBJ_DIR ?= $(BUILD_DIR)/obj

CXX ?= g++
CPPFLAGS ?= -I$(CPP_DIR)
CXXFLAGS ?= -O2 -std=c++17
LDLIBS_MAIN ?= -lsodium -lgmpxx -lgmp -pthread
LDLIBS_COMMITTEE ?= -pthread

MAIN_BIN := $(BUILD_DIR)/did_demo_zk
COMMITTEE_BIN := $(BUILD_DIR)/committee_node

MAIN_SOURCES := \
  $(CPP_DIR)/main_submitonly_waitactive_zk.cpp \
  $(CPP_DIR)/main_cli.cpp \
  $(CPP_DIR)/did_app_common.cpp \
  $(CPP_DIR)/app_crypto.cpp \
  $(CPP_DIR)/url_utils.cpp \
  $(CPP_DIR)/csv_utils.cpp \
  $(CPP_DIR)/app_paths.cpp \
  $(CPP_DIR)/identity_state.cpp \
  $(CPP_DIR)/http_transport.cpp \
  $(CPP_DIR)/http_client.cpp \
  $(CPP_DIR)/zk_auth_flow.cpp \
  $(CPP_DIR)/committee_client.cpp \
  $(CPP_DIR)/ttss_flow.cpp \
  $(CPP_DIR)/ttss_artifacts.cpp \
  $(CPP_DIR)/ttss_meta_registrar.cpp \
  $(CPP_DIR)/ttss_setup_flow.cpp \
  $(CPP_DIR)/ttss_rotate_flow.cpp \
  $(CPP_DIR)/trace_flow.cpp \
  $(CPP_DIR)/text_utils.cpp \
  $(CPP_DIR)/json_utils.cpp \
  $(CPP_DIR)/hex_utils.cpp \
  $(CPP_DIR)/normalize_utils.cpp \
  $(CPP_DIR)/process_utils.cpp \
  $(CPP_DIR)/merkle_poseidon.cpp \
  $(CPP_DIR)/zk_backend_internal.cpp \
  $(CPP_DIR)/zk_paths.cpp \
  $(CPP_DIR)/zk_runner.cpp \
  $(CPP_DIR)/zk_public_signals.cpp \
  $(CPP_DIR)/zk_backend.cpp \
  $(CPP_DIR)/verifier_wrap.cpp \
  $(CPP_DIR)/input_export.cpp \
  $(CPP_DIR)/share_envelope.cpp \
  $(CPP_DIR)/ttss_nits_shamir.cpp \
  $(CPP_DIR)/ttss_trace.cpp

COMMITTEE_SOURCES := \
  $(CPP_DIR)/committee_node.cpp \
  $(CPP_DIR)/json_utils.cpp \
  $(CPP_DIR)/text_utils.cpp \
  $(CPP_DIR)/hex_utils.cpp \
  $(CPP_DIR)/share_envelope.cpp

ALL_SOURCES := $(sort $(MAIN_SOURCES) $(COMMITTEE_SOURCES))
MAIN_OBJECTS := $(patsubst $(CPP_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(MAIN_SOURCES))
COMMITTEE_OBJECTS := $(patsubst $(CPP_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(COMMITTEE_SOURCES))
ALL_OBJECTS := $(patsubst $(CPP_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(ALL_SOURCES))
DEPS := $(ALL_OBJECTS:.o=.d)

.PHONY: all main committee clean print-vars

all: committee main

main: $(MAIN_BIN)

committee: $(COMMITTEE_BIN)

$(BUILD_DIR) $(OBJ_DIR):
> mkdir -p "$@"

$(OBJ_DIR)/%.o: $(CPP_DIR)/%.cpp | $(OBJ_DIR)
> $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CXXFLAGS_EXTRA) -MMD -MP -c "$<" -o "$@"

$(MAIN_BIN): $(MAIN_OBJECTS) | $(BUILD_DIR)
> $(CXX) $(LDFLAGS) $(MAIN_OBJECTS) $(LDFLAGS_EXTRA) $(LDLIBS_MAIN) -o "$@"

$(COMMITTEE_BIN): $(COMMITTEE_OBJECTS) | $(BUILD_DIR)
> $(CXX) $(LDFLAGS) $(COMMITTEE_OBJECTS) $(LDFLAGS_EXTRA) $(LDLIBS_COMMITTEE) -o "$@"

clean:
> rm -rf "$(OBJ_DIR)"
> rm -f "$(MAIN_BIN)" "$(COMMITTEE_BIN)"
> echo "[make] cleaned $(BUILD_DIR)"

print-vars:
> echo "ROOT=$(ROOT)"
> echo "CPP_DIR=$(CPP_DIR)"
> echo "BUILD_DIR=$(BUILD_DIR)"
> echo "CXX=$(CXX)"
> echo "CXXFLAGS=$(CXXFLAGS) $(CXXFLAGS_EXTRA)"

-include $(DEPS)
