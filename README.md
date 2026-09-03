# vscp-mqtt-daemon

[![CI Linux](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-linux.yml) 
[![CI macOS](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-macos.yml/badge.svg)](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-macos.yml) 
[![CI Windows](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-windows.yml/badge.svg)](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-windows.yml) 
[![CI Raspberry Pi](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-raspberry-pi.yml/badge.svg)](https://github.com/grodansparadis/vscp-mqtt-daemon/actions/workflows/ci-raspberry-pi.yml)


![](./docs/images/vscp_arcitecture.png)

There are currently two daemons (servers) for the VSCP protocol. The VSCP MQTT daemon (**vscp-mqtt-daemon**) and the [VSCP tcp/ip daemon (vscp-tcpip-daemon)](https://github.com/grodansparadis/vscp-tcpip-daemon). They manily differ in the way they expose VSCP events.

The **vscp-mqtt-daemon** is oriented around the MQTT protocol. It can be used to connect different VSCP transports semlessly to a MQTT server or to many MQTT servers and get data from a MQTT server. It support all level I and level II drivers.

The **vscp-tcpip-daemon** export the VSCP tcp/ip link protocol and can just as the __vscp-mqtt-daemon__ connect to different VSCP transport mechanism but instead of transfering data to a MQTT server it serves other devices through it's own server interface. It support all level I and level II drivers. As there is a MQTT driver ([vscpl2drv-mqtt](https://github.com/grodansparadis/vscpl2drv-mqtt)) it is also possible to connect to MQTT servers with this daemon.

This repository is for the __vscp-mqtt-daemon__.  The vscp-mqtt-daemon is available for Linux, Windows and Macintosh. Binaries is available for all platforms including Raspberrey Pi.

## Build

Fetch the source code with

```bash
git clone --recursive https://github.com/grodansparadis/vscp-mqtt-daemon.git
``` 

### Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release|debug
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
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
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

## Versioning

Versioning is **year.month.patch** where year is last two digits of the release year and month is the two digits of the release month (01-12) and patch is a push counter that is updated on each push to the repositories main branch.

## License

The whole source code is published under the MIT license. Consider the different licenses of possible third party libraries too!

## Contribution
Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the work by you, shall be licensed as above, without any additional terms or conditions.

-----
Copyright (C) 2000-2026 Åke Hedman and contributors, the VSCP project - MIT license.


