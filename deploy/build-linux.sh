#!/usr/bin/env bash
# Builds the sf4e lobby server on a fresh Linux box (Debian/Ubuntu).
#
# Only the server is built: the launcher, the injected sidecar and the overlay
# are Windows-only and are skipped automatically by CMakeLists.
#
#   chmod +x build-linux.sh && ./build-linux.sh
#
# Run it from the repository root, or from deploy/ -- it finds its own way.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$here"
[ -f "$root/CMakeLists.txt" ] || root="$(dirname "$here")"
if [ ! -f "$root/CMakeLists.txt" ]; then
	echo "ERROR: could not find CMakeLists.txt. Run this from the repo." >&2
	exit 1
fi
cd "$root"

echo "==> Installing build prerequisites (needs sudo)"
sudo apt-get update -y
# curl/zip/unzip/tar/pkg-config are vcpkg's own requirements; the rest are for
# GameNetworkingSockets, which pulls in protobuf and OpenSSL.
sudo apt-get install -y --no-install-recommends \
	build-essential cmake ninja-build git curl zip unzip tar pkg-config \
	python3 ca-certificates

# vcpkg: reuse an existing checkout if VCPKG_ROOT already points at one.
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
	echo "==> Bootstrapping vcpkg into $VCPKG_ROOT"
	[ -d "$VCPKG_ROOT" ] || git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
	"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

echo "==> Configuring"
# No preset: the presets in this repo are all x86-MSVC. Release only -- this is
# a production box, not a debugging one.
cmake -S . -B build-linux -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

echo "==> Building LobbyServer (this pulls and builds GameNetworkingSockets; first run is slow)"
cmake --build build-linux --target LobbyServer

echo
echo "==> Done: $root/build-linux/LobbyServer"
echo "    Test it with:  ./build-linux/LobbyServer"
echo "    Install it as a service with: deploy/install-service-linux.sh"
