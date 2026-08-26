# ~~~
# Summary:     Cmake toolchain file for cross-compiling to Debian/Ubuntu armhf
#              (32-bit ARM) using the distro's crossbuild-essential-armhf
#              multiarch toolchain. Target dev libraries (wx, gtk, etc.) come
#              from ":armhf" packages installed alongside the host's own, via
#              `apt-get build-dep -a armhf`, not from a separate sysroot.
# License:     GPLv3+
# ~~~

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Multiarch layout, not a sysroot: only libraries/includes come from the
# armhf tree, programs (cmake helpers, code generators) stay host-native.
set(CMAKE_FIND_ROOT_PATH /usr/lib/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# crossbuild-essential-armhf provides this wrapper, which points pkg-config
# at the armhf .pc files instead of the host's.
if (NOT DEFINED ENV{PKG_CONFIG})
  set(ENV{PKG_CONFIG} arm-linux-gnueabihf-pkg-config)
endif ()

# find_package(wxWidgets) shells out to wx-config to read target flags; it
# must run the armhf build of wx-config, not the host's. WX_CONFIG is the
# same override MacosWxwidgets.cmake uses to steer the same CMake module.
if (NOT DEFINED ENV{WX_CONFIG})
  execute_process(
    COMMAND sh -c "ls /usr/lib/arm-linux-gnueabihf/wx/config/*-3.2 2>/dev/null | head -1"
    OUTPUT_VARIABLE ARMHF_WX_CONFIG
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (ARMHF_WX_CONFIG)
    set(ENV{WX_CONFIG} ${ARMHF_WX_CONFIG})
  else ()
    message(WARNING "Could not locate armhf wx-config under /usr/lib/arm-linux-gnueabihf/wx/config")
  endif ()
endif ()
