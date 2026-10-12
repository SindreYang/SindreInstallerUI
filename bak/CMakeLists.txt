cmake_minimum_required(VERSION 3.20)

project(SindreInstallerUI VERSION 0.1.0 LANGUAGES CXX)

option(SINDRE_BUILD_PREVIEW "构建独立 UI 预览程序" ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(SINDRE_SKIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/skin")
set(SINDRE_SKIN_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/skin")
add_custom_target(SindreInstallerResources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SINDRE_SKIN_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SINDRE_SKIN_SOURCE_DIR}"
        "${SINDRE_SKIN_OUTPUT_DIR}"
)

set(DUILIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DUILIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/duilib_ultimate)

set(NSIS_PLUGIN_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nsis")
add_library(SindreInstallerUI SHARED
    src/installer_window.cpp
    src/nsis_plugin.cpp
    src/SindreInstallerUI.def
)
add_dependencies(SindreInstallerUI SindreInstallerResources)
target_include_directories(SindreInstallerUI PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/duilib_ultimate/DuiLib"
    "${NSIS_PLUGIN_API_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(SindreInstallerUI PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
)
target_compile_options(SindreInstallerUI PRIVATE /utf-8)
target_link_libraries(SindreInstallerUI PRIVATE
    duilib
    "${NSIS_PLUGIN_API_DIR}/pluginapi-x86-unicode.lib"
    comctl32
    gdiplus
    gdi32
    user32
    ole32
    oleaut32
    shlwapi
)
if(MSVC)
    target_link_options(SindreInstallerUI PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/SindreInstallerUI.def")
endif()
set_target_properties(SindreInstallerUI PROPERTIES
    OUTPUT_NAME "SindreInstallerUI"
    PREFIX ""
)

add_custom_command(TARGET SindreInstallerUI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/skin"
        "$<TARGET_FILE_DIR:SindreInstallerUI>/skin"
)

if(SINDRE_BUILD_PREVIEW)
    add_executable(SindreInstallerPreview WIN32
        preview/main.cpp
        src/installer_window.cpp
    )
    add_dependencies(SindreInstallerPreview SindreInstallerResources)
    target_include_directories(SindreInstallerPreview PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/duilib_ultimate/DuiLib"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(SindreInstallerPreview PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601
    )
    target_compile_options(SindreInstallerPreview PRIVATE /utf-8)
    target_link_libraries(SindreInstallerPreview PRIVATE
        duilib
        comctl32
        gdiplus
        gdi32
        user32
        ole32
        oleaut32
        shlwapi
    )
    set_target_properties(SindreInstallerPreview PROPERTIES
        OUTPUT_NAME "SindreInstallerPreview"
        WIN32_EXECUTABLE TRUE
    )
    add_custom_command(TARGET SindreInstallerPreview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/skin"
            "$<TARGET_FILE_DIR:SindreInstallerPreview>/skin"
    )
endif()
