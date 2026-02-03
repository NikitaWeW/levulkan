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
- GLSL shader compiler (set the SHADER_COMPILER variable to use a custom path), default: slangc

## Resources I use

- [How to vulkan](https://howtovulkan.com)
- [Vulkan docs](https://docs.vulkan.org)
- [OGLDEV's vulkan tutorials](https://youtube.com/watch?v=EsEP9iJKBhU&list=PLA0dXqQjCx0RntJy1pqje9uHRF1Z5vZgA)
- [Sasha Willems' vulkan samples](https://github.com/SaschaWillems/Vulkan)
