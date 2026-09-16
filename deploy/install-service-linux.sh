#!/usr/bin/env bash
# Installs the sf4e lobby server as a systemd service, opens the UDP ports, and
# starts it. After this the server survives reboots and restarts itself if it
# ever exits -- the job the Windows scheduled task used to do.
#
#   chmod +x install-service-linux.sh && ./install-service-linux.sh
#
# Re-running it is safe: it just refreshes the unit and restarts the service.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
binary="$root/build-linux/LobbyServer"

if [ ! -x "$binary" ]; then
	echo "ERROR: $binary not found. Run deploy/build-linux.sh first." >&2
	exit 1
fi

# Run as a dedicated unprivileged account: the server needs no root, and the
# ports it uses are all above 1024.
if ! id -u sf4e >/dev/null 2>&1; then
	echo "==> Creating service user 'sf4e'"
	sudo useradd --system --no-create-home --shell /usr/sbin/nologin sf4e
fi

echo "==> Installing binary to /opt/sf4e"
sudo mkdir -p /opt/sf4e/lib
sudo cp "$binary" /opt/sf4e/LobbyServer

# Ship the shared libraries too. The binary's RPATH points into the build tree
# under the building user's home, which the unprivileged service account cannot
# read -- systemd then fails the unit with status=127 ("not found"), which looks
# like a missing binary rather than a missing library.
libdir="$(dirname "$binary")/vcpkg_installed/x64-linux/lib"
if compgen -G "$libdir"/*.so* >/dev/null; then
	sudo cp -a "$libdir"/*.so* /opt/sf4e/lib/
fi

sudo chown -R sf4e:sf4e /opt/sf4e
sudo chmod 755 /opt/sf4e/LobbyServer

echo "==> Writing /etc/systemd/system/sf4e-lobby.service"
sudo tee /etc/systemd/system/sf4e-lobby.service >/dev/null <<'UNIT'
[Unit]
Description=sf4e lobby server (matchmaker + GGPO relay)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=sf4e
# The shared libraries were copied next to the binary, because the RPATH baked
# in at build time points into the builder's home directory.
Environment=LD_LIBRARY_PATH=/opt/sf4e/lib
ExecStart=/opt/sf4e/LobbyServer
# Always come back, no matter how it died, and never give up: a rate limit here
# would eventually leave players with no server at all.
Restart=always
RestartSec=5
StartLimitIntervalSec=0
# The server logs to stdout; journald keeps it. Read with:
#   journalctl -u sf4e-lobby -f
StandardOutput=journal
StandardError=journal
# It only needs a socket and its own binary.
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true

[Install]
WantedBy=multi-user.target
UNIT

echo "==> Opening UDP ports"
if command -v ufw >/dev/null 2>&1; then
	sudo ufw allow 23400:23420/udp || true
	sudo ufw allow 24001:24020/udp || true
	sudo ufw allow 25001:25080/udp || true
else
	echo "    ufw not installed - open these UDP ranges in your provider's firewall:"
	echo "    23400-23420, 24001-24020, 25001-25080"
fi

echo "==> Enabling and starting the service"
sudo systemctl daemon-reload
sudo systemctl enable sf4e-lobby
sudo systemctl restart sf4e-lobby
sleep 2
sudo systemctl --no-pager --full status sf4e-lobby || true

echo
echo "==> Live logs:     journalctl -u sf4e-lobby -f"
echo "==> Restart:       sudo systemctl restart sf4e-lobby"
echo "==> Remember to point the launcher at this machine's public IP."
