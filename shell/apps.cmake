# Every app in this repository: id, name, icon, colour, factory, source.
# BASE apps are part of ZeroOS itself. The others are installable apps
# (docs/APPS.md): on the device each one is its own module and package,
# with its extras listed in image/apps/<id>.pkg. The home screen lists apps
# in the order below, whichever of them are on the device.
zs_app(terminal "Terminal"  ZS_ICON_TERMINAL     0x3a3a3a make_terminal_app SOURCES src/apps/terminal.cpp BASE)
zs_app(remote   "Remote"    ZS_ICON_NETWORK      0x0e8a9e make_remote_app   SOURCES src/apps/remote.cpp)
zs_app(sync     "Sync"      ZS_ICON_SYNC         0x7764d8 make_sync_app     SOURCES src/apps/sync.cpp)
zs_app(files    "Files"     LV_SYMBOL_DIRECTORY  0xe95420 make_files_app    SOURCES src/apps/files.cpp BASE)
zs_app(notes    "Notes"     ZS_ICON_NOTE         0xe0a100 make_notes_app    SOURCES src/apps/notes.cpp)
zs_app(media    "Media"     LV_SYMBOL_AUDIO      0xd6336c make_media_app    SOURCES src/apps/media.cpp)
zs_app(camera   "Camera"    ZS_ICON_CAMERA       0x2f6f8f make_camera_app   SOURCES src/apps/camera.cpp)
zs_app(car      "Car"       ZS_ICON_CAR          0x1e88a8 make_car_app      SOURCES src/apps/car.cpp)
zs_app(wifi     "Wi-Fi"     LV_SYMBOL_WIFI       0x0073e5 make_wifi_app     SOURCES src/apps/wifi.cpp BASE)
zs_app(tether   "Tether"    ZS_ICON_PHONE        0x26a269 make_tether_app   SOURCES src/apps/tether.cpp)
zs_app(ir       "IR Remote" LV_SYMBOL_POWER      0xc7162b make_ir_app       SOURCES src/apps/ir_remote.cpp)
zs_app(gps      "GPS"       ZS_ICON_PIN          0x0e8420 make_gps_app      SOURCES src/apps/gps.cpp)
zs_app(mesh     "Mesh"      ZS_ICON_CHAT         0x12a182 make_mesh_app     SOURCES src/apps/mesh.cpp)
zs_app(compass  "Compass"   ZS_ICON_COMPASS      0xb04a8f make_compass_app  SOURCES src/apps/compass.cpp)
zs_app(battery  "Battery"   LV_SYMBOL_BATTERY_3  0x3aa655 make_battery_app  SOURCES src/apps/battery.cpp)
zs_app(clock    "Clock"     ZS_ICON_CLOCK        0x4a5bd6 make_clock_app    SOURCES src/apps/clock.cpp)
zs_app(dock     "Dock"      ZS_ICON_DESKTOP      0x1c71d8 make_dock_app     SOURCES src/apps/dock.cpp BASE)
zs_app(sysmon   "System"    ZS_ICON_GAUGE        0x5e6c7b make_sysmon_app   SOURCES src/apps/sysmon.cpp)
zs_app(settings "Settings"  LV_SYMBOL_SETTINGS   0x77767b make_settings_app SOURCES src/apps/settings.cpp BASE)
zs_app(update   "Update"    LV_SYMBOL_DOWNLOAD   0xc66a00 make_update_app   SOURCES src/apps/update.cpp BASE)
zs_app(apps     "Apps"      ZS_ICON_GRID         0x9141ac make_store_app    SOURCES src/apps/store.cpp BASE)
