# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make                  # Build main binary (build/bin/cfddns)
make debug            # Debug build with -g3 -O0 -DDEBUG
make release          # Release build with -O3 -DNDEBUG
make clean            # Remove all build artifacts

# Install/uninstall
make install PREFIX=/usr/local
make uninstall PREFIX=/usr/local

# Run the application
./build/bin/cfddns -c config/example.json
```

## Testing

Tests are organized into 4 phases with separate binaries for each test suite:

```bash
make test-phase1-gcov-asan     # Unit tests (IP validation, platform wrappers, logger)
make test-phase2-gcov-asan     # Integration tests (HTTP, JSON, DDNS workflows)
make test-phase3-gcov-asan     # Functional tests (WAN failover, multi-record, concurrency)
make test-phase4-gcov-asan     # Longevity/performance tests
make test-all-gcov-asan        # Run all phases with coverage and sanitizers

# Individual phase targets (without coverage/sanitizer)
make test-phase1
make test-phase2
make test-phase3
make test-phase4

# Parallel execution for faster feedback
make test-phase2-parallel
make test-phase3-parallel

# Memory checks
make test-phase4-valgrind      # valgrind on Linux, ASAN fallback on macOS
make test-phase4-asan          # Address sanitizer only

# Run a single test binary directly
./build/bin/test_phase1
./build/bin/test_phase2_http
./build/bin/test_phase3_wan
```

Coverage report: `bash tests/coverage/gcov_report.sh` after running tests with `COVERAGE=1`.

## Architecture

This project uses a 4-layer modular architecture:

### 1. HAL Layer (Hardware Abstraction Layer)
- `include/hal/` — Interface definitions
- `src/hal/` — Core implementations that delegate to adapters
- Provides abstract interfaces for: HTTP client, JSON parser, config, logger, platform ops
- Each HAL module has an `impl` vtable pattern for swapping implementations

### 2. Adapter Layer
- `src/adapter/platform/posix/` — POSIX platform implementation (threads, mutex, timer, fs)
- `src/adapter/http/http_libcurl.c` — libcurl HTTP client adapter
- `src/adapter/json/json_cjson.c` — cJSON parser adapter
- `src/adapter/config/config_json.c` — JSON file config adapter
- `src/adapter/logger/` — Console, file, syslog logger backends

Adapters implement HAL interfaces. The HAL layer registers implementations at init time.

### 3. Service Layer
- `src/service/ip_provider.c` — Public IP detection with fallback endpoints, interface binding
- `src/service/ddns_service.c` — Cloudflare API interactions, multi-record management
- `src/service/wan_manager.c` — Multi-WAN interface tracking, failover, alias mapping

Services depend on HAL components via dependency injection (set_http_client, set_logger, etc).

### 4. App Layer
- `src/app/ddns_app.c` — Application lifecycle, signal handling, CLI parsing
- `src/app/main.c` — Entry point

## Multi-WAN and Multi-Record Features

### WAN Interface Binding
WAN manager tracks multiple network interfaces with:
- Alias mapping (e.g., "主 WAN (电信)" → "eth0")
- Health checks and automatic failover
- Interface binding for HTTP requests (libcurl CURLOPT_INTERFACE)
- Both external IP detection and local interface IP reading

### DNS Record Binding Modes
Each DNS record can bind to WAN in three modes:
- `fixed` — Bound to specific WAN interface name/alias
- `dynamic` — Priority-based failover across online WANs
- `auto` — First available WAN

## Configuration

Two config styles:
- `config/example.json` — Single-record simple config
- `config/example_multi.json` — Multi-record + multi-WAN config

Key config sections:
- `cloudflare` — API token, zone info, record settings
- `wan.interfaces[]` — WAN interface configurations with aliases
- `records[]` — DNS record configs with binding_mode and wan_interface

## Dependencies

- `libcurl` — HTTP client
- `cJSON` — JSON parsing
- `pthread` — Threading and mutexes

On macOS: `brew install curl cjson`
On Alpine: `apk add curl-dev cjson-dev`

## Platform Notes

- macOS builds default to x86_64 architecture; override with `ARCH=arm64` for native arm64 builds
- `make test-phase4-valgrind` uses valgrind on Linux, falls back to ASAN on macOS

## Error Handling

Errors defined in `include/common/errors.h`. Use `CFDDNS_ERR_*` codes.
All functions return 0 on success, negative error code on failure.

### Error Checking Macros
```c
CFDDNS_SUCCEEDED(err)   // Check for success (err >= 0)
CFDDNS_FAILED(err)      // Check for failure (err < 0)
CFDDNS_RETURN_ON_ERROR(expr)  // Return early on error
CFDDNS_GOTO_ON_ERROR(expr, label)  // Jump to cleanup on error
```

### Safe Memory and String Macros
Defined in `include/common/macros.h`:
```c
CFDDNS_FREE(ptr)            // Safe free (nullifies pointer)
CFDDNS_STRNCPY(dst, src, size)  // Safe string copy
CFDDNS_STR_EMPTY(s)         // Check NULL or empty string
CFDDNS_STR_EQ(a, b)         // String equality check
```

Always use `CFDDNS_FREE` instead of `free()` to prevent double-free bugs.

## Coding Conventions

- **Memory**: Use `CFDDNS_FREE(ptr)` for all heap deallocations
- **Strings**: Use `CFDDNS_STRNCPY` for bounded copies
- **Errors**: Return `CFDDNS_OK` (0) on success, negative `CFDDNS_ERR_*` on failure
- **Thread Safety**: HAL registries use `pthread_mutex` and `pthread_once` for protection
- **API Visibility**: Public functions marked with `CFDDNS_API` macro in headers

## Test Framework

Custom framework in `tests/test_framework.h`:
```c
TEST_BEGIN();                        // Initialize test run
TEST_RUN("name", test_func);         // Execute named test case
TEST_END();                          // Print summary, return 0/1

ASSERT_TRUE(cond);                   // Boolean assertions
ASSERT_EQ(expected, actual);         // Integer equality
ASSERT_STR_EQ(expected, actual);     // String equality
ASSERT_SUCCESS(result);              // Non-negative result
```

## Adding New Code

When adding new modules:
1. Define interface in `include/hal/` or `include/service/`
2. Implement in `src/hal/` (for HAL) or `src/service/` (for service)
3. If HAL, create adapter implementation in `src/adapter/<category>/`
4. Register implementation via `*_register_impl()` at init
5. Add to Makefile source list (HAL_SRCS, SERVICE_SRCS, etc.)
6. Add corresponding test file in appropriate phase directory