# CMake 构建 DuiLib_Ultimate 核心库。

file(GLOB RootSrc CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB ControlSrc CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Control/*.cpp")
file(GLOB CoreSrc CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp")
file(GLOB LayoutSrc CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Layout/*.cpp")
file(GLOB UtilsSrc CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Utils/*.cpp")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Control)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Layout)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Utils)

if(DUILIB_BUILD_SHARED)
    add_library(duilib SHARED ${ControlSrc} ${CoreSrc} ${LayoutSrc} ${UtilsSrc} ${RootSrc})
else()
    add_library(duilib STATIC ${ControlSrc} ${CoreSrc} ${LayoutSrc} ${UtilsSrc} ${RootSrc})
    target_compile_definitions(duilib PUBLIC UILIB_STATIC)
endif()

target_link_libraries(duilib comctl32 gdi32 user32 ole32 oleaut32 gdiplus shlwapi)
target_compile_definitions(duilib PRIVATE UILIB_EXPORTS)
target_include_directories(duilib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Control>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Layout>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Utils>
)
set_target_properties(duilib PROPERTIES
    OUTPUT_NAME "duilib"
)
set_property(TARGET duilib PROPERTY CXX_STANDARD 14)
set_property(TARGET duilib PROPERTY CXX_STANDARD_REQUIRED ON)

