Vertex Attributes (Not interleaved)
---

Glsl:
```glsl
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;
```

Vulkan:
```c++
// Bindings
const std::array<VkVertexInputBindingDescription, 4> vertexInputBindings = {
    VkVertexInputBindingDescription{ 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
    VkVertexInputBindingDescription{ 1, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX },
    VkVertexInputBindingDescription{ 2, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
    VkVertexInputBindingDescription{ 3, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
};

// Attributes
const std::array<VkVertexInputAttributeDescription, 4> vertexInputAttributes = {
    VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
    VkVertexInputAttributeDescription{ 1, 2, VK_FORMAT_R32G32_SFLOAT, 0 },
    VkVertexInputAttributeDescription{ 2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0 },
    VkVertexInputAttributeDescription{ 3, 3, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
};
```

Descriptor set
---

| binding |           description           |
|  :---:  |               ---               |
|    0    | array of 2d textures            |
|    1    | uniform buffer of matrices      |
|    2    | uniform buffer of lighting data |
