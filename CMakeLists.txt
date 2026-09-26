cmake_minimum_required(VERSION 3.20)
# Before project(), APPLE reflects the *host*, so a macOS machine
# cross-compiling for Android must be excluded explicitly.
if(CMAKE_HOST_APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
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

if(NOT ANDROID)
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
endif()

# Dear ImGui: vendored source, core + the platform/renderer backends for the
# target: GLFW + OpenGL 3 on desktop, Android + OpenGL ES 3 on Android. Input
# never goes through ImGui's own backends (see Platform::pollEvents); they only
# supply display size, time and the GL renderer.
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/thirdparty/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)
if(ANDROID)
  target_sources(imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_android.cpp)
  target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
  target_link_libraries(imgui PUBLIC android log EGL GLESv3)
else()
  target_sources(imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp)
  target_link_libraries(imgui PUBLIC glfw)
endif()

if(NOT ANDROID)
  # libmpv: prefer a vendored Windows SDK under thirdparty/libmpv if present
  # (see thirdparty/libmpv/README.md); otherwise fall back to a system install
  # (e.g. Homebrew's `mpv` formula on macOS dev machines) so the prototype can
  # be built and verified without the Windows SDK in hand. (The Android build
  # has no libmpv yet: it compiles src/mpv_player_stub.cpp instead.)
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

  # libcurl (HTTP fetch) ships with macOS's SDK and is an apt package on the
  # Pi. On Android, HTTP goes through JNI instead (src/platform/android).
  find_package(CURL REQUIRED)
endif()

# pugixml (RSS/Atom parsing) for the NEWS section: `brew install pugixml` /
# `apt install libpugixml-dev`, falling back to a source build if no system
# copy is found (always the case on Android).
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
# Resolved in order: the vendored copy, a system install (Homebrew, apt), then
# a fetched copy -- the last being what a cross-compiling Android build uses.
add_library(json INTERFACE)
if(EXISTS ${CMAKE_SOURCE_DIR}/thirdparty/json/nlohmann/json.hpp)
  target_include_directories(json INTERFACE ${CMAKE_SOURCE_DIR}/thirdparty/json)
else()
  find_package(nlohmann_json QUIET)
  if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found on the system; fetching it")
    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG v3.11.3
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
  target_link_libraries(json INTERFACE nlohmann_json::nlohmann_json)
endif()

