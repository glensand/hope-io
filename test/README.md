# Hope-IO Test Suite

This test suite provides comprehensive testing for the hope-io networking library, covering both Windows and Unix (Linux/Apple) platforms.

## Test Coverage

### TCP Stream Tests (`test_tcp_stream.cpp`)
- Stream creation and destruction
- Connection to server
- Read and write operations
- `read_once()` functionality
- Stream options (timeouts, buffer sizes, non-blocking mode)
  - **Connection timeout enforcement** - Verifies timeout is actually applied
  - **Read timeout enforcement** - Tests that read operations timeout correctly (Unix)
  - **Write timeout enforcement** - Tests that write operations timeout correctly (Unix)
  - **Options persistence** - Verifies options can be changed multiple times
  - **All parameters** - Tests all option fields (connection_timeout, read_timeout, write_timeout, write_buffer_size, non_block_mode)
  - **Non-blocking mode** - Verifies non-blocking behavior (Unix)
  - **Windows option application** - Tests that Windows applies options during connect
- Large data transfers (1MB+)
- Multiple writes and reads
- Disconnect handling
- Error cases (invalid addresses, closed ports)
- Template read/write for trivial types
- String read/write
- Platform socket access
- Endpoint retrieval

### TCP Acceptor Tests (`test_tcp_acceptor.cpp`)
- Acceptor creation
- Opening ports
- Accepting single and multiple connections
- Setting options on acceptors
- Options applied to accepted connections
- Raw socket access
- Port conflict handling
- Non-blocking accept mode
- Data exchange through accepted connections

### UDP Tests (`test_udp.cpp`)
- UDP builder creation and initialization (Unix only)
- UDP receiver creation (Unix only)
- UDP sender creation (Unix only)
- Send/receive operations (Unix only)
- Multiple packet handling (Unix only)
- Platform-specific behavior (Windows UDP not implemented)

### Event Loop Tests (`test_event_loop.cpp`)
- Event loop creation (Unix/Apple only)
- Event loop run and stop
- Connection handling
- Fixed-size buffer operations
- Connection state management
- Connection equality and hashing
- Platform-specific behavior (Windows event loop not implemented)

### TLS Tests (`test_tls.cpp`)
- TLS acceptor creation (requires OpenSSL and certificates)
- TLS stream creation
- TLS websockets stream creation
- Skips tests if OpenSSL is not available

### Error Handling Tests (`test_error_handling.cpp`)
- Reading from disconnected streams
- Writing to disconnected streams
- Reading after server disconnect
- Writing to closed connections
- Double disconnect safety
- Invalid port numbers
- Connection timeouts
- Read timeouts
- Write timeouts
- Null pointer handling
- Memory cleanup on exceptions

### Platform Compatibility Tests (`test_platform_compatibility.cpp`)
- Platform detection
- Cross-platform stream behavior
- Windows-specific behavior (set_options before connect)
- Unix-specific behavior (set_options after connect)
- Windows `read()` return value bug documentation
- Unix `read()` return value correctness
- `read_once()` length parameter bug (both platforms)
- Windows acceptor options inconsistency
- Unix acceptor options consistency
- UDP platform differences
- Event loop platform differences

## Known Issues Found by Tests

### Windows Platform Issues

1. **`read()` return value bug** (`win_stream.cpp:174`)
   - Returns `length` (which is 0 after loop) instead of total bytes read
   - Should return a counter of total bytes received
   - **Impact**: Callers cannot determine how many bytes were actually read

2. **`set_options()` restriction** (`win_stream.cpp:59-62`)
   - Throws error if called when socket is connected
   - Unix allows setting options after connection
   - **Impact**: Inconsistent behavior across platforms

3. **Acceptor doesn't set options on accepted streams** (`win_acceptor.cpp:90`)
   - Windows acceptor doesn't apply options to accepted connections
   - Unix acceptor does apply options
   - **Impact**: Inconsistent behavior, potential timeout issues

4. **UDP not implemented**
   - `create_receiver()` returns `nullptr`
   - `create_sender()` returns `nullptr`
   - `create_udp_builder()` returns `nullptr`
   - **Impact**: UDP functionality unavailable on Windows

5. **Event loop not implemented**
   - `create_event_loop()` returns `nullptr`
   - `create_event_loop2()` returns `nullptr`
   - **Impact**: Event loop functionality unavailable on Windows

### Cross-Platform Issues

1. **`read_once()` length parameter bug** (both platforms)
   - Uses `length - 1` instead of `length`
   - Found in `win_stream.cpp:178` and `nix_stream.cpp:117`
   - **Impact**: One byte less data can be read than requested

2. **`get_endpoint()` not implemented on Windows** (`win_stream.cpp:50-53`)
   - Returns empty string, asserts false
   - Unix implementation works correctly
   - **Impact**: Cannot get peer address on Windows

3. **Memory management**
   - Factory functions return raw pointers (`new`)
   - No clear ownership semantics
   - **Impact**: Potential memory leaks if not properly deleted

### Unix Platform Issues

1. **`set_options()` assertion** (`nix_stream.cpp:125`)
   - Asserts `m_socket != -1` but doesn't handle uninitialized socket gracefully
   - **Impact**: Crashes in debug mode if called on uninitialized socket

2. **UDP receiver `serv_addr` usage** (`nix_receiver.cpp:81-82`)
   - Uses `serv_addr` in `read()` which might not be initialized correctly
   - **Impact**: Potential incorrect address handling

## Building and Running Tests

### Prerequisites
- CMake 3.11 or higher
- C++17 compatible compiler
- GoogleTest (automatically downloaded via FetchContent)
- OpenSSL (optional, for TLS tests)

### Build Instructions

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Run Tests

```bash
# Run all tests
ctest

# Run specific test executable
./bin/hope-io-test

# Run with verbose output
ctest --verbose
```

### Platform-Specific Notes

- **Windows**: Some tests will be skipped (UDP, event loop)
- **Linux/Unix**: All tests should run
- **TLS Tests**: Will be skipped if OpenSSL is not found

## Test Port Selection

Tests use dynamic port selection to avoid conflicts:
- Base port: 15000
- Port range: 15000-24999 (based on thread ID hash)
- Each test uses a unique port to allow parallel execution

## Contributing

When adding new tests:
1. Follow the existing test structure
2. Use dynamic port selection
3. Clean up resources in `TearDown()`
4. Document platform-specific behavior
5. Add appropriate `GTEST_SKIP()` for unimplemented features

