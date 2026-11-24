cmake_minimum_required(VERSION 3.24)
project(zeroshell VERSION 0.1.0 LANGUAGES C CXX)

# Backends:
#   sim      SDL2 window on a desktop PC (development)
#   device   Linux framebuffer + evdev keyboard (Cardputer Zero)
#   headless no window; renders screens to image files (CI screenshots, tests)
#   web      WebAssembly for a browser demo (build with emcmake; see web/)
set(ZS_BACKEND "sim" CACHE STRING "sim | device | headless | web")
set_property(CACHE ZS_BACKEND PROPERTY STRINGS sim device headless web)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# LVGL is pinned. Set FETCHCONTENT_SOURCE_DIR_LVGL to use a local checkout.
include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.2.2
  GIT_SHALLOW TRUE)

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

# quirc reads QR codes for the Camera app (ISC licence). It has no CMake build
# of its own, so fetch the source only and compile its four files here.
FetchContent_Declare(quirc
  GIT_REPOSITORY https://github.com/dlbeer/quirc.git
  GIT_TAG v1.2
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR none)
FetchContent_MakeAvailable(quirc)
add_library(quirc STATIC
  ${quirc_SOURCE_DIR}/lib/decode.c ${quirc_SOURCE_DIR}/lib/identify.c
  ${quirc_SOURCE_DIR}/lib/quirc.c ${quirc_SOURCE_DIR}/lib/version_db.c)
target_include_directories(quirc PUBLIC ${quirc_SOURCE_DIR}/lib)

set(ZS_SDL 0)
set(ZS_DEVICE 0)
if(ZS_BACKEND STREQUAL "sim")
  set(ZS_SDL 1)
elseif(ZS_BACKEND STREQUAL "device")
  set(ZS_DEVICE 1)
endif()
target_compile_definitions(lvgl PUBLIC
  ZS_BACKEND_SDL=${ZS_SDL} ZS_BACKEND_DEVICE=${ZS_DEVICE})

if(ZS_SDL)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

# Apps: the base OS's own are compiled in; the rest become installable app
# modules on the device (docs/APPS.md). The simulator, screenshots and the
# browser build compile every app in.
if(ZS_BACKEND STREQUAL "device")
  set(_zs_modules_default ON)
else()
  set(_zs_modules_default OFF)
endif()
option(ZS_APPS_AS_MODULES "Build the non-base apps as installable modules" ${_zs_modules_default})
include(cmake/ZeroShellApps.cmake)
include(apps.cmake)

# The shell itself: everything in src/ except the apps listed in apps.cmake.
file(GLOB_RECURSE ZS_SOURCES CONFIGURE_DEPENDS src/*.cpp src/fonts/*.c)
get_property(_zs_ids GLOBAL PROPERTY ZS_APP_IDS)
foreach(_id ${_zs_ids})
  get_property(_srcs GLOBAL PROPERTY ZS_APP_${_id}_SOURCES)
  list(REMOVE_ITEM ZS_SOURCES ${_srcs})
endforeach()
add_executable(zeroshell ${ZS_SOURCES})
target_include_directories(zeroshell PRIVATE src)
target_compile_definitions(zeroshell PRIVATE
  ZS_VERSION="${PROJECT_VERSION}"
  ZS_BACKEND_NAME="${ZS_BACKEND}")
target_compile_options(zeroshell PRIVATE -Wall -Wextra -Wno-unused-parameter)
zs_link_lvgl(zeroshell)
target_link_libraries(zeroshell PRIVATE quirc pthread m)
zs_apps_build(zeroshell ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(ZS_BACKEND STREQUAL "web")
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "ZS_BACKEND=web needs Emscripten: emcmake cmake ...")
  endif()
  set_target_properties(zeroshell PROPERTIES SUFFIX ".js")
  target_link_options(zeroshell PRIVATE
    -sMODULARIZE=1 -sEXPORT_NAME=createZeroShell -sENVIRONMENT=web
    -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0
    -sEXPORTED_FUNCTIONS=_main,_zs_web_evdev,_zs_web_power,_zs_web_frame,_zs_web_take_dirty,_zs_web_hdmi,_zs_web_pose,_zs_web_app_count,_zs_web_app_id,_zs_web_app_name,_zs_web_current_app
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString)
endif()

install(TARGETS zeroshell RUNTIME DESTINATION bin)
if(ZS_APP_MODULES)
  install(TARGETS ${ZS_APP_MODULES} LIBRARY DESTINATION lib/zeroos/apps)
endif()

# Unit tests for code that needs no screen (IR codes, the Jellyfin client): ctest, or run ir_test.
if(NOT CMAKE_CROSSCOMPILING AND NOT ZS_BACKEND STREQUAL "web")
  enable_testing()
  add_executable(ir_test tests/ir_test.cpp src/ir/ir.cpp src/ir/db.cpp)
  target_include_directories(ir_test PRIVATE src)
  target_compile_options(ir_test PRIVATE -Wall -Wextra)
  add_test(NAME ir_test COMMAND ir_test)
  add_executable(jellyfin_test tests/jellyfin_test.cpp src/hal/jellyfin.cpp src/platform/watchdog.cpp)
  target_include_directories(jellyfin_test PRIVATE src)
  target_compile_options(jellyfin_test PRIVATE -Wall -Wextra)
  add_test(NAME jellyfin_test COMMAND jellyfin_test)
  # The AI app's requests and replies (no network).
  add_executable(ai_test tests/ai_test.cpp src/apps/ai/chat.cpp)
  target_include_directories(ai_test PRIVATE src)
  target_compile_options(ai_test PRIVATE -Wall -Wextra)
  add_test(NAME ai_test COMMAND ai_test)
  # Launcher entries of apps made for M5Stack's launcher.
  add_executable(desktop_entry_test tests/desktop_entry_test.cpp src/ui/desktop_entry.cpp)
  target_include_directories(desktop_entry_test PRIVATE src)
  target_compile_options(desktop_entry_test PRIVATE -Wall -Wextra)
  add_test(NAME desktop_entry_test COMMAND desktop_entry_test)
  # What typed text finds: Home search and list filters.
  add_executable(search_match_test tests/search_match_test.cpp src/ui/search_match.cpp)
  target_include_directories(search_match_test PRIVATE src)
  target_compile_options(search_match_test PRIVATE -Wall -Wextra)
  add_test(NAME search_match_test COMMAND search_match_test)
  # Settings > Running programs: listing and stopping the user's programs.
  add_executable(proc_test tests/proc_test.cpp src/hal/proc_stats.cpp)
  target_include_directories(proc_test PRIVATE src)
  target_compile_options(proc_test PRIVATE -Wall -Wextra)
  add_test(NAME proc_test COMMAND proc_test)

  # Word suggestions: the word being typed, what is offered, what is learned.
  add_executable(suggest_test tests/suggest_test.cpp src/ui/suggest.cpp src/ui/suggest_words.cpp src/ui/fileio.cpp)
  target_include_directories(suggest_test PRIVATE src)
  target_compile_options(suggest_test PRIVATE -Wall -Wextra)
  add_test(NAME suggest_test COMMAND suggest_test)
  # A test-pattern libretro core for the Games app (tests/games/testcore.c):
  # ZS_GAMES_CORES=<build>/test-cores lets it play ~/Games/test/*.zstest.
  add_library(zstest_libretro MODULE tests/games/testcore.c)
  set_target_properties(zstest_libretro PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
                        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-cores")
  target_link_libraries(zstest_libretro PRIVATE m)
endif()
