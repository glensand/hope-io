---
name: review-code
description: Review code changes in the hope-io project for correctness, style, and adherence to project conventions. Check error handling, cross-platform guards, performance, dead code, and build validation.
---

# Code Review for hope-io

Activate this skill when asked to review code, review a PR, check for issues, or verify adherence to project standards in the hope-io codebase.

## Scope

Check the following areas in order. For each, report what you find — both issues and things that look good.

---

## 1. Error Handling: Assert vs Exception

Check every new or changed error path against the project's strict rules:

- **Must use `HOPE_ASSERT(cond, msg)` for user/programmer errors:**
  - Calling `write()` with `nullptr` buffer or zero `length`
  - Calling `connect()` a second time without `disconnect()` in between
  - Calling `set_options()` before `connect()` / `open()`
  - Calling `open()` on an already-open acceptor
  - Any precondition violation the caller should have ensured

- **Must use `HOPE_THROW(component, msg)` or `HOPE_THROW_ERRNO(component, msg)` for system/environment errors:**
  - `recv()` returns 0 (peer closed connection)
  - `send()` / `recv()` returns -1
  - `socket()`, `bind()`, `listen()`, `accept()`, `connect()` fails
  - `getaddrinfo()` fails (DNS resolution)
  - Any OS syscall failure where `errno` or `WSAGetLastError()` is set

- **Check that no bare `assert()` is used** — always use the `HOPE_ASSERT` macro.

- **Check that no bare `throw std::runtime_error(...)` is used** — always use `HOPE_THROW` or `HOPE_THROW_ERRNO`.

---

## 2. Cross-Platform Correctness

- **Source files**: Every `.cpp` in `nix/`, `linux/`, or `win/` must:
  1. Include `"hope-io/coredefs.h"` first.
  2. Wrap the implementation body with the correct guard:
     - `#if PLATFORM_LINUX || PLATFORM_APPLE` for nix/*
     - `#if PLATFORM_WINDOWS` for win/*

- **Headers**: Class declarations must be wrapped in `#if` guards so the type is only visible on the correct platform.

- **Platform-specific socket options**: Wrap with `#ifdef` (e.g., `TCP_USER_TIMEOUT`, `SO_REUSEPORT`, `SO_MARK`, `SO_BINDTODEVICE`, `TCP_KEEPIDLE`, `TCP_KEEPINTVL`, `TCP_KEEPCNT`, `IP_TOS`, `SO_PRIORITY`).

- **No runtime platform detection**: Compile-time selection only, handled via `factory.h`.

---

## 3. Performance & Low-Latency

- **Branch predictor**: Are there unnecessary branches on hot paths (read/write loops, buffer operations)? Could a branch be restructured so the common path is first?

- **Noexcept**: Are performance-critical functions marked `noexcept` where appropriate?

- **Ring buffer patterns**: If buffer operations are touched, do they follow the `fixed_size_buffer` pattern:
  - Power-of-2 size with `& mask` wrapping (no `%` modulo)
  - Unbounded monotonic head/tail counters
  - `consume_free` / `consume_used` / `peek_used` patterns?

- **Allocations**: Are there unnecessary heap allocations on hot paths? Could a `std::array` replace a `std::vector`?

---

## 4. Dead Code

- Are there any commented-out code blocks?
- Are there unused functions, macros, or types?
- Are there empty macros from removed features that should be cleaned up (like `#define THREAD_SCOPE`)?
- Are there stub implementations (`assert(false && "Not implemented")`) not marked with `TODO::`?

---

## 5. Code Style

- **License header**: Does every new file start with the MIT license header?
- **Include order**: Project headers first (`hope-io/coredefs.h`, then `hope-io/net/...`), then standard library, then system headers.
- **Naming**: `snake_case` for functions/variables, `PascalCase` for types, `UPPER_CASE` for macros.
- **`pragma once`**: All headers must use `#pragma once`.
- **Consistency**: Does the new code follow the style of the surrounding code?

---

## 6. Build Validation Readiness

Check whether the changes would survive the validation sequence:

1. **Full build**: Library + all samples + all tests compile without warnings.
2. **All tests pass**: `ctest --test-dir build --output-on-failure`
3. **Benchmarks don't crash**: `bench_event_loop --mode tcp --payload 64 --connections 10 --duration 2`

Flag any changes that would break one of these steps.

---

## Output Format

For each area, report:
- ✅ **Pass** — no issues found
- ⚠️ **Warning** — minor concern (explain why)
- ❌ **Fail** — must-fix issue (explain what and why)

End with a summary of must-fix items and suggested fixes.
