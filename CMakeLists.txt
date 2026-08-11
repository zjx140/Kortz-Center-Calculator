cmake_minimum_required(VERSION 3.20)

project(KortzCenterCalculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(kortz_optimizer
    src/optimizer.cpp
)
target_include_directories(kortz_optimizer PUBLIC src)

add_executable(KortzCenterCalculator WIN32
    src/main.cpp
)
target_link_libraries(KortzCenterCalculator PRIVATE
    kortz_optimizer
    comctl32
    uxtheme
)
target_compile_definitions(KortzCenterCalculator PRIVATE UNICODE _UNICODE)

add_executable(kortz_optimizer_tests
    tests/optimizer_tests.cpp
)
target_link_libraries(kortz_optimizer_tests PRIVATE kortz_optimizer)

if(MSVC)
    foreach(target kortz_optimizer KortzCenterCalculator kortz_optimizer_tests)
        target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive-)
    endforeach()
endif()

enable_testing()
add_test(NAME optimizer_tests COMMAND kortz_optimizer_tests)

install(TARGETS KortzCenterCalculator RUNTIME DESTINATION .)
