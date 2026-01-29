# CLAUDE.md - Transmission Codebase Guide

This document provides essential context for AI agents working on the Transmission BitTorrent client codebase.

## Project Overview

Transmission is a cross-platform BitTorrent client with multiple UI implementations sharing a common C++ core library. The project uses C++20 and C11, built with CMake.

## Directory Structure

```
transmission/
├── libtransmission/      # Core BitTorrent library (164 files) - THE HEART OF THE PROJECT
├── libtransmission-app/  # Shared utilities for UI clients
├── daemon/               # Headless server (transmission-daemon)
├── gtk/                  # GTK+ client (Linux, C++/gtkmm)
├── qt/                   # Qt client (cross-platform, C++/Qt5/Qt6)
├── macosx/               # Native macOS client (Objective-C++)
├── web/                  # Browser UI (JavaScript, served by daemon)
├── cli/                  # Legacy CLI client (deprecated)
├── utils/                # CLI tools: transmission-remote, -create, -edit, -show
├── tests/                # Unit tests (Google Test)
├── third-party/          # Bundled dependencies
├── cmake/                # CMake modules
├── docs/                 # Documentation (rpc-spec.md is authoritative)
└── po/                   # Translations (94 languages)
```

## Build Commands

```bash
# Configure (from repo root)
cmake -B build -G Ninja

# Build all
cmake --build build

# Build specific target
cmake --build build --target transmission-daemon
cmake --build build --target transmission-qt
cmake --build build --target transmission-gtk

# Run tests
cmake --build build --target test
# Or directly:
cd build && ctest --output-on-failure

# Common CMake options
cmake -B build \
  -DENABLE_DAEMON=ON \
  -DENABLE_GTK=ON \
  -DENABLE_QT=ON \
  -DENABLE_CLI=OFF \
  -DENABLE_TESTS=ON \
  -DUSE_QT_VERSION=6 \
  -DUSE_GTK_VERSION=4
```

## Architecture Overview

### Core Library (libtransmission/)

The core is a standalone C++ library with a C API (`transmission.h`). Key classes:

| Class | File | Purpose |
|-------|------|---------|
| `tr_session` | session.h | Top-level session, owns all torrents and subsystems |
| `tr_torrent` | torrent.h | Single torrent state and lifecycle |
| `tr_swarm` | peer-mgr.cc | Per-torrent peer collection (internal) |
| `tr_peerMgr` | peer-mgr.h | Global peer connection management |
| `tr_peerIo` | peer-io.h | Socket I/O with encryption |
| `tr_peerMsgs` | peer-msgs.h | BitTorrent wire protocol |
| `tr_bandwidth` | bandwidth.h | Hierarchical rate limiting |
| `tr_completion` | completion.h | Download progress tracking |
| `tr_announcer` | announcer.h | Tracker communication |
| `tr_rpc_server` | rpc-server.h | JSON-RPC HTTP server |

### Threading Model

- **Single event loop** (libevent) runs all I/O on session thread
- External threads queue work via `tr_session::run_in_session_thread()`
- `session_mutex_` (recursive) protects shared state
- Verification runs on separate thread pool

### Client Integration Patterns

1. **Local mode** (macOS, GTK): Direct `tr_session*` linking, C API calls
2. **Remote mode** (Qt, Web): JSON-RPC 2.0 over HTTP to daemon

## Key Files to Know

### Core Implementation
- `libtransmission/transmission.h` - Public C API (~1000 functions)
- `libtransmission/session.cc` - Session initialization and management
- `libtransmission/torrent.cc` - Torrent lifecycle
- `libtransmission/peer-mgr.cc` - Peer selection and connection logic
- `libtransmission/peer-msgs.cc` - BitTorrent protocol messages
- `libtransmission/rpcimpl.cc` - RPC method implementations
- `libtransmission/inout.cc` - Disk I/O operations

### Configuration
- `libtransmission/session.h` - `tr_session::Settings` struct (50+ settings)
- `docs/rpc-spec.md` - Authoritative RPC documentation
- `docs/Editing-Configuration-Files.md` - Settings reference

### Tests
- `tests/libtransmission/` - Core library unit tests
- Test files mirror source: `foo.cc` → `foo-test.cc`

## Coding Conventions

### Style
- Run `./code_style.sh` before committing (uses clang-format)
- `.clang-format` and `.clang-tidy` define style rules
- C++20 features encouraged (concepts, ranges, etc.)
- Use `std::` containers and algorithms

