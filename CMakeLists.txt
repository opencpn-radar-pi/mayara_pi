# ---------------------------------------------------------------------------
# mayara_pi - OpenCPN plugin talking to mayara-server (REST + WebSocket).
# Spiritual successor to radar_pi.
#
# Build system: OpenCPN "Frontend2" (FE2) template by Jon Gough.
# Only this file is plugin-specific; everything under cmake/ and ci/ is the
# upstream-maintained FE2 machinery and should be updated wholesale, not edited.
# ---------------------------------------------------------------------------

set(CMLOC "CMakeLists: ")

cmake_minimum_required(VERSION 3.15)
if(COMMAND cmake_policy)
  if(POLICY CMP0043)
    cmake_policy(SET CMP0043 NEW)
  endif()
  cmake_policy(SET CMP0048 NEW)
  if(POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
  endif()
  if(POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
  endif()
  if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
  endif()
  if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
  endif()
  if(POLICY CMP0175)
    cmake_policy(SET CMP0175 NEW)
  endif()
  if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
  endif()
endif()

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# ---------------------------------------------------------------------------
# -- Plugin identity. Edit values here; do not reorder the sections.        --
# ---------------------------------------------------------------------------
set(VERBOSE_NAME "Mayara")
set(COMMON_NAME "Mayara")
set(TITLE_NAME "mayara")
set(PACKAGE_CONTACT "opencpn-radar-pi")
set(PACKAGE "mayara")
set(SHORT_DESCRIPTION
    "Marine radar overlay and PPI, served by mayara-server"
)
set(LONG_DESCRIPTION
    "mayara_pi displays marine radar as a chart overlay and in a PPI window. \
Unlike radar_pi it does not talk to radar hardware directly; it consumes the \
mayara-server REST and WebSocket API, which supports Navico, Garmin, Furuno \
and Raymarine radars."
)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_PATCH "5")
set(VERSION_TWEAK "0")
set(OCPN_MIN_VERSION "ov511")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "21")

set(WX_VER "32")
set(WX_VER_PATCH "8")

set(TP_COMMENT "  * Release using CI")

set(PARENT "opencpn")

# Cloudsmith upload repository suffixes for each catalog channel.
set(PROD "prod")
set(BETA "beta")
set(ALPHA "alpha")

# Cloudsmith repository base name and owner.
set(CLOUDSMITH_BASE_REPOSITORY "mayara")
set(CLOUDSMITH_USER "opencpn-radar-pi")

set(XML_INFO_URL "https://github.com/opencpn-radar-pi/mayara_pi")
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

# ---------------------------------------------------------------------------
# -- Special build requirements                                             --
# ---------------------------------------------------------------------------
option(PLUGIN_USE_SVG "Use SVG graphics" ON)

set(CMAKE_CXX_STANDARD 17)

set(USE_LOCAL_GLU FALSE)
set(OpenGL_GL_PREFERENCE "LEGACY")
option(USE_GL "Enable OpenGL support" ON)
message(STATUS "${CMLOC}USE_GL: ${USE_GL}")

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Choose the type of build" FORCE
  )
endif()
set_property(
  CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "" "Debug" "Release" "RelWithDebInfo"
                                  "MinSizeRel"
)
message(STATUS "${CMLOC}Build type: ${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------------------
# -- Standard CMAKE variable setup (do not remove)                          --
# ---------------------------------------------------------------------------
project(${PACKAGE})
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include("PluginSetup")

set(CMAKE_VERBOSE_MAKEFILE ON)
option(Plugin_CXX11 "Use c++11" OFF)

# Short commit id, baked into the binary via cmake/in-files/version.h.extra so
# mayara_pi::GetPlugInVersionBuild() can report it. VERSION_MAJOR/MINOR/PATCH
# above only change at release time, so without this every alpha/beta build
# in between reports the exact same version, whatever commit it was built
# from -- see cmake/in-files/version.h.extra for detail.
find_package(Git QUIET)
set(PLUGIN_GIT_COMMIT "unknown")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE PLUGIN_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _plugin_git_rev_parse_result
  )
  if(NOT _plugin_git_rev_parse_result EQUAL 0 OR PLUGIN_GIT_COMMIT STREQUAL "")
    set(PLUGIN_GIT_COMMIT "unknown")
  endif()
endif()
message(STATUS "${CMLOC}PLUGIN_GIT_COMMIT: ${PLUGIN_GIT_COMMIT}")

# Channel this build publishes to (alpha/beta/prod/dev), baked into
# GetPlugInVersionBuild() via version.h.extra so OpenCPN's plugin manager can
# show which channel a running binary came from, not just its commit --
# mirrors the tag-string routing in cmake/in-files/cloudsmith-upload.sh.in
# and .github/workflows/build.yml exactly. This repo's only real CI is
# GitHub Actions (see build.yml), so no need to handle CircleCI/Travis/
# AppVeyor here the way PluginConfigure.cmake does for GIT_REPOSITORY_TAG.
set(PLUGIN_CHANNEL "dev")
if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
  if("$ENV{GITHUB_REF_NAME}" MATCHES "-beta\\.[0-9]+")
    string(REGEX MATCH "beta\\.[0-9]+" PLUGIN_CHANNEL "$ENV{GITHUB_REF_NAME}")
  else()
    set(PLUGIN_CHANNEL "prod")
  endif()
elseif(DEFINED ENV{GITHUB_ACTIONS})
  set(PLUGIN_CHANNEL "alpha")
