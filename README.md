![image](lefishe.jpg)

# Learning vulkan

Yeah...

Running on hopes and dreams.

## Building 

The project uses cmake with cpm.cmake.
```shell
cd levulkan
cmake -S . -B build
cmake --build build
build/levulkan # profit
```

You can also set `CPM_USE_LOCAL_PACKAGES=ON` to speed up the process

### Requirements
- Cmake
- Build system (e.g. makefiles, visual studio, ninja, etc.)
- C++ 20 compiler
- Validation layers (VK_LAYER_KHRONOS_validation) installed for validation
- All the packages are managed by cpm.cmake

## Resources I use

- [How to vulkan](https://howtovulkan.com)
- [Vulkan docs](https://docs.vulkan.org)
- [OGLDEV's vulkan tutorials](https://youtube.com/watch?v=EsEP9iJKBhU&list=PLA0dXqQjCx0RntJy1pqje9uHRF1Z5vZgA)
- [Sasha Willems' vulkan samples](https://github.com/SaschaWillems/Vulkan)
