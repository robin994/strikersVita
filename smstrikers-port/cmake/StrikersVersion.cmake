set(_sv_input "${STRIKERS_VERSION}")
if(_sv_input STREQUAL "" AND DEFINED ENV{STRIKERS_VERSION})
    set(_sv_input "$ENV{STRIKERS_VERSION}")
endif()

set(_sv_explicit TRUE)
if(_sv_input STREQUAL "")
    set(_sv_explicit FALSE)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*"
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
            OUTPUT_VARIABLE _sv_input
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
endif()

if(_sv_input MATCHES "^v?(([0-9]+)\\.([0-9]+)\\.([0-9]+))(.*)$")
    set(STRIKERS_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(STRIKERS_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(STRIKERS_VERSION_PATCH "${CMAKE_MATCH_4}")
    set(STRIKERS_VERSION "${CMAKE_MATCH_1}")
    set(STRIKERS_VERSION_STRING "${CMAKE_MATCH_1}${CMAKE_MATCH_5}")
elseif(_sv_explicit)
    message(FATAL_ERROR "STRIKERS_VERSION '${_sv_input}' is not X.Y.Z or vX.Y.Z")
else()
    message(WARNING "No vX.Y.Z tag reachable and STRIKERS_VERSION unset; versioning the build 0.0.0")
    set(STRIKERS_VERSION_MAJOR 0)
    set(STRIKERS_VERSION_MINOR 0)
    set(STRIKERS_VERSION_PATCH 0)
    set(STRIKERS_VERSION "0.0.0")
    set(STRIKERS_VERSION_STRING "0.0.0")
endif()
message(STATUS "Version: ${STRIKERS_VERSION_STRING}")

file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version/strikers_version.h" CONTENT
"#define STRIKERS_VERSION_RC  @STRIKERS_VERSION_MAJOR@,@STRIKERS_VERSION_MINOR@,@STRIKERS_VERSION_PATCH@,0
#define STRIKERS_VERSION_STR \"@STRIKERS_VERSION_STRING@\"
" @ONLY)
