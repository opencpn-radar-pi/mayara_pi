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
set(VERSION_PATCH "0")
set(VERSION_TWEAK "0")
set(VERSION_DATE "19/07/2026") # DD/MM/YYYY
set(OCPN_MIN_VERSION "ov58")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "18")

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

include("PluginConfigure")

# ---------------------------------------------------------------------------
# -- Plugin sources                                                         --
# ---------------------------------------------------------------------------
set(SRCS
    src/mayara_pi.cpp
    src/PpiWindow.cpp
    src/MayaraClient.cpp
)

set(HDRS
    include/mayara_pi.h
    include/PpiWindow.h
    include/MayaraClient.h
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
  set(USE_ZLIB ON CACHE BOOL "" FORCE)
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
  )
endif()

# ---------------------------------------------------------------------------
# -- Install / localization / packaging (do not remove)                     --
# ---------------------------------------------------------------------------
include("PluginInstall")
include("PluginLocalization")
include("PluginPackage")
