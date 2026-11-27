# Every app in this repository: id, name, icon, colour, factory, source.
# BASE apps are part of ZeroOS itself. The others are installable apps
# (docs/APPS.md): on the device each one is its own module and package,
# with its extras listed in image/apps/<id>.pkg. The home screen lists apps
# in the order below, whichever of them are on the device.
zs_app(terminal "Terminal"  ZS_ICON_TERMINAL     0x3a3a3a make_terminal_app SOURCES src/apps/terminal.cpp)
zs_app(remote   "Remote"    ZS_ICON_NETWORK      0x0e8a9e make_remote_app   SOURCES src/apps/remote.cpp)
zs_app(sync     "Sync"      ZS_ICON_SYNC         0x7764d8 make_sync_app     SOURCES src/apps/sync.cpp)
zs_app(files    "Files"     LV_SYMBOL_DIRECTORY  0xe95420 make_files_app    SOURCES src/apps/files.cpp)
zs_app(notes    "Notes"     ZS_ICON_NOTE         0xe0a100 make_notes_app    SOURCES src/apps/notes.cpp)
zs_app(ai    "AI"     ZS_ICON_CHIP         0xd97757 make_ai_app    SOURCES src/apps/ai.cpp src/apps/ai/chat.cpp)
zs_app(media    "Media"     LV_SYMBOL_AUDIO      0xd6336c make_media_app    SOURCES src/apps/media.cpp)
zs_app(camera   "Camera"    ZS_ICON_CAMERA       0x2f6f8f make_camera_app   SOURCES src/apps/camera.cpp)
zs_app(car      "Car"       ZS_ICON_CAR          0x1e88a8 make_car_app      SOURCES src/apps/car.cpp)
zs_app(tether   "Tether"    ZS_ICON_PHONE        0x26a269 make_tether_app   SOURCES src/apps/tether.cpp)
zs_app(ir       "IR Remote" LV_SYMBOL_POWER      0xc7162b make_ir_app       SOURCES src/apps/ir_remote.cpp)
zs_app(gps      "GPS"       ZS_ICON_PIN          0x0e8420 make_gps_app      SOURCES src/apps/gps.cpp)
zs_app(mesh     "Mesh"      ZS_ICON_CHAT         0x12a182 make_mesh_app     SOURCES src/apps/mesh.cpp)
zs_app(compass  "Compass"   ZS_ICON_COMPASS      0xb04a8f make_compass_app  SOURCES src/apps/compass.cpp)
zs_app(clock    "Clock"     ZS_ICON_CLOCK        0x4a5bd6 make_clock_app    SOURCES src/apps/clock.cpp)
zs_app(dock     "Dock"      ZS_ICON_DESKTOP      0x1c71d8 make_dock_app     SOURCES src/apps/dock.cpp)
zs_app(sysmon   "System"    ZS_ICON_GAUGE        0x5e6c7b make_sysmon_app   SOURCES src/apps/sysmon.cpp)
# Wi-Fi, Bluetooth and Battery are sections of Settings, not apps. The three
# base apps keep the places they had when those were listed here (ORDER), so
# they stay where they were among the installed apps on the home screen.
zs_app(settings "Settings"  LV_SYMBOL_SETTINGS   0x77767b make_settings_app BASE ORDER 210
       SOURCES src/apps/settings.cpp src/apps/wifi.cpp src/apps/bluetooth.cpp src/apps/datetime.cpp)
zs_app(update   "Update"    LV_SYMBOL_DOWNLOAD   0xc66a00 make_update_app   BASE ORDER 220
       SOURCES src/apps/update.cpp)
zs_app(apps     "Apps"      ZS_ICON_GRID         0x9141ac make_store_app    BASE ORDER 230
       SOURCES src/apps/store.cpp)

# Dev Tools: one installable pack (module "dev", docs/DEV.md). Write, run and
# ship code from the keyboard: projects, REPLs, Git and a serial monitor.
# The shared plumbing (apps/kit, apps/dev/dev.cpp) is listed once, on the
# first app. Order 300..330: after the base apps and Net Tools.
zs_app(code     "Code"      LV_SYMBOL_EDIT     0x2a7bde make_code_app     MODULE dev ORDER 300
       SOURCES src/apps/dev/code.cpp src/apps/dev/dev.cpp
               src/apps/kit/run.cpp src/apps/kit/ui.cpp)
zs_app(repl     "REPL"      LV_SYMBOL_KEYBOARD 0x3a8f5c make_repl_app     MODULE dev ORDER 310
       SOURCES src/apps/dev/repl.cpp)
zs_app(git      "Git"       LV_SYMBOL_SHUFFLE  0xf05033 make_git_app      MODULE dev ORDER 320
       SOURCES src/apps/dev/git.cpp)
zs_app(serial   "Serial"    LV_SYMBOL_USB      0x8a5cd6 make_serial_app   MODULE dev ORDER 330
       SOURCES src/apps/dev/serial.cpp)

# Libretro: one installable app (module "games", docs/GAMES.md) that plays games
# for classic consoles with libretro cores, on the built-in screen.
zs_app(games    "Libretro"  LV_SYMBOL_PLAY     0xd9480f make_games_app    ORDER 400
       SOURCES src/apps/games/games.cpp src/apps/games/emu.cpp src/apps/games/pad.cpp
               src/apps/games/systems.cpp src/apps/kit/run.cpp src/apps/kit/ui.cpp)
