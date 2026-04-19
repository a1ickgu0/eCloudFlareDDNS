# CloudFlare DDNS Client - Build System
#
# This Makefile builds the complete DDNS client with:
# - HAL abstraction layer
# - Platform adapters (POSIX)
# - Library adapters (libcurl, cJSON)
# - Service layer (DDNS, IP provider)
# - Application layer

# ========== Build Configuration ==========

# Compiler settings
CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2 -g
LDFLAGS ?=

# Build for x86_64 by default on macOS, but allow explicit override
# so tools like arm64 valgrind can run against native binaries.
ifeq ($(shell uname -s),Darwin)
	ARCH ?= x86_64
	ifneq ($(strip $(ARCH)),)
		CFLAGS += -arch $(ARCH)
		LDFLAGS += -arch $(ARCH)
	endif
endif

# ========== Optional Build Features ==========
# Coverage profiling: make COVERAGE=1
ifdef COVERAGE
    CFLAGS += -fprofile-arcs -ftest-coverage
    LDFLAGS += -fprofile-arcs -ftest-coverage
endif

# Performance profiling: make PERF=1
ifdef PERF
    CFLAGS += -fno-omit-frame-pointer
endif

# Thread sanitizer: make TSAN=1
ifdef TSAN
    CFLAGS += -fsanitize=thread -fPIC
    LDFLAGS += -fsanitize=thread
endif

# Address sanitizer: make ASAN=1
ifdef ASAN
    CFLAGS += -fsanitize=address -fno-omit-frame-pointer
    LDFLAGS += -fsanitize=address
endif

# Include paths
INCLUDE_DIRS := include \
                include/common \
                include/hal \
                include/service \
                include/app

INCLUDE_FLAGS := $(addprefix -I,$(INCLUDE_DIRS))

# Library paths and libraries
LIBS := curl cjson pthread
LIB_FLAGS := $(addprefix -l,$(LIBS))

# Build directories
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

# Output binary
TARGET := $(BIN_DIR)/cfddns

# ========== Source Files ==========

# Common sources
COMMON_SRCS :=

# HAL sources
HAL_SRCS := \
    src/hal/errors.c \
    src/hal/platform.c \
    src/hal/http_client.c \
    src/hal/json_parser.c \
    src/hal/logger.c \
    src/hal/config.c

# Platform adapter sources
PLATFORM_SRCS := \
    src/adapter/platform/posix/posix_platform.c

# HTTP adapter sources
HTTP_SRCS := \
    src/adapter/http/http_libcurl.c

# JSON adapter sources
JSON_SRCS := \
    src/adapter/json/json_cjson.c

# Config adapter sources
CONFIG_SRCS := \
    src/adapter/config/config_json.c

# Logger adapter sources
LOGGER_SRCS := \
    src/adapter/logger/logger_console.c \
    src/adapter/logger/logger_file.c \
    src/adapter/logger/logger_syslog.c

# Service layer sources
SERVICE_SRCS := \
    src/service/ip_provider.c \
    src/service/ddns_service.c \
    src/service/wan_manager.c

# Application layer sources
APP_SRCS := \
    src/app/ddns_app.c \
    src/app/main.c

# All sources
ALL_SRCS := $(HAL_SRCS) $(PLATFORM_SRCS) $(HTTP_SRCS) $(JSON_SRCS) \
            $(CONFIG_SRCS) $(LOGGER_SRCS) $(SERVICE_SRCS) $(APP_SRCS)

# Object files
ALL_OBJS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(ALL_SRCS))

# ========== Build Rules ==========
# Core objects (excluding main.c for testing)
CORE_OBJS := $(filter-out $(OBJ_DIR)/app/main.o,$(ALL_OBJS))

.PHONY: all clean install uninstall test test-phase1 test-phase2 test-phase2-parallel test-phase3 test-phase3-parallel test-phase4 test-phase4-perf test-phase4-valgrind test-phase4-asan test-phase4-flamegraph test-phase1-gcov-asan test-phase2-gcov-asan test-phase3-gcov-asan test-phase4-gcov-asan test-all-gcov-asan debug release help

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	@echo "Linking: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(ALL_OBJS) $(LIB_FLAGS)
	@echo "Build complete: $@"

