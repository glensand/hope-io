# hope-io Agent Instructions

This file documents project-specific conventions and requirements for AI agents working on the hope-io codebase. Agents **must** follow these rules.

---

## 1. Error Handling: Assert vs Exception

All errors in hope-io fall into one of two categories with strict rules:

### 1.1 User Errors → `HOPE_ASSERT`

Use `HOPE_ASSERT(cond, msg)` when the **caller violates the API contract** — i.e., the programmer using the library made a mistake. These are debug-only checks (`assert`) that fire during development.

**Examples of user errors (must use `HOPE_ASSERT`):**
- `write()` called with `nullptr` buffer
- `write()` called with zero `length`
- `connect()` called a second time before `disconnect()`
- `set_options()` called before `connect()` / `open()`
- `open()` called on an already-open acceptor
- Any state precondition that the caller should have ensured

### 1.2 System Errors → `HOPE_THROW` / `HOPE_THROW_ERRNO`

Use `HOPE_THROW(component, msg)` or `HOPE_THROW_ERRNO(component, msg)` when an **external system operation fails** — the environment, OS, network, or peer caused the failure. These always throw a `std::runtime_error`.

**Examples of system errors (must use `HOPE_THROW`):**
- `recv()` returns 0 → peer closed connection: `HOPE_THROW("tcp_stream", "connection closed by peer")`
- `send()` returns -1: `HOPE_THROW_ERRNO("tcp_stream", "cannot write to stream")`
- `socket()`, `connect()`, `bind()`, `listen()`, `accept()` fail
- `getaddrinfo()` fails (DNS resolution)
- Any `errno` / `WSAGetLastError()` from OS syscalls

### 1.3 Summary Table

| Condition | Mechanism | Builds to | When |
|-----------|-----------|-----------|------|
| User/programmer error | `HOPE_ASSERT(cond, msg)` | `assert` (debug, removed in Release) | Precondition violation, wrong API usage |
| System/environment error | `HOPE_THROW(component, msg)` | `throw std::runtime_error` | Always | |
| System error with errno | `HOPE_THROW_ERRNO(component, msg)` | `throw std::runtime_error` with errno detail | `errno` is set |

---

## 2. Cross-Platform Code

This is a multiplatform library supporting Linux, macOS, and Windows. Every platform-dependent code path **must** be guarded with preprocessor defines.

### 2.1 Platform Defines

Defined at build time in `lib/CMakeLists.txt`:
- `PLATFORM_LINUX=1` / `PLATFORM_APPLE=0` / `PLATFORM_WINDOWS=0` on Linux
- `PLATFORM_LINUX=0` / `PLATFORM_APPLE=1` / `PLATFORM_WINDOWS=0` on macOS
- `PLATFORM_LINUX=0` / `PLATFORM_APPLE=0` / `PLATFORM_WINDOWS=1` on Windows

### 2.2 Guard Rules

- **Source files**: Wrap the entire implementation body with `#if PLATFORM_LINUX || PLATFORM_APPLE` (for nix) or `#if PLATFORM_WINDOWS` (for Windows). Always `#include "hope-io/coredefs.h"` first.
- **Headers**: Declare classes inside `#if` guards so the type is only visible on the correct platform.
- **Platform-specific options** (Linux-only, macOS-only, etc.): Wrap with `#ifdef` guards (e.g., `#ifdef TCP_USER_TIMEOUT`, `#ifdef SO_REUSEPORT`, `#ifdef SO_MARK`, `#ifdef SO_BINDTODEVICE`).

### 2.3 Directory Structure

| Directory | Content |
|-----------|---------|
| `lib/hope-io/net/nix/` | Shared POSIX (Linux + macOS) implementations |
| `lib/hope-io/net/linux/` | Linux-only code (io_uring, epoll, KTLS) |
| `lib/hope-io/net/win/` | Windows (Winsock2, IOCP) implementations |
| `lib/hope-io/net/tls/` | TLS code (platform-independent, uses BoringSSL) |

### 2.4 Factory Pattern

Platform selection is handled in `factory.h` via typedefs and `#if` guards. Do **not** add runtime platform detection — compile-time selection only.

---

## 3. Low-Latency & Performance

We prioritize minimal latency and predictable execution. Follow these principles:

### 3.1 Branch Predictor Hygiene

- Avoid unnecessary branches on hot paths.
- Prefer `noexcept` on performance-critical functions.
- Do not add runtime checks in hot code paths beyond what is strictly necessary.
- Use `constexpr` for constants that are known at compile time.
- When branching is unavoidable, structure conditions so the common/expected path is the first branch.

### 3.2 Ring Buffer Patterns

The `event_loop::fixed_size_buffer` class demonstrates our preferred approach:
- Buffer size is a power of 2 (512KB = `2^19`), enabling bitmask wrapping (`& mask`) instead of modulo.
- Unbounded head/tail counters grow monotonically — no modulo on the increment path.
- `consume_free` / `consume_used` callbacks avoid intermediate copies.
- `peek_used()` allows protocol parsers to inspect data before consuming.

### 3.3 Memory & Allocation

- Minimize dynamic allocations in hot paths.
- Prefer stack-allocated fixed-size arrays (e.g., `std::array<iovec, 1024>`) over heap allocations.
- Use `reserve()` on vectors when the size is known in advance.

---

## 4. Build & Validation

After **every** code change, you **must** follow this validation sequence:

### 4.1 Configure & Build Everything

```bash
cmake -S . -B build
cmake --build build -j
```

This must compile successfully for **all** targets:
- The static library (`hope-io`)
- All samples (every `.cpp` in `samples/`)
- All tests (every test in `test/`)

### 4.2 Run All Tests

```bash
ctest --test-dir build --output-on-failure
```

All tests **must pass**. Do not claim tests pass without running them.

### 4.3 Run All Benchmarks

Benchmarks are executables in `benchmark/`, built to `build/bin/`. Run them briefly to verify they don't crash:

```bash
./build/bin/bench_event_loop --mode tcp --payload 64 --connections 10 --duration 2
```

Each benchmark must start, run, and complete without crashing. You do not need to analyze the numbers, but you must verify no crashes or assertions.

See `benchmark/CMakeLists.txt` for the full list of benchmarks and their platform requirements.

---

## 5. Dead Code

- **All code must be used.** If code is not reachable, remove it.
- Do not leave commented-out code blocks.
- If a function, macro, or type is no longer needed, delete it entirely.
- Stub implementations (e.g., `assert(false && "Not implemented")`) are acceptable only for features explicitly marked `TODO` and planned for near-term implementation.
- Empty macros from removed features (like `#define THREAD_SCOPE`) should be cleaned up when the last usage is removed.

---

## 6. Code Style & Conventions

- **License header**: Every file must start with the MIT license header (see any existing file for the exact text).
- **Include order**: Project headers first (`hope-io/coredefs.h`, then `hope-io/net/...`), then standard library, then system headers.
- **Naming**: `snake_case` for functions and variables, `snake_case` for classes/types, `UPPER_CASE` for macros.
- **Comments**: Use `//` for single-line and `/* */` for block comments. Use `TODO::` for planned work.
- **`pragma once`**: All headers use `#pragma once` (no include guards).
- **Formatting**: No enforced formatter — keep consistent with surrounding code.

---

## 7. Communication & Process

- Before making changes, read the relevant files completely to understand existing patterns.
- After changes, always validate as described in section 4.
- If validation fails, fix the issue before declaring the task complete.
- Prefer simpler, data-oriented solutions over complex abstractions.
- Do not add dependencies without justification.
