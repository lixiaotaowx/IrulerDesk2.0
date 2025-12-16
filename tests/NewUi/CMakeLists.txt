cmake_minimum_required(VERSION 3.16)
project(NewUiTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Update this path if necessary or rely on system path
set(CMAKE_PREFIX_PATH "C:/Qt/6.8.3/msvc2022_64")

find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)

add_executable(NewUiTest
    main.cpp
    NewUiWindow.cpp
    NewUiWindow.h
)

target_link_libraries(NewUiTest PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
)

# Copy resources
add_custom_command(TARGET NewUiTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:NewUiTest>/maps/logo
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/../../src/maps/logo"
    "$<TARGET_FILE_DIR:NewUiTest>/maps/logo"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/maps/t.png"
    "$<TARGET_FILE_DIR:NewUiTest>/maps/t.png"
)