# Generic compilation rule
$(OBJ_DIR)/%.o: src/%.c
	@echo "Compiling: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -c $< -o $@

# ========== Debug/Release Builds ==========

debug: CFLAGS += -DDEBUG -g3 -O0
debug: clean all
	@echo "Debug build complete"

release: CFLAGS += -DNDEBUG -O3
release: clean all
	@echo "Release build complete"

# ========== Install/Uninstall ==========

PREFIX ?= /usr/local
INSTALL_BIN := $(PREFIX)/bin
INSTALL_CONFIG := $(PREFIX)/etc/cfddns

install: $(TARGET)
	@echo "Installing to $(PREFIX)"
	@mkdir -p $(INSTALL_BIN)
	@mkdir -p $(INSTALL_CONFIG)
	@cp $(TARGET) $(INSTALL_BIN)/cfddns
	@if [ ! -f $(INSTALL_CONFIG)/config.json ]; then \
		cp config/example.json $(INSTALL_CONFIG)/config.json; \
	fi
	@chmod +x $(INSTALL_BIN)/cfddns
	@echo "Installation complete"

uninstall:
	@echo "Uninstalling from $(PREFIX)"
	@rm -f $(INSTALL_BIN)/cfddns
	@rm -rf $(INSTALL_CONFIG)
	@echo "Uninstallation complete"

# ========== Test ==========

TEST_DIR := tests
TEST_SRCS := $(TEST_DIR)/test_main.c
TEST_TARGET := $(BIN_DIR)/cfddns_test
TEST_OBJS := $(ALL_OBJS)

# Mock library sources
MOCK_SRCS := \
    $(TEST_DIR)/mock/mock_http_client.c \
    $(TEST_DIR)/mock/mock_ip_provider.c \
    $(TEST_DIR)/mock/mock_time.c

MOCK_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(MOCK_SRCS))

# Phase-specific test sources
PHASE1_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase1_unit/test_ip_validation.c

PHASE1_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE1_TEST_SRCS))

PHASE1_PLATFORM_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase1_unit/test_platform_wrappers.c

PHASE1_LOGGER_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase1_unit/test_logger_wrappers.c

PHASE1_APP_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase1_unit/test_ddns_app_wrappers.c

PHASE1_PLATFORM_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE1_PLATFORM_TEST_SRCS))
PHASE1_LOGGER_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE1_LOGGER_TEST_SRCS))
PHASE1_APP_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE1_APP_TEST_SRCS))

PHASE2_HTTP_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase2_integration/test_http_headers.c

PHASE2_LIBCURL_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase2_integration/test_http_libcurl_adapter.c

PHASE2_JSON_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase2_integration/test_json_lifecycle.c

PHASE2_DDNS_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase2_integration/test_ddns_workflows.c

PHASE2_CONFIG_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase2_integration/test_config_json_paths.c

PHASE2_HTTP_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE2_HTTP_TEST_SRCS))
PHASE2_LIBCURL_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE2_LIBCURL_TEST_SRCS))
PHASE2_JSON_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE2_JSON_TEST_SRCS))
PHASE2_DDNS_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE2_DDNS_TEST_SRCS))
PHASE2_CONFIG_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE2_CONFIG_TEST_SRCS))

PHASE3_WAN_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase3_functional/test_multiwan_failover.c

PHASE3_BIND_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase3_functional/test_multirecord_binding.c

PHASE3_IP_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase3_functional/test_ip_mutation.c

PHASE3_CONC_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase3_functional/test_concurrency.c

PHASE3_WAN_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE3_WAN_TEST_SRCS))
PHASE3_BIND_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE3_BIND_TEST_SRCS))
PHASE3_IP_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE3_IP_TEST_SRCS))
PHASE3_CONC_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE3_CONC_TEST_SRCS))

PHASE4_LONG_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase4_longevity/test_phase4_longevity.c

PHASE4_PERF_TEST_SRCS := \
	$(TEST_DIR)/test_case/phase4_longevity/test_phase4_perf.c

PHASE4_LONG_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE4_LONG_TEST_SRCS))
PHASE4_PERF_TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(PHASE4_PERF_TEST_SRCS))