endif()
message(STATUS "${CMLOC}PLUGIN_CHANNEL: ${PLUGIN_CHANNEL}")

include("PluginConfigure")

# ---------------------------------------------------------------------------
# -- Plugin sources                                                         --
# ---------------------------------------------------------------------------
set(SRCS
    src/mayara_pi.cpp
    src/PpiWindow.cpp
    src/MayaraClient.cpp
    src/RadarPalette.cpp
    src/RadarState.cpp
    src/RadarMessage.cpp
    src/MayaraDiscovery.cpp
    src/LocalServerHttp.cpp
    src/MayaraServer.cpp
    src/RadarDisplayPanel.cpp
    src/ControlsPanel.cpp
    src/RadarControls.cpp
    src/ThemedControls.cpp
)

set(HDRS
    include/mayara_pi.h
    include/PpiWindow.h
    include/MayaraClient.h
    include/RadarState.h
    include/RadarMessage.h
    include/MayaraDiscovery.h
    include/MayaraServer.h
    include/RadarDisplayPanel.h
    include/ControlsPanel.h
)

if(PLUGIN_USE_SVG)
  add_definitions(-DPLUGIN_USE_SVG)
endif()

set(SRC ${SRCS} ${HDRS})

include_directories(BEFORE ${PROJECT_SOURCE_DIR}/include)
include_directories(BEFORE ${PROJECT_SOURCE_DIR}/ocpninclude)

target_sources(${PACKAGE_NAME} PUBLIC ${SRC})

if(NOT OCPN_FLATPAK_CONFIG)
  if(WIN32)
    add_subdirectory(opencpn-libs/WindowsHeaders)
    target_link_libraries(${PACKAGE_NAME} windows::headers)
  endif()

  add_subdirectory(opencpn-libs/api-${OCPN_API_VERSION_MINOR})
  target_link_libraries(${PACKAGE_NAME} ocpn::api)

  # --- mayara-server client networking -------------------------------------
  # IXWebSocket: WebSocket + HTTP client, permessage-deflate via zlib. TLS is
  # off for now (mayara-server is typically ws:// on the LAN); enable later.
  set(USE_TLS OFF CACHE BOOL "" FORCE)
  # zlib enables permessage-deflate. It's a system library on macOS/Linux but
  # not on Windows/MSVC (find_package(ZLIB) would fail the configure); drop it
  # there. WebSocket compression is negotiated, so uncompressed frames still
  # work -- just a little more bandwidth on Windows.
  if(WIN32)
    set(USE_ZLIB OFF CACHE BOOL "" FORCE)
  else()
    set(USE_ZLIB ON CACHE BOOL "" FORCE)
  endif()
  set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
  # Build IXWebSocket as a STATIC lib linked into the plugin (FE2 sets
  # BUILD_SHARED_LIBS globally, which would otherwise emit a separate dylib the
  # plugin can't find at load time). PIC so it links into our shared object.
  set(_mayara_saved_shared ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(third_party/IXWebSocket)
  set(BUILD_SHARED_LIBS ${_mayara_saved_shared})
  target_link_libraries(${PACKAGE_NAME} ixwebsocket)
  target_include_directories(
    ${PACKAGE_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party
    ${PROJECT_SOURCE_DIR}/third_party/IXWebSocket
    ${PROJECT_SOURCE_DIR}/third_party/mdns
  )
  # MAKING_PLUGIN switches ocpn_plugin.h's DECL_IMP to __declspec(dllimport).
  # It guards exactly one declaration -- wxEVT_DOWNLOAD_EVENT -- and MSVC
  # resolves a *data* symbol from opencpn.lib only through its __imp_ form, so
  # without this the background-download API fails to link on Windows.
  if(WIN32)
    target_compile_definitions(${PACKAGE_NAME} PRIVATE MAKING_PLUGIN)
  endif()
  # MSVC portability: M_PI needs _USE_MATH_DEFINES before <cmath>; NOMINMAX
  # keeps windows.h from defining min/max macros; WIN32_LEAN_AND_MEAN trims it.
  if(MSVC)
    target_compile_definitions(${PACKAGE_NAME} PRIVATE
      _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
    # The sources are UTF-8. Without /utf-8 MSVC reads them as the system ANSI
    # codepage, so every non-ASCII literal (degree signs, ellipses, em dashes,
    # arrows, the gear and cross glyphs) is mangled at compile time.
    target_compile_options(${PACKAGE_NAME} PRIVATE /utf-8)

    # OpenCPN 5.14.0's Windows installer ships a VS2017-era C++ runtime
    # (msvcp140.dll 14.12.25810.0) next to opencpn.exe, and Windows
    # resolves DLLs from the executable's directory first -- so that
    # is the runtime every plugin gets, whatever it was built
    # against.
    #
    # VS2022 17.10 gave std::mutex a constexpr constructor, so the
    # mutex is zero-initialised inline in our own binary instead of
    # by _Mtx_init_in_situ. Locking one of those against the older
    # runtime dereferences null inside msvcp140 and takes the whole
    # process down -- OpenCPN dies at our Init(), with no plugin
    # frame on the stack to say why.
    #
    # This opt-out restores the non-constexpr constructor, which the
    # shipped runtime understands. It can go when OpenCPN ships a
    # current redistributable.
    target_compile_definitions(${PACKAGE_NAME} PRIVATE
      _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
  endif()
endif()

# ---------------------------------------------------------------------------
# -- Install / localization / packaging (do not remove)                     --
# ---------------------------------------------------------------------------
include("PluginInstall")
include("PluginLocalization")
include("PluginPackage")
