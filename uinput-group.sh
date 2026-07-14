#!/bin/bash

USERNAME="${1:?Usage: $0 <username>}"

# Must be run as root
if [[ $EUID -ne 0 ]]; then
    echo "Error: This script must be run as root." >&2
    exit 1
fi

groupadd uinput
usermod -a -G uinput "$USERNAME"
udevadm control --reload-rules
echo "SUBSYSTEM==\"misc\", KERNEL==\"uinput\", GROUP=\"uinput\", MODE=\"0660\"" | tee /etc/udev/rules.d/uinput.rules
echo uinput | tee /etc/modules-load.d/uinput.conf
