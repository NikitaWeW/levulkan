cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(levulkan C CXX)

include(cpm.cmake)

foreach(LOADER GLTF)
    set(ASSIMP_BUILD_${LOADER}_IMPORTER ON)
endforeach()
set(BUILD_SHARED_LIBS OFF)

CPMAddPackage("gh:nikitawew/nicecs#06df83d")
CPMAddPackage(
    NAME assimp
    VERSION 6.0.2
    GITHUB_REPOSITORY "assimp/assimp"
    OPTIONS "ASSIMP_NO_EXPORT ON" 
            "ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF" 
            "ASSIMP_BUILD_TESTS OFF" 
            "ASSIMP_INSTALL OFF" 
            "ASSIMP_WARNINGS_AS_ERRORS OFF"
)
CPMAddPackage(
    NAME meshoptimizer
    VERSION 1.0
    GITHUB_REPOSITORY "zeux/meshoptimizer"
)
CPMAddPackage(
    URI "gh:gabime/spdlog@1.17.0"
    FORCE TRUE
)
CPMAddPackage(
    NAME glm
    VERSION 1.0.2
    GIT_TAG 1.0.2
    GITHUB_REPOSITORY "g-truc/glm"
)
CPMAddPackage(
    NAME glfw
    VERSION 3.4 
    GIT_TAG 3.4
    GITHUB_REPOSITORY "glfw/glfw"
)
CPMAddPackage(
    NAME
    VERSION 3.3.0
    GITHUB_REPOSITORY "GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator"
)
CPMAddPackage(
    NAME volk
    VERSION 1.4.304
    GIT_TAG 1.4.304
    GITHUB_REPOSITORY "zeux/volk"
)
CPMAddPackage(
    NAME spirv-reflect
    GIT_TAG vulkan-sdk-1.4.341.0
    GITHUB_REPOSITORY "KhronosGroup/Spirv-Reflect"
    OPTIONS "SPIRV_REFLECT_STATIC_LIB ON"
            "SPIRV_REFLECT_EXECUTABLE OFF"
            "SPIRV_REFLECT_INSTALL OFF"
)

find_package(glslang)
if(NOT glslang_FOUND)
    CPMAddPackage(
        NAME spirv-headers
        GIT_TAG vulkan-sdk-1.4.341.0
        GITHUB_REPOSITORY "KhronosGroup/Spirv-Headers"
    )
    CPMAddPackage(
        NAME spriv-tools
        VERSION 2026.1
        GITHUB_REPOSITORY "KhronosGroup/Spirv-Tools"
    )
    CPMAddPackage(
        NAME glslang
        VERSION 16.2.0
        GIT_TAG 16.2.0
        GITHUB_REPOSITORY "KhronosGroup/glslang"
    )
else()
    message(STATUS "Found glslang: \"${glslang_DIR}\"")
endif()

set(LEVULKAN_SOURCE
    "src/main.cpp"
    "src/libraries/stb.c"
    "src/libraries/vk_mem_alloc.cpp"

    "src/resource/ModelLoader.cpp"
    "src/resource/TextureLoader.cpp"

    "src/vk/Init.cpp"
    "src/vk/Pipeline.cpp"
    "src/vk/Shader.cpp"
)

add_executable(levulkan ${LEVULKAN_SOURCE})
target_link_libraries(levulkan PRIVATE spdlog nicecs::ecs glm assimp meshoptimizer)
target_link_libraries(levulkan PRIVATE glfw volk GPUOpen::VulkanMemoryAllocator glslang::glslang glslang::glslang-default-resource-limits spirv-reflect-static)
target_include_directories(levulkan PRIVATE "src")
target_compile_definitions(levulkan PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_ENABLE_EXPERIMENTAL)
