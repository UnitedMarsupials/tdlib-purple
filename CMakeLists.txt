cmake_minimum_required(VERSION 3.10)
# the oldest CMake 4 accepts without a deprecation warning

set(VERSION 0.9.4)
project(tdlib-purple VERSION ${VERSION} LANGUAGES CXX)

set(NoPkgConfig FALSE CACHE BOOL "Do not use pkg-config")

find_package(Td REQUIRED)
include(cmake/tdlib-version.cmake)

set(NoLottie FALSE CACHE BOOL "Disable animated sticker conversion")
set(NoTranslations FALSE CACHE BOOL "Disable translation support")
set(NoVoip FALSE CACHE BOOL "Disable voice call support")
set(NoPidgin FALSE CACHE BOOL "Build without Pidgin's headers, so without what only its conversation window can do")
set(API_ID 94575 CACHE STRING "API id")
set(API_HASH a3406de8d171bb422bb6ddf3bbd800e2 CACHE STRING "API hash")
set(STUFF "" CACHE STRING "")

if (NOT NoPkgConfig)
    include(FindPkgConfig)
    pkg_check_modules(Purple REQUIRED purple)
    # To find what the front-end has loaded, such as Pidgin's GdkPixbuf
    pkg_check_modules(GModule REQUIRED gmodule-2.0)
    # The types of files pasted, by their content, as file managers tell them
    pkg_check_modules(GIO REQUIRED gio-2.0)
    if (NOT NoVoip)
        pkg_check_modules(tgvoip tgvoip)
        if ("${tgvoip_LIBRARIES}" STREQUAL "")
            message(STATUS "tgvoip library not found, disabling voice call support")
            set(NoVoip TRUE)
        endif ("${tgvoip_LIBRARIES}" STREQUAL "")
    endif(NOT NoVoip)
    pkg_check_modules(fmt REQUIRED fmt)
    if (NOT NoLottie)
        pkg_check_modules(rlottie REQUIRED rlottie)
    endif (NOT NoLottie)
    # Pidgin's headers only, for its types: its functions, and GTK's, are
    # looked up at run time, so the plugin still loads into other front-ends
    if (NOT NoPidgin)
        pkg_check_modules(Pidgin pidgin)
        if (NOT Pidgin_FOUND)
            message(FATAL_ERROR "Pidgin's headers not found: install them (pidgin-dev on Debian, "
                                "Ubuntu and Alpine, pidgin-devel on Fedora, pidgin elsewhere), "
                                "or build without them: -DNoPidgin=True")
        endif (NOT Pidgin_FOUND)
    endif (NOT NoPidgin)

    pkg_get_variable(PURPLE_PLUGIN_DIR purple plugindir)
    pkg_get_variable(PURPLE_DATA_DIR purple datarootdir)
elseif (NOT NoPidgin AND NOT DEFINED Pidgin_INCLUDE_DIRS)
    message(FATAL_ERROR "Without pkg-config, give Pidgin's headers (and GTK's) in -DPidgin_INCLUDE_DIRS, "
                        "or build without them: -DNoPidgin=True")
endif (NOT NoPkgConfig)

link_directories(${Purple_LIBRARY_DIRS})


if (NOT DEFINED SHARE_INSTALL_PREFIX)
    set(SHARE_INSTALL_PREFIX "share")
endif (NOT DEFINED SHARE_INSTALL_PREFIX)

if (IS_ABSOLUTE "${SHARE_INSTALL_PREFIX}")
    set(LOCALE_DIR "${SHARE_INSTALL_PREFIX}/locale")
else (IS_ABSOLUTE "${SHARE_INSTALL_PREFIX}")
    set(LOCALE_DIR "${CMAKE_INSTALL_PREFIX}/${SHARE_INSTALL_PREFIX}/locale")
endif (IS_ABSOLUTE "${SHARE_INSTALL_PREFIX}")

configure_file(buildopt.h.in buildopt.h)
configure_file(config.cpp.in config.cpp)

add_library(telegram-tdlib SHARED
    tdlib-purple.cpp
    td-client.cpp
    transceiver.cpp
    account-data.cpp
    purple-info.cpp
    ${CMAKE_BINARY_DIR}/config.cpp
    client-utils.cpp
    receiving.cpp
    format.cpp
    sticker.cpp
    custom-emoji.cpp
    media-variants.cpp
    file-transfer.cpp
    call.cpp
    identifiers.cpp
    secret-chat.cpp
    pidgin-ui.cpp
    log-history.cpp
    clipboard.cpp
    thumbnail.cpp
)

#target_compile_options(telegram-tdlib PRIVATE -Wmissing-declarations)

