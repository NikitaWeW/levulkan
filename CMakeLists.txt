cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(levulkan C CXX)

include(cpm.cmake)

# Setup assimp.
set(ASSIMP_NO_EXPORT ON)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
foreach(LOADER FBX OBJ GLTF)
    set(ASSIMP_BUILD_${LOADER}_IMPORTER ON)
endforeach()
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)
set(ASSIMP_WARNINGS_AS_ERRORS OFF)

# set(CPM_USE_LOCAL_PACKAGES ON)
CPMAddPackage("https://codeberg.org/nikitam/nicecs.git#c2b2f0f")
CPMAddPackage("gh:assimp/assimp@6.0.2")
CPMAddPackage("gh:gabime/spdlog@1.16.0")
CPMAddPackage("gh:g-truc/glm#1.0.2")

CPMAddPackage("gh:glfw/glfw#3.4")
CPMAddPackage("gh:KhronosGroup/Vulkan-Headers@1.4.338")
CPMAddPackage("gh:zeux/volk#1.4.304")

set(LEVULKAN_SOURCE
    "src/main.cpp"
    "src/libraries/stb.c"
)

add_executable(levulkan ${LEVULKAN_SOURCE})
message(STATUS "Packages: ${CPM_PACKAGES}")
target_link_libraries(levulkan PRIVATE spdlog nicecs::ecs glm assimp)
target_link_libraries(levulkan PRIVATE glfw Vulkan::Headers volk)
target_include_directories(levulkan PRIVATE "src")

install(TARGETS levulkan DESTINATION .)
