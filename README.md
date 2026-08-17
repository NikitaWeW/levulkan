![image](lefishe.jpg)

# Learning vulkan

Yeah...

A lot of stuff is going to be merged into my engine once I finish with this.

## How to use

![image](https://tenor.com/view/cat-excited-happy-excited-cat-jumping-cat-gif-7705945280075292097)

- Probably don't

## Features

![image](https://tenor.com/view/komik-gif-9952062423559099432)

- Device, swapchain and other stuff initialization. (src/vk/Init.hpp)
- GLSL Shader compilation and binary management with custom #stage parser/splitter. (src/vk/Shader.hpp)
- Pipeline and descriptor set management with shader reflection. (src/vk/Pipeline.hpp)
- GPU ring buffer. Useful for frequently updated data. (src/vk/RingBuffer.hpp)
- Vulkan render graph. (src/vk/RenderGraph.hpp)
- A fucking virtual filesystem??
- Probably something else but I forgot the project has a readme.

## Building 

![image](https://tenor.com/view/cat-vibe-cat-dancing-cat-cat-meme-gif-15870698214896676686)

```shell
cd levulkan
cmake -B build
cmake --build build
build/levulkan # profit
```
*You can also set `CPM_USE_LOCAL_PACKAGES=ON` to speed up the process*

### Requirements
- Cmake.
- Build system (e.g. unix makefiles, visual studio, ninja, etc.).
- C++ 20 compiler.
- Validation layers (VK_LAYER_KHRONOS_validation) installed for validation.
- Everything else is managed by cpm.cmake, so no dependency setup is required.

## Cool vulkan resources

- [How to vulkan](https://howtovulkan.com)
- [Vulkan docs](https://docs.vulkan.org)
- [Sasha Willems' vulkan samples](https://github.com/SaschaWillems/Vulkan)
