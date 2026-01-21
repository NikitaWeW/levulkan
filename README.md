![image](lefishe.jpg)

# Learning vulkan

yeah

## Building 

The project uses cmake with cpm.cmake.
```shell
cd levulkan
cmake -S . -B build
cmake --build build
cmake --install build --prefix install
install/levulkan # profit
```
Vulkan loader and headers are included.

### Requirements
- Cmake
- Build system (e.g. makefiles, visual studio, ninja, etc.)
- C++20 compiler.
- Validation layers (VK_LAYER_KHRONOS_validation) installed for validation.
- GLSL shader compiler (set the SHADER_COMPILER variable to use a custom compiler), default: glslc

## TODO
- finish [vulkan tutorial](https://vulkan-tutorial.com)
- set up [vma](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- dynamic rendering

