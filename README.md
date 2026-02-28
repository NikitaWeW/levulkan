![image](lefishe.jpg)

# Learning vulkan

yeah

## Building 

The project uses cmake with cpm.cmake.
```shell
cd levulkan
cmake -S . -B build
cmake --build build
build/levulkan # profit
```

### Requirements
- Most of the packages are managed by cpm.cmake
- Cmake
- Build system (e.g. makefiles, visual studio, ninja, etc.)
- C++ 20 compiler
- Validation layers (VK_LAYER_KHRONOS_validation) installed for validation
- [shaderc](https://github.com/google/shaderc/) installed and available for cmake.

## Resources I use

- [How to vulkan](https://howtovulkan.com)
- [Vulkan docs](https://docs.vulkan.org)
- [OGLDEV's vulkan tutorials](https://youtube.com/watch?v=EsEP9iJKBhU&list=PLA0dXqQjCx0RntJy1pqje9uHRF1Z5vZgA)
- [Sasha Willems' vulkan samples](https://github.com/SaschaWillems/Vulkan)
