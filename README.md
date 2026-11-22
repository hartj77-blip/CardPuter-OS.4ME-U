ZeroOS for the Cardputer Zero

ZeroOS is a Linux-based OS built for the M5Stack Cardputer Zero. It comes with a keyboard-driven interface, a collection of built-in apps, and the option to install more as needed.

ZeroOS is an unofficial community project. It is not made, endorsed or supported by M5Stack.
Features
Keyboard navigation: Use the arrow keys and Enter to navigate. Press a letter to jump to an app, or Esc to go back.
App management: Seven apps are included by default. More can be installed through the Apps app.
Over-the-air updates: Update ZeroOS directly from the device without re-flashing the SD card.
Power saving: The screen dims after 30 seconds and turns off after a minute when running on battery. Turning the device face down switches the screen off, and picking it up wakes it again.
HDMI desktop: Connect an external monitor to use the desktop environment, which includes a browser, video player, terminal and file manager.
Built-in apps
App	Description
Apps	Browse and install additional apps
Dock	Desktop environment for HDMI displays, with a browser, video player, terminal and file manager
Files	Browse files stored on the device
Settings	Configure Wi-Fi, Bluetooth, brightness, volume and power saving. Also includes restart and shutdown options
Terminal	Linux terminal on the Cardputer's built-in display
Update	Download and install ZeroOS updates over the network
Wi-Fi	Scan for and connect to wireless networks
Installable apps

These apps can be installed from the Apps app.

App	Description
IR Remote	Control TVs, projectors, audio equipment, air conditioners and lights. Includes around 2,000 remote profiles and the ability to learn buttons from your own remotes
Media	Music player, Jellyfin streaming and voice recordings
Notes	Simple text notes
Remote	Tailscale support for connecting to other devices, SSH, remote desktop and file sharing
Sync	Sync notes, documents and recordings with your PC or phone using Syncthing
Tether	Connect to the internet through your phone using Wi-Fi, USB or Bluetooth
Camera	Take photos, browse the gallery and scan QR codes, including Wi-Fi network QR codes (requires a camera-equipped model)
GPS	View your position, save locations and record tracks (requires a GPS module)
Mesh	Send text messages over LoRa using Meshtastic (requires the Cap LoRa-1262)
Car	Play music in your car using USB storage or Bluetooth
Compass	Compass and spirit level
Battery	Battery estimates, power usage and power profiles
Clock	Clock and stopwatch
System	CPU and memory usage

Additional radio apps are available in cardputer-zero-rf.

Installation
Download the latest image: zeroos-cardputerzero-arm64.img.xz. The download is around 1.5 GB. Checksums are available on the releases page.
Flash the image to a microSD card using Raspberry Pi Imager (select Choose OS > Use custom) or balenaEtcher. Both tools can write the .xz file directly, so there's no need to extract it.
Insert the card into your Cardputer Zero and turn it on. The first boot can take a few minutes.
First login

The default login is:

Username: pi
Password: raspberry

On your first login through the Terminal app or SSH (ssh pi@cardputer.local), you'll be prompted to change the default password.

If you configured an SSH key using Raspberry Pi Imager, you can log in without using the default password prompt and change your password later with passwd.

Controls

The home screen uses the following keyboard shortcuts:

Key	Action
Arrow keys	Navigate
Enter	Open selected item
Esc or `	Go back
A letter	Jump to an app on the home screen
1 to 5	Open dock apps from the home screen

Individual apps may have additional shortcuts, which are displayed on screen. For example, in Media, Space pauses playback and N skips to the next track. In IR Remote, L starts learning a button from an existing remote.

Use the Apps app to install more apps and Update to install newer versions of ZeroOS.

Documentation
Apps – Installing apps and creating your own
Updates – Updating ZeroOS over the network
IR Remote – Using the universal remote
Remote – Tailscale, SSH and file sharing
Sync – Syncing notes and files
Tether – Connecting through your phone
Media and Jellyfin – Playing music from a Jellyfin server
Car – Using ZeroOS for music in the car
Camera – Photos and QR codes
Dock – Using the desktop on an external monitor
GPS and LoRa – Expansion modules
Battery and performance – Power management and performance

Developer documentation:

Architecture
Hardware notes
Roadmap
Running on a PC

You can run the same home screen and apps in a window on Linux using the SDL simulator.

Install the dependencies:

sh
sudo apt install cmake g++ libsdl2-dev


Build and run:

sh
cmake -S shell -B build/sim -DZS_BACKEND=sim
cmake --build build/sim -j
./build/sim/zeroshell


Use --zoom 4 to increase the window size.

There's also a browser-based WebAssembly version. Building it requires Emscripten:

sh
web/build.sh build/zeroos-simulator.html

Building from source

Install the cross-compiler:

sh
sudo apt install g++-aarch64-linux-gnu


Build the device version:

sh
cmake -S shell -B build/device -DZS_BACKEND=device -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/aarch64-linux-gnu.cmake
cmake --build build/device -j


Build the apps and SD card image:

sh
image/apps/build.sh build/device/apps
image/build.sh build/device/zeroshell


Building the SD card image requires Docker and typically takes 1–2 hours.

You can also build an image using the Build image workflow in the GitHub Actions tab.

To push a build to a device that's already running ZeroOS:

sh
tools/push-update.sh cardputer.local


See docs/UPDATE.md for details.

Security

To report a security issue, see SECURITY.md.

License

ZeroOS is licensed under the MIT license.

It uses several other open-source projects and resources:

LVGL – MIT
quirc – ISC
Flipper-IRDB – CC0
M5Stack pi-gen fork – BSD-3
M5Stack board drivers

Cardputer and M5Stack are trademarks of M5Stack and are used here only to identify the supported hardware.
