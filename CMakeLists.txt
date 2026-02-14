cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(levulkan C CXX)

if(NOT DEFINED SHADER_COMPILER)
set(SHADER_COMPILER "slangc" CACHE STRING "The path to the shader compiler")
endif()

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
# TODO: stack traces
# CPMAddPackage("gh:boostorg/stacktrace#boost-1.90.0")

CPMAddPackage("gh:glfw/glfw#3.4")
CPMAddPackage("gh:KhronosGroup/Vulkan-Headers@1.4.338")
CPMAddPackage("gh:zeux/volk#1.4.304")
CPMAddPackage("gh:GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator@3.3.0")
# TODO: glsl and at runtime
# CPMAddPackage("gh:google/shaderc@2026.1")

set(LEVULKAN_SOURCE
    "src/main.cpp"
    "src/libraries/stb.c"
	"src/ModelLoader.cpp"
	"src/TextureLoader.cpp"
)

add_executable(levulkan ${LEVULKAN_SOURCE})
target_link_libraries(levulkan PRIVATE spdlog nicecs::ecs glm assimp meshoptimizer)
target_link_libraries(levulkan PRIVATE glfw Vulkan::Headers volk GPUOpen::VulkanMemoryAllocator)
target_include_directories(levulkan PRIVATE "src")

install(TARGETS levulkan DESTINATION .)

set(SHADERS_IN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADERS_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders-bin")

file(GLOB SHADERS "${SHADERS_IN_DIR}/*.slang")

file(MAKE_DIRECTORY ${SHADERS_OUT_DIR})

# HACK
foreach(SHADER ${SHADERS})
	get_filename_component(SHADER_NAME ${SHADER} NAME)
	set(SHADER_OUT_NAME "${SHADERS_OUT_DIR}/${SHADER_NAME}.spv")
	list(APPEND SHADER_OUT_NAMES ${SHADER_OUT_NAME})
	add_custom_command(
		MAIN_DEPENDENCY ${SHADER}
		OUTPUT ${SHADER_OUT_NAME}
		COMMAND ${SHADER_COMPILER} ${SHADER} "-o" ${SHADER_OUT_NAME} "-target" "spirv"
		VERBATIM)
endforeach()

add_custom_target(build_shaders DEPENDS ${SHADER_OUT_NAMES})

add_dependencies(levulkan build_shaders)