# Test binaries
TEST_PHASE1_TARGET := $(BIN_DIR)/test_phase1
TEST_PHASE2_HTTP_TARGET := $(BIN_DIR)/test_phase2_http
TEST_PHASE2_LIBCURL_TARGET := $(BIN_DIR)/test_phase2_libcurl
TEST_PHASE2_JSON_TARGET := $(BIN_DIR)/test_phase2_json
TEST_PHASE2_DDNS_TARGET := $(BIN_DIR)/test_phase2_ddns
TEST_PHASE2_CONFIG_TARGET := $(BIN_DIR)/test_phase2_config
TEST_PHASE3_WAN_TARGET := $(BIN_DIR)/test_phase3_wan
TEST_PHASE3_BIND_TARGET := $(BIN_DIR)/test_phase3_binding
TEST_PHASE3_IP_TARGET := $(BIN_DIR)/test_phase3_ip
TEST_PHASE3_CONC_TARGET := $(BIN_DIR)/test_phase3_concurrency
TEST_PHASE4_LONG_TARGET := $(BIN_DIR)/test_phase4_longevity
TEST_PHASE4_PERF_TARGET := $(BIN_DIR)/test_phase4_perf
TEST_PHASE1_PLATFORM_TARGET := $(BIN_DIR)/test_phase1_platform
TEST_PHASE1_LOGGER_TARGET := $(BIN_DIR)/test_phase1_logger
TEST_PHASE1_APP_TARGET := $(BIN_DIR)/test_phase1_app

test: $(TEST_TARGET)
	@echo "Running tests..."
	@$(TEST_TARGET)

# ========== Phase 1 Tests ==========

test-phase1: $(TEST_PHASE1_TARGET) $(TEST_PHASE1_PLATFORM_TARGET) $(TEST_PHASE1_LOGGER_TARGET) $(TEST_PHASE1_APP_TARGET)
	@echo "Running Phase 1 (Unit Tests)..."
	@$(TEST_PHASE1_TARGET)
	@$(TEST_PHASE1_PLATFORM_TARGET)
	@$(TEST_PHASE1_LOGGER_TARGET)
	@$(TEST_PHASE1_APP_TARGET)

test-phase1-gcov-asan:
	@echo "Running Phase 1 with COVERAGE=1 and ASAN=1..."
	@$(MAKE) clean >/dev/null
	@$(MAKE) COVERAGE=1 ASAN=1 test-phase1
	@bash tests/coverage/gcov_report.sh

$(TEST_PHASE1_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE1_TEST_OBJS)
	@echo "Linking Phase 1 tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)
	@echo "Phase 1 test binary ready: $@"

$(TEST_PHASE1_PLATFORM_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE1_PLATFORM_TEST_OBJS)
	@echo "Linking Phase 1 platform tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE1_LOGGER_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE1_LOGGER_TEST_OBJS)
	@echo "Linking Phase 1 logger tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE1_APP_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE1_APP_TEST_OBJS)
	@echo "Linking Phase 1 app tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

# ========== Phase 2 Tests ==========

test-phase2: $(TEST_PHASE2_HTTP_TARGET) $(TEST_PHASE2_LIBCURL_TARGET) $(TEST_PHASE2_JSON_TARGET) $(TEST_PHASE2_DDNS_TARGET) $(TEST_PHASE2_CONFIG_TARGET)
	@echo "Running Phase 2 (Integration Tests)..."
	@$(TEST_PHASE2_HTTP_TARGET)
	@$(TEST_PHASE2_LIBCURL_TARGET)
	@$(TEST_PHASE2_JSON_TARGET)
	@$(TEST_PHASE2_DDNS_TARGET)
	@$(TEST_PHASE2_CONFIG_TARGET)

test-phase2-gcov-asan:
	@echo "Running Phase 2 with COVERAGE=1 and ASAN=1..."
	@$(MAKE) clean >/dev/null
	@$(MAKE) COVERAGE=1 ASAN=1 test-phase2
	@bash tests/coverage/gcov_report.sh

