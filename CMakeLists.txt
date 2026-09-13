cmake_minimum_required(VERSION 3.20)

project(openfbx VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENFBX_SOURCES
    src/main.cpp
    src/editor/OpenFbxApp.cpp
    src/editor/OpenFbxApp.h
    src/editor/AppCore.inl
    src/editor/RenderingMaterials.inl
    src/editor/SelectionHierarchyOps.inl
    src/editor/AnimationPlayback.inl
    src/editor/ViewportDrawing.inl
    src/editor/TransformSkinningOps.inl
    src/editor/ViewportPicking.inl
    src/editor/TransformGizmoUi.inl
    src/editor/AppUiUtilities.inl
    src/editor/HierarchyAndValidation.inl
    src/editor/MaterialsPanel.inl
    src/editor/UvPanel.inl
    src/editor/SkinWeightsPanel.inl
    src/editor/MenuTabsTimeline.inl
    src/editor/OpenFbxAppMain.inl
    src/core/MathUtils.cpp
    src/core/MathUtils.h
    src/FbxLoader.cpp
    src/FbxLoader.h
    src/geometry/MeshTangents.cpp
    src/geometry/DeferredSkinning.cpp
    src/geometry/DeferredSkinning.h
    src/geometry/MeshTangents.h
    src/platform/FileDialogs.cpp
    src/platform/FileDialogs.h
    src/platform/ModelConversion.h
    src/platform/BlenderConversion.cpp
    src/ui/UiPrimitives.cpp
    src/ui/UiPrimitives.h
    src/viewport/OrbitCamera.cpp
    src/viewport/OrbitCamera.h
)

if(WIN32)
    list(APPEND OPENFBX_SOURCES src/openfbx.rc)
endif()

add_executable(openfbx ${OPENFBX_SOURCES})
target_compile_definitions(openfbx PRIVATE OPENFBX_VERSION="${PROJECT_VERSION}")

if(WIN32)
    set_target_properties(openfbx PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(openfbx PRIVATE /ENTRY:mainCRTStartup)
    endif()

    add_custom_command(TARGET openfbx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/open_fbx_icon.png"
                "$<TARGET_FILE_DIR:openfbx>/open_fbx_icon.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/checker.png"
                "$<TARGET_FILE_DIR:openfbx>/checker.png"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/colored_checkers"
                "$<TARGET_FILE_DIR:openfbx>/colored_checkers"
        COMMENT "Copying openfbx window icon and checker textures"
    )
endif()

set(FBXSDK_ROOT "" CACHE PATH "Autodesk FBX SDK install root, for example C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.7")

if(NOT FBXSDK_ROOT AND DEFINED ENV{FBXSDK_ROOT})
    set(FBXSDK_ROOT "$ENV{FBXSDK_ROOT}" CACHE PATH "Autodesk FBX SDK install root" FORCE)
endif()

if(NOT FBXSDK_ROOT AND DEFINED ENV{FBXSDK_DIR})
    set(FBXSDK_ROOT "$ENV{FBXSDK_DIR}" CACHE PATH "Autodesk FBX SDK install root" FORCE)
endif()

if(NOT FBXSDK_ROOT)
    message(FATAL_ERROR "Set FBXSDK_ROOT to your Autodesk FBX SDK install directory.")
endif()

set(FBXSDK_INCLUDE_DIR "${FBXSDK_ROOT}/include" CACHE PATH "Autodesk FBX SDK include directory")

set(_fbxsdk_arch_dir "x64")
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_fbxsdk_arch_dir "x86")
endif()

set(_fbxsdk_vs_dirs
    "${FBXSDK_ROOT}/lib/${_fbxsdk_arch_dir}"
    "${FBXSDK_ROOT}/lib/vs2022/${_fbxsdk_arch_dir}"
    "${FBXSDK_ROOT}/lib/vs2019/${_fbxsdk_arch_dir}"
    "${FBXSDK_ROOT}/lib/vs2017/${_fbxsdk_arch_dir}"
)

find_path(FBXSDK_LIBRARY_DIR
    NAMES release/libfbxsdk-md.lib debug/libfbxsdk-md.lib libfbxsdk-md.lib
    HINTS ${_fbxsdk_vs_dirs}
    DOC "Autodesk FBX SDK library directory containing release/ and debug/, or the library files directly"
)

if(NOT FBXSDK_LIBRARY_DIR)
    message(FATAL_ERROR "Could not find FBX SDK libraries. Set FBXSDK_LIBRARY_DIR manually.")
endif()

