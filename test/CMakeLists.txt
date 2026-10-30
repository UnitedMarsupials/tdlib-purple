if (DEFINED GTEST_PATH)
    link_directories(${GTEST_PATH}/lib)
endif (DEFINED GTEST_PATH)

set(GLIB_LIBRARIES "glib-2.0" CACHE STRING "GLib libraries")

# Compiling plugin sources again is not optimal but it's the easy way
add_executable(tests EXCLUDE_FROM_ALL
    test-main.cpp
    login-test.cpp
    private-chat-test.cpp
    group-chat-test.cpp
    supergroup-test.cpp
    file-transfer-test.cpp
    secret-chat-test.cpp
    message-split-test.cpp
    message-order-test.cpp
    message-history-test.cpp
    message-edit-test.cpp
    location-test.cpp
    reaction-test.cpp
    message-link-test.cpp
    custom-emoji-test.cpp
    clipboard-test.cpp
    formatted-text-test.cpp
    test-transceiver.cpp
    libpurple-mock.cpp
    printout.cpp
    purple-events.cpp
    fixture.cpp

    ../tdlib-purple.cpp
    ../td-client.cpp
    ../transceiver.cpp
    ../account-data.cpp
    ../purple-info.cpp
    ${CMAKE_BINARY_DIR}/config.cpp
    ../client-utils.cpp
    ../receiving.cpp
    ../format.cpp
    ../sticker.cpp
    ../custom-emoji.cpp
    ../media-variants.cpp
    ../file-transfer.cpp
    ../call.cpp
    ../identifiers.cpp
    ../secret-chat.cpp
    ../pidgin-ui.cpp
    ../log-history.cpp
    ../clipboard.cpp
)

# C++17, not the plugin's C++14: GoogleTest requires it from 1.17 on.
set_property(TARGET tests PROPERTY CXX_STANDARD 17)
target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR} ${fmt_INCLUDE_DIRS} ${rlottie_INCLUDE_DIRS})
# The plugin gets zlib and the thread library by way of the shared libraries it loads, but this
# binary links TDLib statically and so has to name them itself.
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(tests PRIVATE gtest ${fmt_LIBRARIES} Td::TdStatic ${GLIB_LIBRARIES}
                      ${rlottie_LIBRARIES} ZLIB::ZLIB Threads::Threads ${GModule_LIBRARIES} ${GIO_LIBRARIES})
# The mock GdkPixbuf has to be found among the program's symbols, as Pidgin's would be
set_property(TARGET tests PROPERTY ENABLE_EXPORTS ON)

if (DEFINED GTEST_PATH)
    target_include_directories(tests PRIVATE ${GTEST_PATH}/include)
    link_directories(${GTEST_PATH}/lib)
endif (DEFINED GTEST_PATH)


if (NOT NoPidgin)
    target_include_directories(tests SYSTEM PRIVATE ${Pidgin_INCLUDE_DIRS})
    target_compile_options(tests PRIVATE ${Pidgin_CFLAGS_OTHER})
endif (NOT NoPidgin)

if (NOT NoLottie)
    target_compile_definitions(tests PRIVATE LOT_BUILD)
endif (NOT NoLottie)

if (NOT NoTranslations)
    # The top level finds it only after adding this directory
    find_package(Intl REQUIRED)
    target_include_directories(tests PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(tests PRIVATE ${Intl_LIBRARIES})
endif (NOT NoTranslations)

if (NOT tgvoip_INCLUDE_DIRS STREQUAL "")
    target_include_directories(tests SYSTEM PRIVATE ${tgvoip_INCLUDE_DIRS})
endif (NOT tgvoip_INCLUDE_DIRS STREQUAL "")
if (NOT NoVoip)
    target_link_libraries(tests PRIVATE ${tgvoip_LIBRARIES})
endif (NOT NoVoip)

add_custom_target(run-tests ${CMAKE_CURRENT_BINARY_DIR}/tests DEPENDS tests)
