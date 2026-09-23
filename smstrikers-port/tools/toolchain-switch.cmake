# Nintendo Switch builds with devkitPro's devkitA64, libnx and portlibs:
#
#   dkp-pacman -S switch-dev switch-sdl2 switch-mesa switch-zlib switch-pkg-config
#   ./tools/fetch-switch-deps.sh
#   ./tools/switch/build-nvk.sh
#   CMAKE_TOOLCHAIN_FILE=$PWD/tools/toolchain-switch.cmake ./tools/configure.sh build-switch Release
#   ./tools/switch/rebuild.sh build-switch

if(DEFINED ENV{DEVKITPRO})
    file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" _dkp)
else()
    set(_dkp "/opt/devkitpro")
endif()
if(NOT EXISTS "${_dkp}/cmake/Switch.cmake")
    message(FATAL_ERROR "No devkitPro at ${_dkp}; install switch-dev or set DEVKITPRO.")
endif()
set(ENV{DEVKITPRO} "${_dkp}")

# The flags, specs file, libnx and portlib search paths are devkitPro's, unchanged.
include("${_dkp}/cmake/Switch.cmake")

# The build keys every Switch branch off NX.
set(NX ON CACHE BOOL "Build for Nintendo Switch" FORCE)