include_directories(${Purple_INCLUDE_DIRS} ${GModule_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS} ${CMAKE_BINARY_DIR} ${fmt_INCLUDE_DIRS} ${rlottie_INCLUDE_DIRS})
target_link_libraries(telegram-tdlib PRIVATE Td::TdStatic ${Purple_LIBRARIES} ${fmt_LIBRARIES} ${rlottie_LIBRARIES}
                      ${GModule_LIBRARIES} ${GIO_LIBRARIES})

set(VERSION_SCRIPT_FLAG "-Wl,--version-script=${CMAKE_SOURCE_DIR}/ld_version_script")
set(CMAKE_EXE_LINKER_FLAGS ${VERSION_SCRIPT_FLAG})
try_compile(VERSION_SCRIPT_SUPPORTED ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/cmake/testflag.cpp)
set(CMAKE_EXE_LINKER_FLAGS "")
if (${VERSION_SCRIPT_SUPPORTED})
    set_target_properties(telegram-tdlib PROPERTIES LINK_FLAGS ${VERSION_SCRIPT_FLAG})
endif (${VERSION_SCRIPT_SUPPORTED})


set_property(TARGET telegram-tdlib PROPERTY CXX_STANDARD 14)

set(BUILD_SHARED_LIBS OFF)

if (NOT NoPidgin)
    # GTK 2's headers warn under today's compilers
    target_include_directories(telegram-tdlib SYSTEM PRIVATE ${Pidgin_INCLUDE_DIRS})
    target_compile_options(telegram-tdlib PRIVATE ${Pidgin_CFLAGS_OTHER})
endif (NOT NoPidgin)

if (NOT tgvoip_INCLUDE_DIRS STREQUAL "")
    target_include_directories(telegram-tdlib SYSTEM PRIVATE ${tgvoip_INCLUDE_DIRS})
endif (NOT tgvoip_INCLUDE_DIRS STREQUAL "")
if (NOT NoVoip)
    target_link_libraries(telegram-tdlib PRIVATE ${tgvoip_LIBRARIES})
endif (NOT NoVoip)

if (DEFINED STANDARD_LIBRARIES_EXTRA)
    set (CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} ${STANDARD_LIBRARIES_EXTRA}")
endif (DEFINED STANDARD_LIBRARIES_EXTRA)

install(TARGETS telegram-tdlib DESTINATION "${PURPLE_PLUGIN_DIR}")
install(FILES data/telegram16.png DESTINATION "${PURPLE_DATA_DIR}/pixmaps/pidgin/protocols/16" RENAME telegram.png)
install(FILES data/telegram22.png DESTINATION "${PURPLE_DATA_DIR}/pixmaps/pidgin/protocols/22" RENAME telegram.png)
install(FILES data/telegram48.png DESTINATION "${PURPLE_DATA_DIR}/pixmaps/pidgin/protocols/48" RENAME telegram.png)
install(FILES data/tdlib-purple.metainfo.xml DESTINATION "${SHARE_INSTALL_PREFIX}/metainfo")

add_subdirectory(test)

add_custom_command(
    OUTPUT libtelegram-tdlib-stripped.dll
    COMMAND i686-w64-mingw32-strip -o libtelegram-tdlib-stripped.dll libtelegram-tdlib.dll
    DEPENDS telegram-tdlib
)
add_custom_command(
    OUTPUT lang.nsi
    COMMAND ${CMAKE_SOURCE_DIR}/po/gen-nsi.sh lang.nsi
    DEPENDS ${CMAKE_SOURCE_DIR}/po/gen-nsi.sh
    DEPENDS ${CMAKE_SOURCE_DIR}/po/LINGUAS
)
add_custom_target(nsis
    makensis -DBUILD_DIR=${CMAKE_BINARY_DIR} -DPRPL_LIBRARY=libtelegram-tdlib-stripped.dll -DPLUGIN_VERSION=${VERSION} ${CMAKE_SOURCE_DIR}/tdlib-purple.nsi
    DEPENDS libtelegram-tdlib-stripped.dll lang.nsi
)

if (NOT NoTranslations)
    find_package(Gettext REQUIRED)
    file(STRINGS po/LINGUAS _linguas)
    foreach(_lang ${_linguas})
        GETTEXT_PROCESS_PO_FILES(
            ${_lang} ALL
            PO_FILES "${CMAKE_SOURCE_DIR}/po/${_lang}.po"
        )
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/${_lang}.gmo
            DESTINATION "${SHARE_INSTALL_PREFIX}/locale/${_lang}/LC_MESSAGES"
            RENAME tdlib-purple.mo
        )
        add_custom_target(idontwanttohavethisrule_${_lang} DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${_lang}.gmo)
        add_dependencies(nsis idontwanttohavethisrule_${_lang})
    endforeach()

    find_package(Intl REQUIRED)
    target_include_directories(telegram-tdlib PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(telegram-tdlib PRIVATE ${Intl_LIBRARIES})
endif (NOT NoTranslations)