# Everything teletext that needs neither ImGui nor a window: page model,
# navigation state, HTTP fetch, feed parsing. Kept as its own library so it
# can be used without the GUI stack. Only the HTTP transport differs per
# platform: libcurl on desktop, JNI on Android.
add_library(teletext_core STATIC
  src/teletext/page.cpp
  src/teletext/navigator.cpp
  src/teletext/html_strip.cpp
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
target_link_libraries(teletext_core PUBLIC pugixml::pugixml json)
if(ANDROID)
  target_sources(teletext_core PRIVATE src/platform/android/http_fetch_android.cpp)
else()
  target_sources(teletext_core PRIVATE src/teletext/http_fetch.cpp)
  target_link_libraries(teletext_core PUBLIC CURL::libcurl)
endif()

# Platform-free input vocabulary (actions, key maps). input_core has no
# windowing dependency; each platform adds its own key translation on top
# (input_glfw on desktop, input_android on Android).
add_library(input_core STATIC
  src/input/input_action.cpp
  src/input/keymap.cpp
  src/input/gamepad_input.cpp
)
target_include_directories(input_core PUBLIC src)

if(NOT ANDROID)
  add_library(input_glfw STATIC src/platform/glfw/keymap_glfw.cpp)
  target_link_libraries(input_glfw PUBLIC input_core glfw)

  enable_testing()
  add_executable(input_selftest tests/input_selftest.cpp)
  target_link_libraries(input_selftest PRIVATE input_glfw)
  add_test(NAME input_selftest COMMAND input_selftest)

  # The gamepad translator is platform-free, and the Android default key map
  # only needs the AKEYCODE_* constants, so both are tested on the desktop
  # against a shim of <android/keycodes.h>.
  add_executable(pad_selftest tests/pad_selftest.cpp src/platform/android/keymap_android.cpp)
  target_include_directories(pad_selftest PRIVATE tests/android_shim)
  target_link_libraries(pad_selftest PRIVATE input_core)
  add_test(NAME pad_selftest COMMAND pad_selftest)
endif()

# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------
# The platform-independent app: screens, settings, rendering.
set(PVM_APP_SOURCES
  src/app.cpp
  src/settings.cpp
  src/shader.cpp
  src/ui/menu.cpp
  src/ui/file_browser.cpp
  src/ui/style.cpp
  src/teletext/teletext_view.cpp
)

if(ANDROID)
  # Built as libpvm_player.so by the Gradle project in android/ and loaded by
  # its GameActivity subclass.
  find_package(game-activity REQUIRED CONFIG)

  add_library(input_android STATIC src/platform/android/keymap_android.cpp)
  target_link_libraries(input_android PUBLIC input_core)
  target_link_libraries(input_android PUBLIC android)

  # libmpv: the prebuilt arm64 library that android/fetch_libmpv.sh puts in
  # thirdparty/libmpv-android (Gradle packages it from there). Without it the
  # app still builds, using a player that cannot play anything.
  set(PVM_LIBMPV_DIR ${CMAKE_SOURCE_DIR}/thirdparty/libmpv-android)
  if(ANDROID_ABI STREQUAL "arm64-v8a"
     AND EXISTS ${PVM_LIBMPV_DIR}/lib/arm64-v8a/libmpv.so
     AND EXISTS ${PVM_LIBMPV_DIR}/include/mpv/client.h)
    add_library(mpv SHARED IMPORTED)
    set_target_properties(mpv PROPERTIES
      IMPORTED_LOCATION ${PVM_LIBMPV_DIR}/lib/arm64-v8a/libmpv.so
      INTERFACE_INCLUDE_DIRECTORIES ${PVM_LIBMPV_DIR}/include)
    set(PVM_MPV_SOURCE src/mpv_player.cpp)
    message(STATUS "Android: using libmpv from ${PVM_LIBMPV_DIR}")
  else()
    set(PVM_MPV_SOURCE src/mpv_player_stub.cpp)
    message(WARNING "libmpv for Android not found: playback will be disabled. Run android/fetch_libmpv.sh.")
  endif()

  add_library(pvm_player SHARED
    src/platform/android/android_main.cpp
    src/platform/android/android_platform.cpp
    src/platform/android/android_jni.cpp
    ${PVM_MPV_SOURCE}
    ${PVM_APP_SOURCES}
  )
  target_include_directories(pvm_player PRIVATE src)
  target_link_libraries(pvm_player PRIVATE
    game-activity::game-activity_static
    imgui teletext_core input_android
    android log EGL GLESv3
  )
  if(TARGET mpv)
    target_link_libraries(pvm_player PRIVATE mpv)
    target_compile_definitions(pvm_player PRIVATE PVM_HAVE_LIBMPV)
  endif()
  # GameActivity's JNI entry points live in a static library nothing else
  # references, so force them past the linker's dead-stripping.
  set_target_properties(pvm_player PROPERTIES LINK_FLAGS
    "-u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode -u GameActivity_onCreate")
else()
  add_executable(pvm_player
    src/platform/glfw/main_glfw.cpp
    src/platform/glfw/glfw_platform.cpp
    src/platform/glfw/paths.cpp
    src/mpv_player.cpp
    ${PVM_APP_SOURCES}
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
    # Objective-C needed. Both are used only by the GLFW platform.
    target_sources(pvm_player PRIVATE
      src/platform/glfw/macos_media_keys.mm
      src/platform/glfw/macos_sleep_guard.cpp)
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
endif()
