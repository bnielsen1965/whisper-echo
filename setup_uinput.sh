#!/usr/bin/env bash
set -euo pipefail

# whisper-echo uinput setup helper
# Adds uinput group and udev rule for non-root access.

GROUP="uinput"
RULE_FILE="/etc/udev/rules.d/99-whisper-echo-uinput.rules"
RULE_CONTENT='KERNEL=="uinput", GROUP="input", MODE="0660"'

if getent group "$GROUP" >/dev/null; then
  echo "Group $GROUP already exists"
else
  sudo groupadd "$GROUP"
fi

if [ ! -f "$RULE_FILE" ]; then
  echo "$RULE_CONTENT" | sudo tee "$RULE_FILE" >/dev/null
  sudo udevadm control --reload-rules
  sudo udevadm trigger
fi

echo "Add your user to $GROUP to use whisper-echo without sudo:"
echo "  sudo usermod -aG $GROUP \$USER"
echo "Then log out/in."
