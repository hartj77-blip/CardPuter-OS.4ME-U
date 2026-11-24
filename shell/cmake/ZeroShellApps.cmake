# Apps for ZeroShell: built into the binary, or built as installable app
# modules (docs/APPS.md).
#
#   include(path/to/shell/cmake/ZeroShellApps.cmake)
#   zs_app(<id> <name> <icon> <color> <make-function>
#          SOURCES <file.cpp>... [LIBS <lib>...] [BASE] [ORDER <n>] [MODULE <name>])
#   ...
#   zs_apps_build(<zeroshell-target> <shell-src-dir>)
#   zs_link_lvgl(<zeroshell-target>)
#
# <icon> is a C expression (LV_SYMBOL_AUDIO, ZS_ICON_CAR, "\xEF\x80\x81").
# <make-function> is the app's factory in namespace zs. BASE apps are always
# compiled into ZeroShell. With ZS_APPS_AS_MODULES on, every other app
# becomes its own <id>.so in <build>/apps/; with it off (the simulator,
# screenshots, the browser build) every app is compiled in.
# Apps are listed on the home screen in the order zs_app() was called,
# unless ORDER says otherwise (this repository's apps use 10, 20, 30...).
# MODULE puts several apps in one module (<name>.so; default: the app's id):
# apps that share state (one radio connection) must share a module.
# SOURCES are relative to the file that calls zs_app(). LIBS are linked into
# the app (static libraries must be built with -fPIC for a module).

