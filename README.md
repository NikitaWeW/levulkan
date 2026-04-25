![image](lefishe.jpg)

# Learning vulkan

Yeah...

A lot of stuff is going to be merged into my engine.

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
- Build system (e.g. unix makefiles, visual studio, ninja, etc.)
- C++ 20 compiler
- Validation layers (VK_LAYER_KHRONOS_validation) installed for validation
- All the packages are managed by cpm.cmake, so no dependency setup is required

## Resources I use

- [How to vulkan](https://howtovulkan.com)
- [Vulkan docs](https://docs.vulkan.org)
- [Sasha Willems' vulkan samples](https://github.com/SaschaWillems/Vulkan)