test-phase2-parallel: $(TEST_PHASE2_HTTP_TARGET) $(TEST_PHASE2_LIBCURL_TARGET) $(TEST_PHASE2_JSON_TARGET) $(TEST_PHASE2_DDNS_TARGET) $(TEST_PHASE2_CONFIG_TARGET)
	@echo "Running Phase 2 in parallel..."
	@set +e; \
	$(TEST_PHASE2_HTTP_TARGET) & p1=$$!; \
	$(TEST_PHASE2_LIBCURL_TARGET) & p2=$$!; \
	$(TEST_PHASE2_JSON_TARGET) & p3=$$!; \
	$(TEST_PHASE2_DDNS_TARGET) & p4=$$!; \
	$(TEST_PHASE2_CONFIG_TARGET) & p5=$$!; \
	wait $$p1; s1=$$?; \
	wait $$p2; s2=$$?; \
	wait $$p3; s3=$$?; \
	wait $$p4; s4=$$?; \
	wait $$p5; s5=$$?; \
	echo "Phase2 exit codes: http=$$s1 libcurl=$$s2 json=$$s3 ddns=$$s4 config=$$s5"; \
	if [ $$s1 -ne 0 ] || [ $$s2 -ne 0 ] || [ $$s3 -ne 0 ] || [ $$s4 -ne 0 ] || [ $$s5 -ne 0 ]; then exit 1; fi

$(TEST_PHASE2_HTTP_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE2_HTTP_TEST_OBJS)
	@echo "Linking Phase 2 HTTP tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE2_LIBCURL_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE2_LIBCURL_TEST_OBJS)
	@echo "Linking Phase 2 libcurl tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE2_JSON_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE2_JSON_TEST_OBJS)
	@echo "Linking Phase 2 JSON tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE2_DDNS_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE2_DDNS_TEST_OBJS)
	@echo "Linking Phase 2 DDNS tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE2_CONFIG_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE2_CONFIG_TEST_OBJS)
	@echo "Linking Phase 2 config tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

# ========== Phase 3 Tests ==========

test-phase3: $(TEST_PHASE3_WAN_TARGET) $(TEST_PHASE3_BIND_TARGET) $(TEST_PHASE3_IP_TARGET) $(TEST_PHASE3_CONC_TARGET)
	@echo "Running Phase 3 (Functional Tests)..."
	@$(TEST_PHASE3_WAN_TARGET)
	@$(TEST_PHASE3_BIND_TARGET)
	@$(TEST_PHASE3_IP_TARGET)
	@$(TEST_PHASE3_CONC_TARGET)

test-phase3-gcov-asan:
	@echo "Running Phase 3 with COVERAGE=1 and ASAN=1..."
	@$(MAKE) clean >/dev/null
	@$(MAKE) COVERAGE=1 ASAN=1 test-phase3
	@bash tests/coverage/gcov_report.sh

test-phase3-parallel: $(TEST_PHASE3_WAN_TARGET) $(TEST_PHASE3_BIND_TARGET) $(TEST_PHASE3_IP_TARGET) $(TEST_PHASE3_CONC_TARGET)
	@echo "Running Phase 3 in parallel..."
	@set +e; \
	$(TEST_PHASE3_WAN_TARGET) & p1=$$!; \
	$(TEST_PHASE3_BIND_TARGET) & p2=$$!; \
	$(TEST_PHASE3_IP_TARGET) & p3=$$!; \
	$(TEST_PHASE3_CONC_TARGET) & p4=$$!; \
	wait $$p1; s1=$$?; \
	wait $$p2; s2=$$?; \
	wait $$p3; s3=$$?; \
	wait $$p4; s4=$$?; \
	echo "Phase3 exit codes: wan=$$s1 bind=$$s2 ip=$$s3 conc=$$s4"; \
	if [ $$s1 -ne 0 ] || [ $$s2 -ne 0 ] || [ $$s3 -ne 0 ] || [ $$s4 -ne 0 ]; then exit 1; fi

$(TEST_PHASE3_WAN_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE3_WAN_TEST_OBJS)
	@echo "Linking Phase 3 WAN tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE3_BIND_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE3_BIND_TEST_OBJS)
	@echo "Linking Phase 3 binding tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE3_IP_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE3_IP_TEST_OBJS)
	@echo "Linking Phase 3 IP tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE3_CONC_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE3_CONC_TEST_OBJS)
	@echo "Linking Phase 3 concurrency tests: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

# ========== Phase 4 Tests ==========