set(ZS_SHELL_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(zs_app id name icon color make)
  cmake_parse_arguments(A "BASE" "ORDER;MODULE" "SOURCES;LIBS" ${ARGN})
  if(NOT A_MODULE)
    set(A_MODULE "${id}")
  endif()
  if(NOT id MATCHES "^[a-z0-9][a-z0-9-]*$")
    message(FATAL_ERROR "zs_app: id '${id}' must be lower-case letters, digits and -")
  endif()
  if(NOT A_SOURCES)
    message(FATAL_ERROR "zs_app(${id}): no SOURCES")
  endif()
  get_property(count GLOBAL PROPERTY ZS_APP_COUNT)
  if(NOT count)
    set(count 0)
  endif()
  math(EXPR count "${count} + 1")
  set_property(GLOBAL PROPERTY ZS_APP_COUNT ${count})
  if(NOT A_ORDER)
    math(EXPR A_ORDER "${count} * 10")
  endif()
  set(srcs "")
  foreach(s ${A_SOURCES})
    get_filename_component(abs "${s}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
    list(APPEND srcs "${abs}")
  endforeach()
  set_property(GLOBAL APPEND PROPERTY ZS_APP_IDS ${id})
  set_property(GLOBAL PROPERTY ZS_APP_${id}_NAME "${name}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_ICON "${icon}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_COLOR "${color}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_MAKE "${make}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_ORDER "${A_ORDER}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_BASE "${A_BASE}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_SOURCES "${srcs}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_LIBS "${A_LIBS}")
  set_property(GLOBAL PROPERTY ZS_APP_${id}_MODULE "${A_MODULE}")
endfunction()

# Link LVGL into ZeroShell. With app modules, all of it: a module may use a
# part of LVGL that ZeroShell itself doesn't, and finds it in the binary.
function(zs_link_lvgl target)
  if(ZS_APPS_AS_MODULES)
    target_link_libraries(${target} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,lvgl>")
  else()
    target_link_libraries(${target} PRIVATE lvgl)
  endif()
endfunction()

# With ZS_BACKEND=web, build <target> as the browser simulator: one
# self-contained zeroshell.js that web/template.html loads (web/build.sh).
# The apps repository calls this too, to build the simulator with every app.
function(zs_web_link target)
  if(NOT ZS_BACKEND STREQUAL "web")
    return()
  endif()
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "ZS_BACKEND=web needs Emscripten: emcmake cmake ...")
  endif()
  set_target_properties(${target} PROPERTIES SUFFIX ".js")
  target_link_options(${target} PRIVATE
    -sMODULARIZE=1 -sEXPORT_NAME=createZeroShell -sENVIRONMENT=web
    -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0
    -sEXPORTED_FUNCTIONS=_main,_zs_web_evdev,_zs_web_power,_zs_web_frame,_zs_web_take_dirty,_zs_web_hdmi,_zs_web_pose,_zs_web_app_count,_zs_web_app_id,_zs_web_app_name,_zs_web_current_app
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString)
endfunction()

# Build every app registered with zs_app(): into <target>, or as modules.
# <shell-src> is the base OS's shell/src (for the headers).
function(zs_apps_build target shell_src)
  get_property(ids GLOBAL PROPERTY ZS_APP_IDS)
  set(gen "${CMAKE_BINARY_DIR}/zs_generated")
  file(MAKE_DIRECTORY "${gen}")
  set(decls "")
  set(entries "")
  set(modules "")
  foreach(id ${ids})
    get_property(name GLOBAL PROPERTY ZS_APP_${id}_NAME)
    get_property(icon GLOBAL PROPERTY ZS_APP_${id}_ICON)
    get_property(color GLOBAL PROPERTY ZS_APP_${id}_COLOR)
    get_property(make GLOBAL PROPERTY ZS_APP_${id}_MAKE)
    get_property(order GLOBAL PROPERTY ZS_APP_${id}_ORDER)
    get_property(base GLOBAL PROPERTY ZS_APP_${id}_BASE)
    get_property(srcs GLOBAL PROPERTY ZS_APP_${id}_SOURCES)
    get_property(libs GLOBAL PROPERTY ZS_APP_${id}_LIBS)
    if(base OR NOT ZS_APPS_AS_MODULES)
      target_sources(${target} PRIVATE ${srcs})
      if(libs)
        target_link_libraries(${target} PRIVATE ${libs})
      endif()
      string(APPEND decls "std::unique_ptr<App> ${make}();\n")
      if(base)
        set(is_base true)
      else()
        set(is_base false)
      endif()
      string(APPEND entries "    {\n        AppInfo a{\"${id}\", \"${name}\", ${icon}, ${color}, ${make}};\n"
                            "        a.order = ${order};\n        a.base = ${is_base};\n"
                            "        out.push_back(std::move(a));\n    }\n")
    else()
      get_property(mod GLOBAL PROPERTY ZS_APP_${id}_MODULE)
      if(NOT mod IN_LIST modules)
        list(APPEND modules ${mod})
        set(mod_${mod}_decls "")
        set(mod_${mod}_entries "")
        set(mod_${mod}_srcs "")
        set(mod_${mod}_libs "")
      endif()
      string(APPEND mod_${mod}_decls "std::unique_ptr<App> ${make}();\n")
      string(APPEND mod_${mod}_entries "        {ZS_APP_ABI, \"${id}\", \"${name}\", ${icon}, ${color}, ${order}, zs::${make}},\n")
      list(APPEND mod_${mod}_srcs ${srcs})
      list(APPEND mod_${mod}_libs ${libs})
    endif()
  endforeach()
  set(targets "")
  foreach(mod ${modules})
    set(ZS_MODULE "${mod}")
    set(ZS_DECLS "${mod_${mod}_decls}")
    set(ZS_ENTRIES "${mod_${mod}_entries}")
    configure_file("${ZS_SHELL_CMAKE_DIR}/app_module.cpp.in" "${gen}/module_${mod}.cpp" @ONLY)
    list(REMOVE_DUPLICATES mod_${mod}_srcs)
    add_library(zsapp_${mod} MODULE "${gen}/module_${mod}.cpp" ${mod_${mod}_srcs})
    set_target_properties(zsapp_${mod} PROPERTIES
      PREFIX "" OUTPUT_NAME "${mod}"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps")
    # Everything the module uses (LVGL, the hardware layer, shell services)
    # comes from the zeroshell binary at load time: compile against the
    # same headers and settings, link nothing else.
    target_include_directories(zsapp_${mod} PRIVATE "${shell_src}"
      $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>
      $<TARGET_PROPERTY:lvgl,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(zsapp_${mod} PRIVATE
      $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>
      $<TARGET_PROPERTY:lvgl,INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(zsapp_${mod} PRIVATE $<TARGET_PROPERTY:${target},COMPILE_OPTIONS>)
    if(mod_${mod}_libs)
      list(REMOVE_DUPLICATES mod_${mod}_libs)
      target_link_libraries(zsapp_${mod} PRIVATE ${mod_${mod}_libs})
    endif()
    list(APPEND targets zsapp_${mod})
  endforeach()
  file(CONFIGURE OUTPUT "${gen}/builtin_apps.cpp" CONTENT
"// Generated by ZeroShellApps.cmake: the apps compiled into this binary.
#include \"ui/app.h\"
#include \"ui/theme.h\"

namespace zs {

${decls}
void zs_builtin_apps(std::vector<AppInfo>& out) {
${entries}}

}  // namespace zs
")
  target_sources(${target} PRIVATE "${gen}/builtin_apps.cpp")
  target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
  if(ZS_APPS_AS_MODULES)
    # Modules find the shell's own functions in the binary: export them.
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS ON)
  endif()
  set(ZS_APP_MODULES "${targets}" PARENT_SCOPE)
endfunction()
