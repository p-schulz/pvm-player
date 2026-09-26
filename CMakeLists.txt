cmake_minimum_required(VERSION 3.20)
if(APPLE)
  # OBJC (not just OBJCXX) has to be enabled too, or CMake has no correctly-
  # configured compiler for GLFW's own vendored cocoa_*.m (plain Objective-
  # C, not Objective-C++) sources -- without it they got compiled with C++
  # strictness applied (e.g. rejecting the implicit void*-to-T* casts that
  # plain C/Objective-C allow), which doesn't build.
  project(pvm_player LANGUAGES C CXX OBJC OBJCXX)
else()
  project(pvm_player LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# GLFW: fetched from source so the same version builds identically on the
# eventual Windows target and this dev machine.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# glad: pre-generated OpenGL 3.3 core loader, vendored under thirdparty/glad
# (generated once via the glad2 CLI, not regenerated at configure time -- no
# python/network dependency needed to build).
add_library(glad STATIC thirdparty/glad/src/gl.c)
target_include_directories(glad PUBLIC thirdparty/glad/include)

# Dear ImGui: vendored source, core + GLFW/OpenGL3 backends.
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/thirdparty/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# libmpv: prefer a vendored Windows SDK under thirdparty/libmpv if present
# (see thirdparty/libmpv/README.md); otherwise fall back to a system install
# (e.g. Homebrew's `mpv` formula on macOS dev machines) so the prototype can
# be built and verified without the Windows SDK in hand.
if(EXISTS ${CMAKE_SOURCE_DIR}/thirdparty/libmpv/include/mpv/client.h)
  add_library(mpv INTERFACE)
  target_include_directories(mpv INTERFACE ${CMAKE_SOURCE_DIR}/thirdparty/libmpv/include)
  if(WIN32)
    target_link_libraries(mpv INTERFACE ${CMAKE_SOURCE_DIR}/thirdparty/libmpv/libmpv.dll.a)
  else()
    target_link_libraries(mpv INTERFACE ${CMAKE_SOURCE_DIR}/thirdparty/libmpv/libmpv.dylib)
  endif()
  message(STATUS "Using vendored libmpv from thirdparty/libmpv")
else()
  find_path(MPV_INCLUDE_DIR mpv/client.h)
  find_library(MPV_LIBRARY mpv)
  if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
    message(FATAL_ERROR
      "libmpv not found. Either vendor a prebuilt SDK under thirdparty/libmpv/ "
      "(see PLAN.md) or install it system-wide (e.g. `brew install mpv` on "
      "macOS) for local development.")
  endif()
  add_library(mpv INTERFACE)
  target_include_directories(mpv INTERFACE ${MPV_INCLUDE_DIR})
  target_link_libraries(mpv INTERFACE ${MPV_LIBRARY})
  message(STATUS "Using system libmpv: ${MPV_LIBRARY}")
endif()

# libcurl (HTTP fetch) and pugixml (RSS/Atom parsing) for the NEWS section.
# curl ships with macOS's SDK and is an apt package on the Pi; pugixml is
# `brew install pugixml` / `apt install libpugixml-dev`, falling back to a
# source build if no system copy is found.
find_package(CURL REQUIRED)
find_package(pugixml QUIET)
if(NOT pugixml_FOUND)
  message(STATUS "pugixml not found on the system; fetching it")
  FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(pugixml)
endif()

# nlohmann/json: vendored single header (thirdparty/json/nlohmann/json.hpp)
# for the ARD section's MediathekViewWeb API responses -- unlike RSS's
# simple tag-stripping (html_strip.cpp), that JSON is nested/escaped enough
# to warrant a real, tested parser rather than a hand-rolled one.
add_library(json INTERFACE)
target_include_directories(json INTERFACE ${CMAKE_SOURCE_DIR}/thirdparty/json)

# Everything teletext that needs neither ImGui nor a window: page model,
# navigation state, HTTP fetch, feed parsing. Kept as its own library so the
# command-line probe and self-test can use it without the GUI stack.
add_library(teletext_core STATIC
  src/teletext/page.cpp
  src/teletext/navigator.cpp
  src/teletext/html_strip.cpp
  src/teletext/http_fetch.cpp
  src/teletext/feed_parser.cpp
  src/teletext/article_pages.cpp
  src/teletext/title_art.cpp
  src/teletext/news_config.cpp
  src/teletext/news_pages.cpp
  src/teletext/mvw_api.cpp
  src/teletext/mvw_config.cpp
  src/teletext/mvw_pages.cpp
  src/teletext/mvw_service.cpp
  src/teletext/news_service.cpp
)
target_include_directories(teletext_core PUBLIC src)
target_link_libraries(teletext_core PUBLIC CURL::libcurl pugixml::pugixml json)

# Phase 2 verification tool: fetch + parse one feed URL and print the result.
add_executable(teletext_probe tools/teletext_probe.cpp)
target_link_libraries(teletext_probe PRIVATE teletext_core)

# Logic tests for the above (HTML stripping, feed parsing, pagination,
# navigation): `ctest --test-dir build` or run ./teletext_selftest directly.
enable_testing()
add_executable(teletext_selftest tests/teletext_selftest.cpp)
target_link_libraries(teletext_selftest PRIVATE teletext_core)
add_test(NAME teletext_selftest COMMAND teletext_selftest)

# Platform-free input vocabulary (actions, key maps) plus the GLFW key
# translation. input_core has no GLFW dependency so the Android build can
# reuse it; input_glfw only needs GLFW's key constants.
add_library(input_core STATIC
  src/input/input_action.cpp
  src/input/keymap.cpp
)
target_include_directories(input_core PUBLIC src)

add_library(input_glfw STATIC src/platform/glfw/keymap_glfw.cpp)
target_link_libraries(input_glfw PUBLIC input_core glfw)

add_executable(input_selftest tests/input_selftest.cpp)
target_link_libraries(input_selftest PRIVATE input_glfw)
add_test(NAME input_selftest COMMAND input_selftest)

# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------
add_executable(pvm_player
  src/main.cpp
  src/app.cpp
  src/mpv_player.cpp
  src/paths.cpp
  src/settings.cpp
  src/shader.cpp
  src/ui/menu.cpp
  src/ui/file_browser.cpp
  src/ui/style.cpp
  src/teletext/teletext_view.cpp
)

target_include_directories(pvm_player PRIVATE src)

target_link_libraries(pvm_player PRIVATE glfw glad mpv imgui teletext_core input_glfw)

if(APPLE)
  # macos_media_keys.mm: hooks the hardware Play/Pause media key via a
  # CGEventTap, since GLFW has no concept of it (see the file's header
  # comment). Objective-C++, hence the OBJCXX language enabled above.
  # macos_sleep_guard.cpp: holds an IOKit display-idle-sleep assertion while
  # media is playing (see its header comment for why mpv's own
  # stop-screensaver doesn't cover this app's render setup). Plain C++, no
  # Objective-C needed.
  target_sources(pvm_player PRIVATE src/macos_media_keys.mm src/macos_sleep_guard.cpp)
  target_link_libraries(pvm_player PRIVATE
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework CoreGraphics"
    "-framework IOKit"
  )
  target_compile_definitions(pvm_player PRIVATE GL_SILENCE_DEPRECATION)
endif()

# Copy shader/asset files next to the built executable so they resolve via
# exeDir() regardless of the process's current working directory.
add_custom_command(TARGET pvm_player POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/src/shaders
          $<TARGET_FILE_DIR:pvm_player>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:pvm_player>/assets
)

# Shipped as news.default.cfg (etc.) so a user's own news.cfg is never
# overwritten. A separate target (not a POST_BUILD step) so that editing a
# conf/*.cfg file alone is enough for the next build to refresh the copy.
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/news.default.cfg ${CMAKE_BINARY_DIR}/tagesschau.default.cfg
         ${CMAKE_BINARY_DIR}/ard.default.cfg ${CMAKE_BINARY_DIR}/zdf.default.cfg
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/conf/news.cfg ${CMAKE_BINARY_DIR}/news.default.cfg
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/conf/tagesschau.cfg ${CMAKE_BINARY_DIR}/tagesschau.default.cfg
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/conf/ard.cfg ${CMAKE_BINARY_DIR}/ard.default.cfg
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/conf/zdf.cfg ${CMAKE_BINARY_DIR}/zdf.default.cfg
  DEPENDS ${CMAKE_SOURCE_DIR}/conf/news.cfg ${CMAKE_SOURCE_DIR}/conf/tagesschau.cfg ${CMAKE_SOURCE_DIR}/conf/ard.cfg
          ${CMAKE_SOURCE_DIR}/conf/zdf.cfg
)
add_custom_target(news_default_config ALL
  DEPENDS ${CMAKE_BINARY_DIR}/news.default.cfg ${CMAKE_BINARY_DIR}/tagesschau.default.cfg
          ${CMAKE_BINARY_DIR}/ard.default.cfg ${CMAKE_BINARY_DIR}/zdf.default.cfg)
add_dependencies(pvm_player news_default_config)
