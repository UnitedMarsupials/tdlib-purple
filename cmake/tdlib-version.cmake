# Which TDLib this is, and so which form of its API to use.
#
# Sets TDLIB_VERSION ("1.8.67", or "unknown"), TDLIB_VERSION_NUMBER (10867, or
# 0) and TDLIB_API_* (1 or 0; see buildopt.h.in) for the TDLib that
# find_package(Td) found.
#
# TDLib's headers carry no version.  It is looked for in, in order:
#   - TdConfigVersion.cmake, which find_package(Td) reads -- some packages
#     (Debian's) install it with no version in it, so it says 0.0.0;
#   - TDLib's pkg-config files;
#   - the soname of the shared libtdjson, e.g. libtdjson.so.1.8.67.
# If none tells, the build goes on, warning loudly, and the API changes are
# found by compiling probes against td_api.h instead.

set(TDLIB_VERSION "${Td_VERSION}")

macro(_tdlib_version_unknown result)
    set(${result} FALSE)
    if ("${TDLIB_VERSION}" STREQUAL "" OR "${TDLIB_VERSION}" VERSION_EQUAL 0)
        set(${result} TRUE)
    endif ()
endmacro()

_tdlib_version_unknown(_unknown)
if (_unknown AND NOT NoPkgConfig)
    include(FindPkgConfig)
    pkg_search_module(TdPkg QUIET tdclient tdjson)
    set(TDLIB_VERSION "${TdPkg_VERSION}")
endif ()

_tdlib_version_unknown(_unknown)
if (_unknown AND TARGET Td::tdjson)
    get_target_property(_configs Td::tdjson IMPORTED_CONFIGURATIONS)
    foreach (_property IMPORTED_SONAME IMPORTED_LOCATION)
        foreach (_config "" ${_configs})
            if (_config)
                set(_name "${_property}_${_config}")
            else ()
                set(_name "${_property}")
            endif ()
            get_target_property(_soname Td::tdjson ${_name})
            if (_soname MATCHES "tdjson[^/]*\\.so\\.([0-9]+\\.[0-9]+\\.[0-9]+)$")
                set(TDLIB_VERSION "${CMAKE_MATCH_1}")
                break ()
            endif ()
        endforeach ()
        _tdlib_version_unknown(_unknown)
        if (NOT _unknown)
            break ()
        endif ()
    endforeach ()
endif ()

_tdlib_version_unknown(_unknown)
if (_unknown)
    set(TDLIB_VERSION unknown)
    message(WARNING
        "\n"
        "################################################################\n"
        "#                                                              #\n"
        "#   CANNOT TELL WHICH VERSION OF TDLIB THIS IS.                #\n"
        "#                                                              #\n"
        "#   Neither TdConfigVersion.cmake, nor TDLib's pkg-config      #\n"
        "#   files, nor the soname of libtdjson says.  1.8.0 or newer   #\n"
        "#   is required; that cannot be checked.  Which form of the    #\n"
        "#   TDLib API to use is guessed by compiling probes instead.   #\n"
        "#                                                              #\n"
        "################################################################\n")
elseif ("${TDLIB_VERSION}" VERSION_LESS 1.8.0)
    # Error message must begin with "tdlib version" for a grep command from readme
    message(FATAL_ERROR "at least tdlib version 1.8.0 is required
(version found: ${TDLIB_VERSION})")
else ()
    message(STATUS "TDLib version: ${TDLIB_VERSION}")
endif ()

set(TDLIB_VERSION_NUMBER 0)
if ("${TDLIB_VERSION}" MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    math(EXPR TDLIB_VERSION_NUMBER "10000*${CMAKE_MATCH_1} + 100*${CMAKE_MATCH_2} + ${CMAKE_MATCH_3}")
endif ()

# tdlib_api(NAME FIRST_RELEASE TYPE): NAME is 1 for a TDLib at least
# FIRST_RELEASE (as a version number), or, if the version is unknown, for one
# whose td_api.h declares td::td_api::TYPE.
macro(tdlib_api name first_release type)
    if (TDLIB_VERSION_NUMBER)
        if (TDLIB_VERSION_NUMBER LESS ${first_release})
            set(${name} 0)
        else ()
            set(${name} 1)
        endif ()
    else ()
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_LIBRARIES Td::TdStatic)
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
        check_cxx_source_compiles("
            #include <td/telegram/td_api.h>
            int probe() { return td::td_api::${type}::ID; }
        " _probe_${name})
        unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
        unset(CMAKE_REQUIRED_LIBRARIES)
        if (_probe_${name})
            set(${name} 1)
        else ()
            set(${name} 0)
        endif ()
        message(STATUS "${name} (td_api::${type}): ${${name}}")
    endif ()
endmacro()

tdlib_api(TDLIB_API_IMPORTED_CONTACT 10856 importedContact)
tdlib_api(TDLIB_API_ADDED_PROXY      10861 addedProxy)
tdlib_api(TDLIB_API_INPUT_PHOTO      10865 inputPhoto)
