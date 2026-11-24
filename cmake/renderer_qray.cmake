if(NOT BUILD_CLIENT OR NOT BUILD_RENDERER_QRAY)
    return()
endif()

if(NOT USE_RENDERER_DLOPEN)
    message(FATAL_ERROR "BUILD_RENDERER_QRAY requires USE_RENDERER_DLOPEN=ON")
endif()

include(utils/set_output_dirs)
include(renderer_common)

set(QUAKERAY_SDK_DIR "${SOURCE_DIR}/quakeraysdk" CACHE PATH "QuakeRay SDK source directory")
if(NOT QUAKERAY_SDK_DIR OR NOT EXISTS "${QUAKERAY_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "QuakeRay SDK is missing at ${QUAKERAY_SDK_DIR}. "
        "Run `git submodule update --init --recursive`, or set QUAKERAY_SDK_DIR explicitly.")
endif()

set(QR_NVRHI_SOURCE_DIR "" CACHE PATH "NVRHI source directory")
if(NOT QR_NVRHI_SOURCE_DIR OR NOT EXISTS "${QR_NVRHI_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set -DQR_NVRHI_SOURCE_DIR=/path/to/nvrhi when BUILD_RENDERER_QRAY=ON")
endif()

# The SDK and NVRHI are static archives linked into the renderer module; keep PIC enabled
# and avoid inheriting global C IPO across the C++ ray-tracing targets.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(IOQ3_PREV_IPO ${CMAKE_INTERPROCEDURAL_OPTIMIZATION})
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

find_package(Vulkan REQUIRED)