function(find_fbxsdk_config_lib out_var config_dir lib_name)
    find_library(${out_var}
        NAMES ${lib_name}
        HINTS
            "${FBXSDK_LIBRARY_DIR}/${config_dir}"
            "${FBXSDK_LIBRARY_DIR}"
        NO_DEFAULT_PATH
    )
    if(NOT ${out_var})
        message(FATAL_ERROR "Could not find ${lib_name} in ${FBXSDK_LIBRARY_DIR}/${config_dir} or ${FBXSDK_LIBRARY_DIR}")
    endif()
    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

find_fbxsdk_config_lib(FBXSDK_LIB_RELEASE release libfbxsdk-md)
find_fbxsdk_config_lib(FBXXML_LIB_RELEASE release libxml2-md)
find_fbxsdk_config_lib(FBXZLIB_LIB_RELEASE release zlib-md)

find_fbxsdk_config_lib(FBXSDK_LIB_DEBUG debug libfbxsdk-md)
find_fbxsdk_config_lib(FBXXML_LIB_DEBUG debug libxml2-md)
find_fbxsdk_config_lib(FBXZLIB_LIB_DEBUG debug zlib-md)

target_include_directories(openfbx PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${FBXSDK_INCLUDE_DIR}"
)
target_link_libraries(openfbx PRIVATE
    optimized "${FBXSDK_LIB_RELEASE}"
    optimized "${FBXXML_LIB_RELEASE}"
    optimized "${FBXZLIB_LIB_RELEASE}"
    debug "${FBXSDK_LIB_DEBUG}"
    debug "${FBXXML_LIB_DEBUG}"
    debug "${FBXZLIB_LIB_DEBUG}"
)

if(WIN32)
    target_link_libraries(openfbx PRIVATE bcrypt winmm comdlg32 ole32 uuid)
endif()

if(EXISTS "${FBXSDK_ROOT}/bin/${_fbxsdk_arch_dir}")
    set(_fbxsdk_runtime_config "$<IF:$<CONFIG:Debug>,debug,release>")
    add_custom_command(TARGET openfbx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${FBXSDK_ROOT}/bin/${_fbxsdk_arch_dir}/${_fbxsdk_runtime_config}"
                "$<TARGET_FILE_DIR:openfbx>"
        COMMENT "Copying FBX SDK runtime DLLs"
    )
endif()

find_package(raylib CONFIG QUIET)
if(NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(raylib_prebuilt
        URL https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_win64_msvc16.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(raylib_prebuilt)

    add_library(raylib SHARED IMPORTED GLOBAL)
    set_target_properties(raylib PROPERTIES
        IMPORTED_IMPLIB "${raylib_prebuilt_SOURCE_DIR}/lib/raylib.lib"
        IMPORTED_LOCATION "${raylib_prebuilt_SOURCE_DIR}/lib/raylib.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${raylib_prebuilt_SOURCE_DIR}/include"
    )

    add_custom_command(TARGET openfbx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:raylib>"
                "$<TARGET_FILE_DIR:openfbx>"
        COMMENT "Copying raylib runtime DLL"
    )
endif()

target_link_libraries(openfbx PRIVATE raylib)

option(OPENFBX_BUILD_TESTS "Build headless editor regression tests" OFF)
if(OPENFBX_BUILD_TESTS)
    enable_testing()
    set(OPENFBX_TEST_SOURCES ${OPENFBX_SOURCES})
    list(REMOVE_ITEM OPENFBX_TEST_SOURCES src/main.cpp src/editor/OpenFbxApp.cpp src/openfbx.rc)
    add_executable(openfbx_transform_tests tests/TransformPerformance.cpp ${OPENFBX_TEST_SOURCES})
    target_include_directories(openfbx_transform_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" "${FBXSDK_INCLUDE_DIR}")
    get_target_property(OPENFBX_TEST_LIBRARIES openfbx LINK_LIBRARIES)
    target_link_libraries(openfbx_transform_tests PRIVATE ${OPENFBX_TEST_LIBRARIES})
    add_executable(openfbx_conversion_tests tests/ModelConversion.cpp ${OPENFBX_TEST_SOURCES})
    target_include_directories(openfbx_conversion_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" "${FBXSDK_INCLUDE_DIR}")
    target_link_libraries(openfbx_conversion_tests PRIVATE ${OPENFBX_TEST_LIBRARIES})
    add_test(NAME model_conversion_regression COMMAND openfbx_conversion_tests)
    add_test(NAME transform_regression COMMAND openfbx_transform_tests)
endif()

if(MSVC)
    target_compile_options(openfbx PRIVATE /W4 /permissive-)
endif()
