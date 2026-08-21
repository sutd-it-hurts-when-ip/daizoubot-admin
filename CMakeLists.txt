cmake_minimum_required(VERSION 3.16)
project(daizoubu_admin C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (WIN32)
    find_package(unofficial-pdcurses CONFIG REQUIRED)
    set(CURSES_TARGET unofficial::pdcurses::pdcurses)
else()
    find_package(Curses REQUIRED)
    set(CURSES_TARGET ${CURSES_LIBRARIES})
endif()

find_package(mongoc CONFIG REQUIRED)

include(CTest)

add_executable(daizoubu_admin
    src/main.c
    src/ui_markup.c
    src/mongo_adapter.c
)

target_include_directories(daizoubu_admin PRIVATE
    include
)

target_link_libraries(daizoubu_admin PRIVATE
    ${CURSES_TARGET}
    mongoc::mongoc
)

if(BUILD_TESTING)
    add_executable(daizoubu_tests
        tests/test_core.c
        src/ui_markup.c
        src/mongo_adapter.c
    )

    target_include_directories(daizoubu_tests PRIVATE
        include
        ${CMAKE_SOURCE_DIR}
    )

    target_link_libraries(daizoubu_tests PRIVATE
        ${CURSES_TARGET}
        mongoc::mongoc
    )

    add_test(NAME daizoubu_core_tests COMMAND daizoubu_tests)
endif()
