cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(levulkan C CXX)

include(cmake/cpm.cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(BUILD_SHARED_LIBS OFF)

foreach(LOADER GLTF)
    set(ASSIMP_BUILD_${LOADER}_IMPORTER ON)
endforeach()

set(SPIRV_REFLECT_STATIC_LIB ON)
set(SPIRV_REFLECT_EXECUTABLE OFF)
set(SPIRV_REFLECT_INSTALL OFF)

set(ASSIMP_NO_EXPORT ON)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)
set(ASSIMP_WARNINGS_AS_ERRORS OF)

set(ENABLE_COMMONCRYPTO OFF)
set(ENABLE_GNUTLS OFF)
set(ENABLE_WINDOWS_CRYPTO OFF)
set(LIBZIP_DO_INSTALL OFF)
set(ENABLE_ZSTD OFF)

CPMAddPackage(
    NAME assimp
    VERSION 6.0.2
    GITHUB_REPOSITORY "assimp/assimp"
)
CPMAddPackage(
    NAME glfw
    VERSION 3.4 
    GIT_TAG 3.4
    GITHUB_REPOSITORY "glfw/glfw"
)
CPMAddPackage("gh:nikitawew/nicecs#fd857895")
CPMAddPackage("gh:zeux/meshoptimizer@1.0")
CPMAddPackage(URI "gh:gabime/spdlog@1.17.0" FORCE ON)
CPMAddPackage("gh:g-truc/glm#1.0.2")
CPMAddPackage("gh:GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator@3.3.0")
CPMAddPackage("gh:zeux/volk@1.4.304")
CPMAddPackage("gh:KhronosGroup/Spirv-Reflect#vulkan-sdk-1.4.341.0")
CPMAddPackage("gh:jeremy-rifkin/cpptrace#v1.0.4")
CPMAddPackage("gh:nlohmann/json#v3.12.0")
CPMAddPackage("gh:KhronosGroup/Spirv-Headers#vulkan-sdk-1.4.341.0")
CPMAddPackage("gh:nih-at/libzip#4cd7310")

find_package(SPIRV-Tools)
if(NOT SPIRV-Tools_FOUND)
CPMAddPackage("gh:KhronosGroup/Spirv-Tools@2026.1")
else()
    message(STATUS "Found spirv tools: \"${SPIRV-Tools_DIR}\"")
endif()

find_package(glslang)
if(NOT glslang_FOUND)
    CPMAddPackage("gh:KhronosGroup/glslang#16.2.0")
else()
    message(STATUS "Found glslang: \"${glslang_DIR}\"")
endif()

find_package(slang)
message(STATUS "Found slang: \"${slang_DIR}\"")

set(ENGINE_SOURCE
    "src/Renderer.cpp"
    "src/Controller.cpp"
    "src/libraries/stb.c"
    "src/libraries/vk_mem_alloc.cpp"

    "src/resource/ModelLoader.cpp"
    "src/resource/TextureLoader.cpp"

    "src/fs/Path.cpp"
    "src/fs/File.cpp"
    "src/fs/NativeFilesystem.cpp"
    "src/fs/ArchiveFilesystem.cpp"

    "src/vk/Init.cpp"
    "src/vk/Pipeline.cpp"
    "src/vk/Resource.cpp"
    "src/vk/RingBuffer.cpp"
    "src/vk/Shader.cpp"
    "src/vk/Swapchain.cpp"
    "src/vk/RenderGraph.cpp"
)
add_library(engine ${ENGINE_SOURCE})
target_link_libraries(engine PUBLIC spdlog nicecs::ecs glm assimp meshoptimizer cpptrace::cpptrace nlohmann_json::nlohmann_json zip)
target_link_libraries(engine PUBLIC glfw volk GPUOpen::VulkanMemoryAllocator)
target_link_libraries(engine PUBLIC glslang::glslang glslang::glslang-default-resource-limits spirv-reflect-static SPIRV-Headers SPIRV-Tools slang::slang)
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_ENABLE_EXPERIMENTAL)
target_include_directories(engine PRIVATE "src")

if(glslang_FOUND)
target_compile_definitions(engine PRIVATE SHADER_ENABLE_GLSL)
endif()
if(slang_FOUND)
target_compile_definitions(engine PRIVATE SHADER_ENABLE_SLANG)
endif()

add_executable(levulkan "src/main.cpp")
target_link_libraries(levulkan PRIVATE engine)
target_include_directories(levulkan PRIVATE "src")

add_subdirectory(tests)