if(NOT TARGET nvrhi)
    set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
    set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${QR_NVRHI_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/nvrhi")
endif()

if(QR_BUILD_EDITOR)
    set(QR_EDITOR_ADAPTER_SOURCES
        "${SOURCE_DIR}/rendererqray/editor/Q3EditorAdapter.cpp"
        "${SOURCE_DIR}/rendererqray/editor/Q3Bsp.cpp"
        "${SOURCE_DIR}/rendererqray/editor/Q3Model.cpp"
        "${SOURCE_DIR}/rendererqray/editor/Q3Shader.cpp"
        CACHE STRING "External editor format adapter sources" FORCE
    )
    set(QR_EDITOR_ADAPTER_INCLUDE_DIRS
        "${SOURCE_DIR}/rendererqray/editor"
        CACHE STRING "External editor format adapter include directories" FORCE
    )
    if(NOT QR_EDITOR_SHADER_DIR AND EXISTS "${QUAKERAY_SDK_DIR}/build/local-quality/editor/shaders")
        set(QR_EDITOR_SHADER_DIR
            "${QUAKERAY_SDK_DIR}/build/local-quality/editor/shaders"
            CACHE PATH "Directory with compiled QuakeRay shaders copied next to the editor" FORCE)
    endif()
else()
    set(QR_EDITOR_ADAPTER_SOURCES "" CACHE STRING "External editor format adapter sources" FORCE)
    set(QR_EDITOR_ADAPTER_INCLUDE_DIRS "" CACHE STRING "External editor format adapter include directories" FORCE)
endif()

if(QR_BUILD_TESTS)
    enable_testing()
endif()

if(NOT TARGET qray::renderer)
    add_subdirectory("${QUAKERAY_SDK_DIR}" "${CMAKE_BINARY_DIR}/qray_sdk")
endif()

set(RENDERER_QRAY_BINARY renderer_qray)

add_library(${RENDERER_QRAY_BINARY} SHARED
    ${SOURCE_DIR}/rendererqray/qray_host.cpp
    ${SOURCE_DIR}/rendererqray/qray_window.cpp
)

target_compile_features(${RENDERER_QRAY_BINARY} PRIVATE cxx_std_20)
target_include_directories(${RENDERER_QRAY_BINARY} PRIVATE
    ${SOURCE_DIR}/renderercommon
    ${SOURCE_DIR}/qcommon
    ${QUAKERAY_SDK_DIR}/Source/Stb
    ${RENDERER_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)
target_compile_definitions(${RENDERER_QRAY_BINARY} PRIVATE ${RENDERER_DEFINITIONS})
target_compile_options(${RENDERER_QRAY_BINARY} PRIVATE ${SDL2_CFLAGS_OTHER})

target_link_libraries(${RENDERER_QRAY_BINARY} PRIVATE qray::renderer ${SDL2_LIBRARIES})

if(WIN32)
    target_compile_definitions(${RENDERER_QRAY_BINARY} PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(${RENDERER_QRAY_BINARY} PRIVATE user32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(${RENDERER_QRAY_BINARY} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    set_target_properties(${RENDERER_QRAY_BINARY} PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN"
    )
    target_link_options(${RENDERER_QRAY_BINARY} PRIVATE "LINKER:--no-undefined" "LINKER:--exclude-libs,ALL")
endif()

if(MSVC)
    target_compile_options(${RENDERER_QRAY_BINARY} PRIVATE /W3 /EHsc /utf-8 /MP)
else()
    target_compile_options(${RENDERER_QRAY_BINARY} PRIVATE -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers)
endif()

set_target_properties(${RENDERER_QRAY_BINARY} PROPERTIES
    OUTPUT_NAME "renderer_qray"
    PREFIX ""
    INTERPROCEDURAL_OPTIMIZATION OFF
    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
)
set_output_dirs(${RENDERER_QRAY_BINARY})

if(WIN32 AND DEFINED QR_FFX_VK_RUNTIME_DLL AND EXISTS "${QR_FFX_VK_RUNTIME_DLL}")
    add_custom_command(TARGET ${RENDERER_QRAY_BINARY} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QR_FFX_VK_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:${RENDERER_QRAY_BINARY}>/amd_fidelityfx_vk.dll"
    )
elseif(NOT WIN32 AND DEFINED QR_FFX_VK_SO AND EXISTS "${QR_FFX_VK_SO}")
    add_custom_command(TARGET ${RENDERER_QRAY_BINARY} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QR_FFX_VK_SO}"
            "$<TARGET_FILE_DIR:${RENDERER_QRAY_BINARY}>/libamd_fidelityfx_vk.so"
    )
endif()

if(QR_BUILD_EDITOR AND QR_BUILD_TESTS)
    add_executable(qray_q3_editor_adapter_tests
        ${SOURCE_DIR}/rendererqray/editor/Q3AdapterTests.cpp
        ${SOURCE_DIR}/rendererqray/editor/Q3EditorAdapter.cpp
        ${SOURCE_DIR}/rendererqray/editor/Q3Bsp.cpp
        ${SOURCE_DIR}/rendererqray/editor/Q3Model.cpp
        ${SOURCE_DIR}/rendererqray/editor/Q3Shader.cpp
        ${QUAKERAY_SDK_DIR}/editor/Assets.cpp
        ${QUAKERAY_SDK_DIR}/editor/Scene.cpp
        ${QUAKERAY_SDK_DIR}/editor/EditorSession.cpp
        ${QUAKERAY_SDK_DIR}/Source/Stb/stb_image.cpp
    )
    target_compile_features(qray_q3_editor_adapter_tests PRIVATE cxx_std_20)
    target_include_directories(qray_q3_editor_adapter_tests PRIVATE
        ${SOURCE_DIR}/rendererqray/editor
        ${QUAKERAY_SDK_DIR}/editor
        ${QUAKERAY_SDK_DIR}/Include
        ${QUAKERAY_SDK_DIR}/Source/Stb
    )
    if(MSVC)
        target_compile_options(qray_q3_editor_adapter_tests PRIVATE /W3 /EHsc /utf-8 /MP)
    else()
        target_compile_options(qray_q3_editor_adapter_tests PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
        set_source_files_properties(${QUAKERAY_SDK_DIR}/editor/EditorSession.cpp
            TARGET_DIRECTORY qray_q3_editor_adapter_tests
            PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")
    endif()
    set_target_properties(qray_q3_editor_adapter_tests PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF
        INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
    )
    set_output_dirs(qray_q3_editor_adapter_tests)
    add_test(NAME qray_q3_editor_adapter_tests COMMAND qray_q3_editor_adapter_tests)
    set_tests_properties(qray_q3_editor_adapter_tests PROPERTIES TIMEOUT 300)
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${IOQ3_PREV_IPO})
