![image](diagram.png)

# Game engine? 

Yeah...

- Is a game engine.
- Sucks ass.
- ECS driven.
- Vulkan only.
- Has too many gifs


## How to use

- Probably don't

![image](https://media1.tenor.com/m/avEEZ4V8XcEAAAAd/cat-excited.gif)
![image](https://static2.klipy.com/ii/bea85337777ad0e23e63683391435543/ee/ac/YeFqPso5.gif)

## Features

![image](https://media1.tenor.com/m/b4LEpOGvktEAAAAd/the-square-hole-square-hole.gif)
![image](https://media.tenor.com/ihzUC8LA1CgAAAAj/komik.gif)

- ECS
- GLSL and Slang Shader compilation and binary management.
- Pipeline and descriptor set management with shader reflection.
  - Automatic descriptor updates.
- GPU ring buffer.
- Vulkan render graph.
- A virtual filesystem
- Probably something else but I forgot the project has a readme.

## Building 

![image](https://media1.tenor.com/m/3EAL8JxXr04AAAAd/cat-vibe-cat.gif)

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
- Glslc / Slangc compiler.
- Validation layers (VK_LAYER_KHRONOS_validation) installed for validation.
- Everything else is managed by cpm.cmake, so no dependency setup is required.

## Cool vulkan resources

- [How to vulkan](https://howtovulkan.com)
- [Vulkan docs](https://docs.vulkan.org)
- [Sasha Willems' vulkan samples](https://github.com/SaschaWillems/Vulkan)
