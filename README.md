# vscp-mqtt-daemon

There are currently two daemons (servers) for the VSCP protocol. The VSCP MQTT daemon (mqtt-vscpd) and the [VSCP tcp/ip daemon (tcpip-vscpd)](https://github.com/grodansparadis/vscp-tcpip-daemon). 

The **mqtt-vscpd** is oriented around the MQTT protocol. It can be used to connect different VSCP transports semlessly to a MQTT server or to many MQTT servers.

The **tcpip-vscpd** daemon export the VSCP tcp/ip link protocol and can just as the mqtt-vscpd connect to different VSCP transport mechanism but instead of transfering data to a MQTT server it serves other devices through it's own server interface.

This repository is for the mqtt-vscpd.  Got the repository for the [tcpip-vscpd](https://github.com/grodansparadis/vscp-tcpip-daemon) for information about the tcpip-vscpd

## Build

The daemon now builds from this repository root and links against the vendored VSCP sources in `third-party/vscp`.

> Important: files under `third-party/` are vendored dependencies and should not be modified directly unless the task explicitly requires it. Keep project changes in the top-level source tree instead.

### Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### macOS

Install the required dependencies with Homebrew first.

```sh
brew install openssl expat mosquitto curl cjson
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix openssl);$(brew --prefix expat);$(brew --prefix mosquitto);$(brew --prefix curl);$(brew --prefix cjson)"
cmake --build build --parallel
```

### Windows

Use `vcpkg` manifest mode with the repository `vcpkg.json`.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
	-DCMAKE_TOOLCHAIN_FILE=C:/src/vcpkg/scripts/buildsystems/vcpkg.cmake `
	-DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
```

## Packages

Create binary packages with CPack after a successful build.

```sh
cd build
cpack -C Release
```

Default generators are:

- Linux: `TGZ` and `DEB`
- macOS: `TGZ`
- Windows: `ZIP` and `NSIS` when `makensis` is available

## CI

GitHub Actions builds and packages the daemon on Linux, macOS, and Windows from `.github/workflows/ci.yml`.