test-phase4: $(TEST_PHASE4_LONG_TARGET) $(TEST_PHASE4_PERF_TARGET)
	@echo "Running Phase 4 basic checks..."
	@$(TEST_PHASE4_LONG_TARGET) 500
	@$(TEST_PHASE4_PERF_TARGET) | tee tests/perf/phase4_baseline.txt
	@bash tests/test_case/test_phase4_resources.sh

test-phase4-perf: $(TEST_PHASE4_PERF_TARGET)
	@echo "Running Phase 4 performance benchmark..."
	@$(TEST_PHASE4_PERF_TARGET) | tee tests/perf/phase4_baseline.txt
	@if command -v perf >/dev/null 2>&1; then \
		perf record -F 99 -g --call-graph dwarf -o tests/perf/perf.data -- $(TEST_PHASE4_PERF_TARGET) >/dev/null 2>&1 || true; \
		echo "perf.data generated at tests/perf/perf.data"; \
	else \
		echo "PERF_NOT_FOUND on this platform; skipped perf sampling"; \
	fi

test-phase4-valgrind: $(TEST_PHASE4_LONG_TARGET)
	@echo "Running Phase 4 memory check..."
	@bash tests/test_case/test_phase4_memory.sh

test-phase4-asan:
	@echo "Running ASAN fallback for memory checks..."
	@$(MAKE) clean >/dev/null
	@$(MAKE) $(TEST_PHASE4_LONG_TARGET) ASAN=1 >/dev/null
	@ASAN_OPTIONS=halt_on_error=1 $(TEST_PHASE4_LONG_TARGET) 2000

test-phase4-gcov-asan:
	@echo "Running Phase 4 with COVERAGE=1 and ASAN=1..."
	@$(MAKE) clean >/dev/null
	@$(MAKE) COVERAGE=1 ASAN=1 test-phase4
	@bash tests/coverage/gcov_report.sh

test-all-gcov-asan:
	@echo "Running Phase 1-4 with mandatory COVERAGE=1 and ASAN=1..."
	@$(MAKE) clean >/dev/null
	@$(MAKE) COVERAGE=1 ASAN=1 test-phase1
	@$(MAKE) COVERAGE=1 ASAN=1 test-phase2
	@$(MAKE) COVERAGE=1 ASAN=1 test-phase3
	@echo "Running Phase 4 ASAN longevity check without dropping COVERAGE artifacts..."
	@$(MAKE) COVERAGE=1 ASAN=1 $(TEST_PHASE4_LONG_TARGET) >/dev/null
	@ASAN_OPTIONS=halt_on_error=1 $(TEST_PHASE4_LONG_TARGET) 2000
	@bash tests/coverage/gcov_report.sh

test-phase4-flamegraph:
	@echo "Generating flamegraph (if perf data exists)..."
	@if command -v perf >/dev/null 2>&1 && [ -f tests/perf/perf.data ]; then \
		bash tests/perf/flame_graph_gen.sh; \
	else \
		echo "SKIPPED: perf or perf.data unavailable on this platform"; \
	fi

$(TEST_PHASE4_LONG_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE4_LONG_TEST_OBJS)
	@echo "Linking Phase 4 longevity test: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

$(TEST_PHASE4_PERF_TARGET): $(CORE_OBJS) $(MOCK_OBJS) $(PHASE4_PERF_TEST_OBJS)
	@echo "Linking Phase 4 perf test: $@"
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIB_FLAGS)

# Compile mock files
$(OBJ_DIR)/tests/mock/%.o: $(TEST_DIR)/mock/%.c
	@echo "Compiling mock: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -I$(TEST_DIR) -c $< -o $@

# Compile phase test files
$(OBJ_DIR)/tests/test_case/phase1_unit/%.o: $(TEST_DIR)/test_case/phase1_unit/%.c
	@echo "Compiling phase1 test: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -I$(TEST_DIR) -c $< -o $@

$(OBJ_DIR)/tests/test_case/phase2_integration/%.o: $(TEST_DIR)/test_case/phase2_integration/%.c
	@echo "Compiling phase2 test: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -I$(TEST_DIR) -c $< -o $@

$(OBJ_DIR)/tests/test_case/phase3_functional/%.o: $(TEST_DIR)/test_case/phase3_functional/%.c
	@echo "Compiling phase3 test: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -I$(TEST_DIR) -c $< -o $@

