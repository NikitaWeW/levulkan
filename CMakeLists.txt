cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(levulkan C CXX)

include(cpm.cmake)

# Setup assimp.
set(ASSIMP_NO_EXPORT ON)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
foreach(LOADER GLTF)
    set(ASSIMP_BUILD_${LOADER}_IMPORTER ON)
endforeach()
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)
set(ASSIMP_WARNINGS_AS_ERRORS OFF)
set(BUILD_SHARED_LIBS OFF)

CPMAddPackage("gh:nikitawew/nicecs#06df83d")
CPMAddPackage("gh:assimp/assimp@6.0.2")
CPMAddPackage("gh:zeux/meshoptimizer@1.0")
CPMAddPackage("gh:gabime/spdlog@1.17.0")
CPMAddPackage("gh:g-truc/glm#1.0.2")

CPMAddPackage("gh:glfw/glfw#3.4")
CPMAddPackage("gh:zeux/volk#1.4.304")
CPMAddPackage("gh:GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator@3.3.0")

find_package(glslang)
if(NOT glslang_FOUND)
    message(FATAL "glslang not found. Make sure it is installed and available for cmake.")
else()
    message(STATUS "Found glslang: \"${glslang_DIR}\"")
endif()

set(LEVULKAN_SOURCE
    "src/main.cpp"
    "src/libraries/stb.c"
	"src/ModelLoader.cpp"
	"src/TextureLoader.cpp"
	"src/Controller.cpp"
)

add_executable(levulkan ${LEVULKAN_SOURCE})
target_link_libraries(levulkan PRIVATE spdlog nicecs::ecs glm assimp meshoptimizer)
target_link_libraries(levulkan PRIVATE glfw volk GPUOpen::VulkanMemoryAllocator glslang::glslang glslang::glslang-default-resource-limits)
target_include_directories(levulkan PRIVATE "src")
target_compile_definitions(levulkan PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_ENABLE_EXPERIMENTAL)
