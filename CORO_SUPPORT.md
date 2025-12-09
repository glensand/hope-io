# Coroutine Support for hope-io Streams

## Overview

hope-io now supports C++20 coroutines for efficient asynchronous stream operations. The coroutine system:

- **Enables non-blocking mode automatically** for all async streams
- **Uses platform-native I/O multiplexing** (epoll on Linux/macOS, select on Windows)
- **Checks I/O readiness** before blocking operations to avoid thread stalls
- **Supports timeout parameters** for all async operations
- **Provides clean, synchronous-style code** using `co_await`

## How It Works

### Non-Blocking Mode
When you create an `async_stream`, it automatically enables non-blocking mode on the underlying TCP stream. This prevents any single operation from blocking the entire thread.

### I/O Readiness Checking
Before performing read/write operations, the coroutine system checks if the socket is ready:

**On Linux/macOS:** Uses `poll()` syscall for efficient readiness checking
```cpp
struct pollfd pfd = {socket, POLLIN, 0};  // or POLLOUT for writes
int result = poll(&pfd, 1, timeout_ms);
```

**On Windows:** Uses `select()` syscall
```cpp
fd_set fds;
FD_SET(socket, &fds);
select(socket + 1, &fds, nullptr, nullptr, &timeval);
```

This prevents busy-waiting and allows the OS to efficiently multiplex many connections.

## Files Added

- `stream_coro.h` - Core coroutine support with:
  - `io_readiness` - Platform-agnostic I/O readiness checks
  - `async_read_awaitable` - Async read with readiness checking
  - `async_write_awaitable` - Async write with readiness checking
  - `async_connect_awaitable` - Async connect
  - `coro_task<T>` - Coroutine return type

- `async_stream.h` - Convenient wrapper class
- `coro_stream_client.cpp` - Example demonstrating coroutine usage

## Components

### 1. I/O Readiness (in `stream_coro.h`)

```cpp
class io_readiness {
    enum class mode { read, write };
    static bool check_ready(int32_t socket, mode m, int timeout_ms = 0) noexcept;
};
```

Uses `poll()` on Linux/macOS and `select()` on Windows to check if I/O is ready without blocking.

### 2. Awaitables with Readiness Checking

#### `async_read_awaitable`
- Checks if data is available (non-blocking poll)
- Waits for data with timeout if not immediately available
- Returns bytes read

```cpp
std::size_t bytes = co_await stream.async_read(buffer, size, 3000);  // 3 sec timeout
```

#### `async_write_awaitable`
- Checks if socket is writable
- Waits for writability with timeout if not immediately available
- Performs write operation

```cpp
co_await stream.async_write(data, size, 3000);  // 3 sec timeout
```

#### `async_connect_awaitable`
- Performs asynchronous connection
- Default timeout: 3 seconds

```cpp
co_await stream.async_connect("127.0.0.1", 8080, 5000);  // 5 sec timeout
```

### 3. Async Stream Wrapper (in `async_stream.h`)

```cpp
class async_stream {
    async_read_awaitable async_read(void* buffer, std::size_t length, int timeout_ms = -1);
    async_write_awaitable async_write(const void* data, std::size_t length, int timeout_ms = -1);
    async_connect_awaitable async_connect(std::string_view ip, std::size_t port, int timeout_ms = 3000);
};
```

**Automatically:**
- Enables non-blocking mode on the stream
- Ensures non-blocking mode in `set_options()`

### 4. Coroutine Task Type

```cpp
template<typename T = void>
class coro_task {
    T get();  // Wait for completion and get result
    bool done() const noexcept;
    void resume();
};
```

Manages coroutine lifetime and exception propagation.

## Usage Examples

### Basic Async Operations with Timeouts

```cpp
hope::io::coro_task<> my_coroutine() {
    auto* tcp_stream = hope::io::create_stream();
    hope::io::async_stream stream(tcp_stream);
    
    try {
        // Connect with 5 second timeout
        co_await stream.async_connect("127.0.0.1", 8080, 5000);
        
        // Write with 3 second timeout
        const std::string msg = "Hello";
        co_await stream.async_write(msg.c_str(), msg.length(), 3000);
        
        // Read with 3 second timeout (uses poll/select under the hood)
        char buffer[256] = {0};
        std::size_t bytes = co_await stream.async_read(buffer, sizeof(buffer), 3000);
        
        std::cout << "Received: " << buffer << " (" << bytes << " bytes)" << std::endl;
        
        stream.disconnect();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;  // e.g., timeout
    }
    
    delete tcp_stream;
}
```

### Running Multiple Coroutines

```cpp
int main() {
    try {
        // Run first coroutine
        auto task1 = async_echo_client("127.0.0.1", 8080);
        task1.get();  // Wait for completion
        
        // Run second coroutine
        auto task2 = async_echo_client("127.0.0.1", 8081);
        task2.get();
        
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << std::endl;
        return 1;
    }
}
```

## Performance Characteristics

### Non-Blocking + Poll/Select
- **No busy-waiting** - OS handles I/O multiplexing
- **Efficient timeout handling** - Single poll/select call instead of sleep loops
- **Sequential coroutines scale** - Each coroutine waits independently
- **Thread efficiency** - Each thread can run multiple sequential coroutines

### Timeout Behavior
- **-1 (default)**: No timeout (wait indefinitely)
- **0**: Non-blocking check only (immediate return)
- **> 0**: Wait up to N milliseconds before timeout

### Exception on Timeout
```cpp
try {
    co_await stream.async_read(buffer, size, 1000);  // 1 second timeout
} catch (const std::runtime_error& ex) {
    // ex.what() == "hope-io/async_read: timeout waiting for data"
}
```

## Advanced Usage: Integration with Event Loop

For true async multiplexing with many concurrent connections, use coroutines with the event-loop:

```cpp
// Each connection in event-loop can use async operations
hope::io::coro_task<> handle_connection(hope::io::stream* conn) {
    hope::io::async_stream stream(conn);
    char buffer[256];
    
    while (true) {
        std::size_t n = co_await stream.async_read(buffer, sizeof(buffer), -1);
        if (n == 0) break;
        
        co_await stream.async_write(buffer, n, -1);  // Echo
    }
}
```

## Compiler Support

- **GCC 10+** - Full coroutine support
- **Clang 10+** - Full coroutine support
- **MSVC 2019+** - Full coroutine support

Compile with: `-std=c++20` or `/std:c++latest`

## Key Features Summary

✅ **Platform-agnostic I/O multiplexing** (poll/select/epoll)  
✅ **Automatic non-blocking mode** - Set up for you  
✅ **Timeout support** - All async operations  
✅ **Clean syntax** - `co_await` instead of callbacks  
✅ **Exception safe** - Proper error propagation  
✅ **RAII compliant** - Automatic resource cleanup  
✅ **Low overhead** - No extra threads needed  

## Example: Echo Client

See `coro_stream_client.cpp` for complete working examples with:
- Single async message exchange
- Multiple sequential async operations  
- Timeout handling
- Error handling and recovery
