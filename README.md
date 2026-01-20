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
- Cmake, build system and a c++20 compiler of your choice.
- Validation layers (VK_LAYER_KHRONOS_validation) installed for validation.
- shader compiler (set the SHADER_COMPILER option to use a custom slang compiler)

## TODO
- set up [vma](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- dynamic rendering