$(OBJ_DIR)/tests/test_case/phase4_longevity/%.o: $(TEST_DIR)/test_case/phase4_longevity/%.c
	@echo "Compiling phase4 test: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -I$(TEST_DIR) -c $< -o $@

# ========== Clean ==========

clean:
	@echo "Cleaning build files..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# ========== Help ==========

help:
	@echo "CloudFlare DDNS Client Build System"
	@echo ""
	@echo "Usage: make [target] [OPTIONS]"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build the main binary (default)"
	@echo "  debug       - Build with debug symbols"
	@echo "  release     - Build optimized release version"
	@echo "  clean       - Remove all build files"
	@echo "  install     - Install to \$(PREFIX)"
	@echo "  uninstall   - Remove installation"
	@echo "  test        - Build and run tests"
	@echo "  test-phase1 - Build and run Phase 1 unit tests"
	@echo "  test-phase2 - Build and run Phase 2 integration tests"
	@echo "  test-phase2-parallel - Run Phase 2 tests in parallel"
	@echo "  test-phase3 - Build and run Phase 3 functional tests"
	@echo "  test-phase3-parallel - Run Phase 3 tests in parallel"
	@echo "  test-phase4 - Run Phase 4 longevity/perf/resources checks"
	@echo "  test-phase4-perf - Run Phase 4 performance benchmark"
	@echo "  test-phase4-valgrind - Run memory checks (valgrind/asan fallback)"
	@echo "  test-phase4-asan - Run ASAN fallback for memory checks"
	@echo "  test-phase4-flamegraph - Generate flamegraph when perf.data exists"
	@echo "  test-phase1-gcov-asan - Phase 1 with mandatory gcov+asan"
	@echo "  test-phase2-gcov-asan - Phase 2 with mandatory gcov+asan"
	@echo "  test-phase3-gcov-asan - Phase 3 with mandatory gcov+asan"
	@echo "  test-phase4-gcov-asan - Phase 4 with mandatory gcov+asan"
	@echo "  test-all-gcov-asan - Run Phase 1-4 with mandatory gcov+asan"
	@echo "  help        - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  CC        - Compiler (default: gcc)"
	@echo "  CFLAGS    - Compiler flags"
	@echo "  LDFLAGS   - Linker flags"
	@echo "  PREFIX    - Install prefix (default: /usr/local)"
	@echo ""
	@echo "Build Options (can be combined):"
	@echo "  COVERAGE=1 - Enable code coverage profiling (-fprofile-arcs -ftest-coverage)"
	@echo "  PERF=1     - Enable performance profiling (-fno-omit-frame-pointer)"
	@echo "  TSAN=1     - Enable thread sanitizer"
	@echo "  ASAN=1     - Enable address sanitizer"
	@echo ""
	@echo "Examples:"
	@echo "  make release PREFIX=/opt/cfddns install"
	@echo "  make test-phase1 COVERAGE=1"
	@echo "  make debug ASAN=1"
	@echo "  make PERF=1 test-phase1"

# ========== Dependencies ==========

# Header dependencies
HEADERS := $(wildcard include/**/*.h)

# Create dependency files
$(OBJ_DIR)/%.d: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	@$(CC) $(INCLUDE_FLAGS) -MM $< > $@.tmp
	@sed 's|$(notdir $@)|$@|' $@.tmp > $@
	@rm -f $@.tmp

-include $(ALL_OBJS:.o=.d)

# ========== Special Rules ==========

# Ensure directory structure exists
$(ALL_OBJS): | $(OBJ_DIR)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)/hal \
	          $(OBJ_DIR)/adapter/platform/posix \
	          $(OBJ_DIR)/adapter/http \
	          $(OBJ_DIR)/adapter/json \
	          $(OBJ_DIR)/adapter/config \
	          $(OBJ_DIR)/adapter/logger \
	          $(OBJ_DIR)/service \
	          $(OBJ_DIR)/app \
	          $(OBJ_DIR)/tests/mock \
	          $(OBJ_DIR)/tests/test_case/phase1_unit \
	          $(OBJ_DIR)/tests/test_case/phase2_integration \
	          $(OBJ_DIR)/tests/test_case/phase3_functional \
	          $(OBJ_DIR)/tests/test_case/phase4_longevity