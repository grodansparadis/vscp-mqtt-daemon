# VSCP UDP log helper

The VSCP UDP log helper is a lightweight, header-only debug logging facility that sends formatted log lines to a UDP socket. It is intended for local diagnostics and quick troubleshooting, especially when you want to inspect runtime state without changing the main daemon logging pipeline.

This helper is defined in [src/vscp-udp-log.h](../src/vscp-udp-log.h).

## How it works

When the environment variable `VSCP_ENABLE_UDP_DEBUG` is not set, the logger does nothing and only performs a small state check on each call.

When the variable is set, the first call initializes a UDP socket lazily and sends lines to the configured target address. By default it sends to `127.0.0.1:9999`, but you can override both the IP and the port with environment variables.

## Include it

Add the header to the translation unit where you want debug output:

```c
#include "vscp-udp-log.h"
```

Then emit a message with the macro:

```c
VSCP_UDP_LOG("state=%d addr=0x%02X", state, addr);
```

## Configure the destination

### Linux and macOS

```bash
VSCP_ENABLE_UDP_DEBUG=1 \
VSCP_UDP_DEBUG_IP=127.0.0.1 \
VSCP_UDP_DEBUG_PORT=9999 \
./your_app
```

### Windows

```bat
set VSCP_ENABLE_UDP_DEBUG=1
set VSCP_UDP_DEBUG_IP=127.0.0.1
set VSCP_UDP_DEBUG_PORT=9999
your_app.exe
```

If you do not set `VSCP_UDP_DEBUG_IP` or `VSCP_UDP_DEBUG_PORT`, the helper uses its built-in defaults:

- IP: `127.0.0.1`
- Port: `9999`

## Example usage in a daemon or driver

```c
#include "vscp-udp-log.h"

void some_function(int state, unsigned char addr)
{
    VSCP_UDP_LOG("state=%d addr=0x%02X", state, addr);
}
```

This is useful during development when you want to watch a live stream of state changes from a local UDP listener.

## Thread safety

The helper uses a per-translation-unit mutex around initialization and the enabled-state check. That prevents concurrent threads from racing while the socket is being created on the first use.

## Receive the messages in Python

A simple Python receiver is available at [../src/vscp/python/udp_debug_receiver.py](../src/vscp/python/udp_debug_receiver.py). It listens for UDP packets on the configured host and port and prints each received message to the terminal.

Example:

```bash
python3 ../src/vscp/python/udp_debug_receiver.py --host 127.0.0.1 --port 9999
```

This is useful when testing the logger locally, for example with:

```bash
VSCP_ENABLE_UDP_DEBUG=1 \
VSCP_UDP_DEBUG_IP=127.0.0.1 \
VSCP_UDP_DEBUG_PORT=9999 \
./your_app
```

The receiver script prints one line per packet, keeping the debug stream visible without requiring a separate network sniffer.

## Notes

- The logger is per translation unit, so each source file that includes the header keeps its own internal state.
- If you need a single shared UDP log target across a whole binary, it is better to move the implementation into one common source file and expose a shared configuration API.
- The helper is intended for debugging, not as the main application logging system.

[filename](./bottom_copyright.md ':include')
