# Apps for ZeroShell: built into the binary, or built as installable app
# modules (docs/APPS.md).
#
#   include(path/to/shell/cmake/ZeroShellApps.cmake)
#   zs_app(<id> <name> <icon> <color> <make-function>
#          SOURCES <file.cpp>... [BASE] [ORDER <n>])
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
# unless ORDER says otherwise (base apps use 10, 20, 30...; others use 1000+).

set(ZS_SHELL_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(zs_app id name icon color make)
  cmake_parse_arguments(A "BASE" "ORDER" "SOURCES" ${ARGN})
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
    get_filename_component(abs "${s}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
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
    if(base OR NOT ZS_APPS_AS_MODULES)
      target_sources(${target} PRIVATE ${srcs})
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
      set(ZS_ID "${id}")
      set(ZS_NAME "${name}")
      set(ZS_ICON "${icon}")
      set(ZS_COLOR "${color}")
      set(ZS_MAKE "${make}")
      set(ZS_ORDER "${order}")
      configure_file("${ZS_SHELL_CMAKE_DIR}/app_module.cpp.in" "${gen}/module_${id}.cpp" @ONLY)
      add_library(zsapp_${id} MODULE "${gen}/module_${id}.cpp" ${srcs})
      set_target_properties(zsapp_${id} PROPERTIES
        PREFIX "" OUTPUT_NAME "${id}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps")
      # Everything the module uses (LVGL, the hardware layer, shell services)
      # comes from the zeroshell binary at load time: compile against the
      # same headers and settings, link nothing.
      target_include_directories(zsapp_${id} PRIVATE "${shell_src}"
        $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:lvgl,INTERFACE_INCLUDE_DIRECTORIES>)
      target_compile_definitions(zsapp_${id} PRIVATE
        $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:lvgl,INTERFACE_COMPILE_DEFINITIONS>)
      target_compile_options(zsapp_${id} PRIVATE $<TARGET_PROPERTY:${target},COMPILE_OPTIONS>)
      list(APPEND modules zsapp_${id})
    endif()
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
  set(ZS_APP_MODULES "${modules}" PARENT_SCOPE)
endfunction()
