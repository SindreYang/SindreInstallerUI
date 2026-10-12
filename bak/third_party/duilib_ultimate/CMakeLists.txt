cmake_minimum_required(VERSION 3.20)

project(DuiLib VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_definitions(-DCMAKE)
add_definitions(-DUNICODE -D_UNICODE)

add_compile_options(/W3 /WX-) 
add_definitions(/D_UNICODE /DUNICODE)

option(DUILIB_BUILD_SHARED "构建共享 DuiLib 库" OFF)
option(DUILIB_BUILD_EXAMPLES "构建 DuiLib 示例" OFF)

add_subdirectory(DuiLib)

if(DUILIB_BUILD_EXAMPLES)
    add_subdirectory(Demos)
endif()
