cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
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

set(CPM_USE_LOCAL_PACKAGES ON)
CPMAddPackage("gh:nikitawew/nicecs#c2b2f0f")
CPMAddPackage("gh:glfw/glfw#3.4")
CPMAddPackage("gh:assimp/assimp@6.0.2")
CPMAddPackage("gh:g-truc/glm#1.0.2")
CPMAddPackage("gh:KhronosGroup/Vulkan-Headers@1.4.338")
CPMAddPackage("gh:KhronosGroup/Vulkan-Loader@1.4.338")

set(LEVULKAN_SOURCE
    "src/main.cpp"
)

add_executable(levulkan ${LEVULKAN_SOURCE})
target_link_libraries(levulkan PRIVATE nicecs::ecs glfw glm assimp Vulkan::Headers Vulkan::Loader)

install(TARGETS levulkan DESTINATION .)
