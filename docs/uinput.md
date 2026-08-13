# UInput Setup

whisper-echo can emulate keyboard/mouse via Linux uinput. This is a privileged operation.

**Do not install udev rules automatically.** The package ships `share/whisper-echo/setup_uinput.sh` as documentation / helper.

To enable uinput manually:

1. Review `share/whisper-echo/setup_uinput.sh`
2. If you accept the security implications, run it as root:
   `sudo share/whisper-echo/setup_uinput.sh`
3. Add your user to the `uinput` group and log out/in.

Security notes:
* uinput allows creating virtual input devices
* An attacker with access can inject keystrokes
* Only enable on trusted systems
* Consider using a dedicated unprivileged user for the service