### Naming
- Classes: `PascalCase` (e.g., `tr_torrent`, `tr_peerMgr`)
- Functions: `snake_case` (e.g., `tr_torrentStart()`)
- Member variables: `snake_case_` with trailing underscore
- Constants: `PascalCase` (e.g., `BlockSize`, `MaxPeers`)

### Common Patterns
```cpp
// Interned strings for efficiency
tr_interned_string name = tr_interned_string("foo");

// Quark system for fast string comparison
tr_quark key = TR_KEY_download_dir;

// Variant for JSON-like data
tr_variant settings;
settings.init_map();
settings.try_add_int(TR_KEY_peer_port, 51413);

// Observer pattern for events
tor->done_.emit(tor, true);
```

## Important Constants

```cpp
// Block/piece sizes (block-info.h)
tr_block_info::BlockSize = 16384;  // 16 KiB

// Peer limits (session.h defaults)
peer_limit_global = 200;
peer_limit_per_torrent = 50;

// Network (various)
DefaultRpcPort = 9091;
DefaultPeerPort = 51413;
HandshakeTimeoutSec = 30;
KeepaliveIntervalSecs = 100;
RechokePeriodSec = 10;
```

## RPC Interface

Default endpoint: `http://localhost:9091/transmission/rpc`

Key methods:
- `torrent-add`, `torrent-start`, `torrent-stop`, `torrent-remove`
- `torrent-get`, `torrent-set` - Query/modify torrents
- `session-get`, `session-set` - Query/modify settings
- `session-stats` - Transfer statistics

CSRF: Requests need `X-Transmission-Session-Id` header (get from 409 response).

## Testing

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific test
./build/tests/libtransmission/libtransmission-test --gtest_filter="*Bitfield*"

# Test with verbose output
./build/tests/libtransmission/libtransmission-test --gtest_print_time=1
```

## Common Tasks

### Adding a new RPC method
1. Add method handler in `libtransmission/rpcimpl.cc`
2. Register in `methods` map at bottom of file
3. Document in `docs/rpc-spec.md`

### Adding a new session setting
1. Add field to `tr_session::Settings` in `session.h`
2. Add to `Fields` tuple for serialization
3. Add accessor in `session_accessors()` map in `rpcimpl.cc`
4. Update `docs/Editing-Configuration-Files.md`

### Adding a new torrent field
1. Add to `tr_torrent` class in `torrent.h`
2. Add to resume fields if persistent (`resume.h`, `resume.cc`)
3. Add RPC accessor in `torrent_get()` / `torrent_set()` in `rpcimpl.cc`

### Modifying peer protocol
1. Message parsing/building in `peer-msgs.cc`
2. State machine in `tr_peerMsgs` class
3. Handshake changes in `handshake.cc`

## Debugging Tips

### Logging
```cpp
tr_logAddInfo("message");      // Info level
tr_logAddDebug("message");     // Debug level (needs message_level >= 5)
tr_logAddTrace("message");     // Trace level (needs message_level >= 6)
```

Enable via settings: `"message_level": 5`

### Common issues
- **Deadlock**: Don't call libtransmission functions from callbacks
- **Thread safety**: Use `run_in_session_thread()` for cross-thread access
- **Memory**: Most objects owned by session; check ownership before delete

## Platform Notes

### macOS
- Uses Xcode project (`Transmission.xcodeproj`) or CMake
- Native Cocoa UI in Objective-C++
- CommonCrypto for encryption by default

### Linux
- GTK 3 or 4 via gtkmm C++ bindings
- inotify for watch directory
- OpenSSL typically used for crypto

### Windows
- Qt client recommended
- Visual Studio 2019+ or MinGW
- OpenSSL or WolfSSL for crypto

## Dependencies

**Required:** CURL 7.28+, pthread, crypto library (OpenSSL/WolfSSL/MbedTLS/CommonCrypto)

**Bundled in third-party/:**
- libevent (event loop)
- libdeflate (compression)
- miniupnpc, libnatpmp (NAT traversal)
- libutp (µTP protocol)
- dht (Kademlia DHT)
- fmt (string formatting)
- small (small vector optimization)

## Useful Links

- RPC Spec: `docs/rpc-spec.md`
- Build Guide: `docs/Building-Transmission.md`
- Configuration: `docs/Editing-Configuration-Files.md`
- Scripts: `docs/Scripts.md`
