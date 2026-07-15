#include "Pipeline.hpp"
#include "Utility.hpp"
#include "spirv_reflect.h"
#include "Logging.hpp"

using namespace vk;

static constexpr std::string string_SpvReflectResult(SpvReflectResult res)
{
    switch(res) {
        case SPV_REFLECT_RESULT_SUCCESS                                    : return "SPV_REFLECT_RESULT_SUCCESS";                                   
        case SPV_REFLECT_RESULT_NOT_READY                                  : return "SPV_REFLECT_RESULT_NOT_READY";                                 
        case SPV_REFLECT_RESULT_ERROR_PARSE_FAILED                         : return "SPV_REFLECT_RESULT_ERROR_PARSE_FAILED";                        
        case SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED                         : return "SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED";                        
        case SPV_REFLECT_RESULT_ERROR_RANGE_EXCEEDED                       : return "SPV_REFLECT_RESULT_ERROR_RANGE_EXCEEDED";                      
        case SPV_REFLECT_RESULT_ERROR_NULL_POINTER                         : return "SPV_REFLECT_RESULT_ERROR_NULL_POINTER";                        
        case SPV_REFLECT_RESULT_ERROR_INTERNAL_ERROR                       : return "SPV_REFLECT_RESULT_ERROR_INTERNAL_ERROR";                      
        case SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH                       : return "SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH";                      
        case SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND                    : return "SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND";                   
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_CODE_SIZE              : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_CODE_SIZE";             
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_MAGIC_NUMBER           : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_MAGIC_NUMBER";          
        case SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_EOF                 : return "SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_EOF";                
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ID_REFERENCE           : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ID_REFERENCE";          
        case SPV_REFLECT_RESULT_ERROR_SPIRV_SET_NUMBER_OVERFLOW            : return "SPV_REFLECT_RESULT_ERROR_SPIRV_SET_NUMBER_OVERFLOW";           
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_STORAGE_CLASS          : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_STORAGE_CLASS";         
        case SPV_REFLECT_RESULT_ERROR_SPIRV_RECURSION                      : return "SPV_REFLECT_RESULT_ERROR_SPIRV_RECURSION";                     
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_INSTRUCTION            : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_INSTRUCTION";           
        case SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_BLOCK_DATA          : return "SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_BLOCK_DATA";         
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_BLOCK_MEMBER_REFERENCE : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_BLOCK_MEMBER_REFERENCE";
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ENTRY_POINT            : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ENTRY_POINT";           
        case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_EXECUTION_MODE         : return "SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_EXECUTION_MODE";        
        case SPV_REFLECT_RESULT_ERROR_SPIRV_MAX_RECURSIVE_EXCEEDED         : return "SPV_REFLECT_RESULT_ERROR_SPIRV_MAX_RECURSIVE_EXCEEDED";        
        default: return "Unhandled SpvReflectResult";
    };
}

#define SPV_CHK(x, name, action) { SpvReflectResult _result = x; if(_result != SPV_REFLECT_RESULT_SUCCESS) { LOG_ERROR("{}:{}: Failed to {}: {} for binary \"{}\".", __FILE__, __LINE__, #x, string_SpvReflectResult(_result), name); action; } }
static constexpr VkDescriptorType toVulkanDescriptorType(SpvReflectDescriptorType const &type)
{
    switch(type)
    {
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:                    return VK_DESCRIPTOR_TYPE_SAMPLER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:     return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:              return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:              return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:       return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:       return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:             return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:             return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:     return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:     return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:           return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        default: return(VkDescriptorType) 0;
    }
}
static constexpr VkFormat toVulkanFormat(SpvReflectFormat const &format)
{
    switch(format)
    {
        case SPV_REFLECT_FORMAT_UNDEFINED:           return VK_FORMAT_UNDEFINED;          
        case SPV_REFLECT_FORMAT_R16_UINT:            return VK_FORMAT_R16_UINT;           
        case SPV_REFLECT_FORMAT_R16_SINT:            return VK_FORMAT_R16_SINT;           
        case SPV_REFLECT_FORMAT_R16_SFLOAT:          return VK_FORMAT_R16_SFLOAT;         
        case SPV_REFLECT_FORMAT_R16G16_UINT:         return VK_FORMAT_R16G16_UINT;        
        case SPV_REFLECT_FORMAT_R16G16_SINT:         return VK_FORMAT_R16G16_SINT;        
        case SPV_REFLECT_FORMAT_R16G16_SFLOAT:       return VK_FORMAT_R16G16_SFLOAT;      
        case SPV_REFLECT_FORMAT_R16G16B16_UINT:      return VK_FORMAT_R16G16B16_UINT;     
        case SPV_REFLECT_FORMAT_R16G16B16_SINT:      return VK_FORMAT_R16G16B16_SINT;     
        case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT:    return VK_FORMAT_R16G16B16_SFLOAT;   
        case SPV_REFLECT_FORMAT_R16G16B16A16_UINT:   return VK_FORMAT_R16G16B16A16_UINT;  
        case SPV_REFLECT_FORMAT_R16G16B16A16_SINT:   return VK_FORMAT_R16G16B16A16_SINT;  
        case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case SPV_REFLECT_FORMAT_R32_UINT:            return VK_FORMAT_R32_UINT;           
        case SPV_REFLECT_FORMAT_R32_SINT:            return VK_FORMAT_R32_SINT;           
        case SPV_REFLECT_FORMAT_R32_SFLOAT:          return VK_FORMAT_R32_SFLOAT;         
        case SPV_REFLECT_FORMAT_R32G32_UINT:         return VK_FORMAT_R32G32_UINT;        
        case SPV_REFLECT_FORMAT_R32G32_SINT:         return VK_FORMAT_R32G32_SINT;        
        case SPV_REFLECT_FORMAT_R32G32_SFLOAT:       return VK_FORMAT_R32G32_SFLOAT;      
        case SPV_REFLECT_FORMAT_R32G32B32_UINT:      return VK_FORMAT_R32G32B32_UINT;     
        case SPV_REFLECT_FORMAT_R32G32B32_SINT:      return VK_FORMAT_R32G32B32_SINT;     
        case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:    return VK_FORMAT_R32G32B32_SFLOAT;   
        case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:   return VK_FORMAT_R32G32B32A32_UINT;  
        case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:   return VK_FORMAT_R32G32B32A32_SINT;  
        case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case SPV_REFLECT_FORMAT_R64_UINT:            return VK_FORMAT_R64_UINT;           
        case SPV_REFLECT_FORMAT_R64_SINT:            return VK_FORMAT_R64_SINT;           
        case SPV_REFLECT_FORMAT_R64_SFLOAT:          return VK_FORMAT_R64_SFLOAT;         
        case SPV_REFLECT_FORMAT_R64G64_UINT:         return VK_FORMAT_R64G64_UINT;        
        case SPV_REFLECT_FORMAT_R64G64_SINT:         return VK_FORMAT_R64G64_SINT;        
        case SPV_REFLECT_FORMAT_R64G64_SFLOAT:       return VK_FORMAT_R64G64_SFLOAT;      
        case SPV_REFLECT_FORMAT_R64G64B64_UINT:      return VK_FORMAT_R64G64B64_UINT;     
        case SPV_REFLECT_FORMAT_R64G64B64_SINT:      return VK_FORMAT_R64G64B64_SINT;     
        case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT:    return VK_FORMAT_R64G64B64_SFLOAT;   
        case SPV_REFLECT_FORMAT_R64G64B64A64_UINT:   return VK_FORMAT_R64G64B64A64_UINT;  
        case SPV_REFLECT_FORMAT_R64G64B64A64_SINT:   return VK_FORMAT_R64G64B64A64_SINT;  
        case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT: return VK_FORMAT_R64G64B64A64_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

struct Reflection 
{
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::map<uint32_t, SparseSet<VkDescriptorSetLayoutBinding>> descSets;
    std::map<DescriptorBinding, SpvReflectDescriptorBinding> descBindings;
    std::map<DescriptorBinding, VkPushConstantRange> pushConstants;
    std::unordered_map<uint32_t, VkFormat> input;
    std::unordered_map<uint32_t, VkFormat> output;
};
static Reflection reflect(Shader const &shader)
{
    Reflection reflection;
    for(auto const &binDesc : shader.binDescriptors)
    {
        auto const &bin = shader.binaries[binDesc.binary];
        reflection.stages.emplace_back(VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = binDesc.stage,
            .module = bin.module,
            .pName = binDesc.entry.c_str()
        });

        SpvReflectShaderModule module;
        SPV_CHK(spvReflectCreateShaderModule(bin.spirv.size() * sizeof(bin.spirv[0]), bin.spirv.data(), &module), bin.path, continue);

        // Descriptor bindings
        uint32_t count = 0;
        SPV_CHK(spvReflectEnumerateDescriptorBindings(&module, &count, nullptr), bin.path, continue);
        std::vector<SpvReflectDescriptorBinding *> descBindings(count);
        SPV_CHK(spvReflectEnumerateDescriptorBindings(&module, &count, descBindings.data()), bin.path, continue);
        for(auto descBinding : descBindings)
        {
            reflection.descBindings[{descBinding->set, descBinding->binding}] = *descBinding;

            auto &binding = reflection.descSets[descBinding->set][descBinding->binding];
            binding.binding = descBinding->binding;
            binding.descriptorType = toVulkanDescriptorType(descBinding->descriptor_type);
            binding.descriptorCount = std::max(descBinding->count, binding.descriptorCount);
            binding.stageFlags |= binDesc.stage;
        }

        // Push constants
        SPV_CHK(spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr), bin.path, continue);
        std::vector<SpvReflectBlockVariable *> pushConstants(count);
        SPV_CHK(spvReflectEnumeratePushConstantBlocks(&module, &count, pushConstants.data()), bin.path, continue);
        for(auto pushConstant : pushConstants)
        {
            auto &constant = reflection.pushConstants[{pushConstant->offset, pushConstant->size}];
            constant.offset = pushConstant->offset;
            constant.size = pushConstant->size;
            constant.stageFlags |= binDesc.stage;
        }

        // Input
        SPV_CHK(spvReflectEnumerateInputVariables(&module, &count, nullptr), bin.path, continue);
        std::vector<SpvReflectInterfaceVariable *> inputVariables(count);
        SPV_CHK(spvReflectEnumerateInputVariables(&module, &count, inputVariables.data()), bin.path, continue);
        for(auto inputVariable : inputVariables)
            reflection.input.try_emplace(inputVariable->location, toVulkanFormat(inputVariable->format));

        // Output
        SPV_CHK(spvReflectEnumerateOutputVariables(&module, &count, nullptr), bin.path, continue);
        std::vector<SpvReflectInterfaceVariable *> outputVariables(count);
        SPV_CHK(spvReflectEnumerateOutputVariables(&module, &count, outputVariables.data()), bin.path, continue);
        for(auto outputVariable : outputVariables)
            reflection.output.try_emplace(outputVariable->location, toVulkanFormat(outputVariable->format));

        spvReflectDestroyShaderModule(&module);
    }

    return reflection;
}

template <> struct fmt::formatter<SpvReflectDescriptorBinding> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    auto format(SpvReflectDescriptorBinding const &binding, format_context &ctx) const {
        return fmt::format_to(ctx.out(), "layout(set = {}, binding = {}) ({})", binding.set, binding.binding, string_VkDescriptorType(toVulkanDescriptorType(binding.descriptor_type)));
    }
};

// WARNING: nested spaghetti code incoming (works on hopes and dreams, or not)
// TODO: Add more error checking
static Pipeline::Layout makePipelineLayout(VkDevice dev, PipelineLayoutCreateInfo const &ci, Shader const &shader, VmaAllocator allocator)
{
    Pipeline::Layout layout;
    layout._reflection = new Reflection(reflect(shader));
    auto &reflection = *static_cast<Reflection *>(layout._reflection);

    std::map<uint32_t, SparseSet<VkDescriptorBindingFlags>> descFlags;
 
    // Descriptor flags
    for(auto [binding, desc] : reflection.descBindings)
    {
        auto &flags = descFlags[binding.set][binding.binding];
        if(ci.descriptorBindingFlags.contains(binding))
            flags |= ci.descriptorBindingFlags.at(binding);

        if((desc.array.dims_count > 0 && desc.array.dims[0] == 0) || 
            desc.type_description->op == SpvOpTypeRuntimeArray || 
            ci.unsizedDescriptorSize.contains({binding.set, binding.binding})
        )
            flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    }

    for(auto &[set, bindings] : reflection.descSets) 
    {
        for(auto [binding, desc] : bindings) 
        {
            if(ci.unsizedDescriptorSize.contains({set, (uint32_t) binding}))
                desc.descriptorCount = std::max(desc.descriptorCount, ci.unsizedDescriptorSize.at({set, (uint32_t) binding}));
            if(ci.descriptorTypeOverride.contains({set, (uint32_t) binding}))
                desc.descriptorType = ci.descriptorTypeOverride.at({set, (uint32_t) binding});
        }
    }

    // Descriptor set layouts
    layout.descLayouts.reserve(reflection.descSets.size());
    for(auto const &[set, bindings] : reflection.descSets)
    {
        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsCI{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = (uint32_t) descFlags.at(set).size(),
            .pBindingFlags = descFlags.at(set).dense().data(),
        };
        VkDescriptorSetLayoutCreateInfo layoutCI{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &flagsCI,
            .flags = ci.descriptorSetFlags.contains(set) ? ci.descriptorSetFlags.at(set) : 0,
            .bindingCount = (uint32_t) bindings.size(),
            .pBindings = bindings.dense().data(),
        };
        vkCreateDescriptorSetLayout(dev, &layoutCI, nullptr, &layout.descLayouts[set]);
    }

    // Descriptor pool
    SparseSet<VkDescriptorPoolSize> poolSizes;
    for(auto const &[set, bindings] : reflection.descSets)
    {
        for(auto const &binding : bindings.dense())
        {
            auto const &flags = descFlags.at(set).get(binding.binding);
            auto &size = poolSizes[binding.descriptorType];
            size.type = binding.descriptorType;
            size.descriptorCount += flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT ? ci.maxVariableCountSize : binding.descriptorCount;
        }
    }
    VkDescriptorPoolCreateInfo poolCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = ci.maxDescriptorSets,
        .poolSizeCount = (uint32_t) poolSizes.size(),
        .pPoolSizes = poolSizes.dense().data()
    };
    CHECK_VK_RES(vkCreateDescriptorPool(dev, &poolCI, nullptr, &layout.descPool));

    // Allocate descriptors
    for(auto const &[set, bindings] : reflection.descSets)
    {
        uint32_t count;
        std::optional<VkDescriptorSetVariableDescriptorCountAllocateInfo> variableSizedBinding;
        for(auto binding : bindings.dense())
        {
            auto const &flags = descFlags.at(set).get(binding.binding);
            if(flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
            {
                if(ci.unsizedDescriptorSize.contains({set, binding.binding}))
                {
                    count = ci.unsizedDescriptorSize.at({set, binding.binding});
                    variableSizedBinding = {
                        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
                        .descriptorSetCount = 1,
                        .pDescriptorCounts = &count
                    };
                    break;
                } else {
                    LOG_ERROR("You must specify an unsizedDescriptorSize a variable sized array descriptor binding {}", reflection.descBindings.at({set, binding.binding}));
                    continue;
                }
            }
        }

        VkDescriptorSetAllocateInfo descSetAlloc{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = variableSizedBinding.has_value() ? &variableSizedBinding.value() : nullptr,
            .descriptorPool = layout.descPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &layout.descLayouts.get(set)
        };
        CHECK_VK_RES(vkAllocateDescriptorSets(dev, &descSetAlloc, &layout.descSets[set]));
    }

    std::vector<VkPushConstantRange> pushConstants;
    pushConstants.reserve(reflection.pushConstants.size());
    for(auto const &pair : reflection.pushConstants)
        pushConstants.emplace_back(pair.second);

    VkPipelineLayoutCreateInfo pipelineLayoutCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = (uint32_t) layout.descLayouts.size(),
        .pSetLayouts = layout.descLayouts.dense().data(),
        .pushConstantRangeCount = (uint32_t) pushConstants.size(),
        .pPushConstantRanges = pushConstants.data(),
    };
    CHECK_VK_RES(vkCreatePipelineLayout(dev, &pipelineLayoutCI, nullptr, &layout.layout));

    return layout;
}
Pipeline vk::makePipeline(Shader const &shader, GraphicsPipelineCreateInfo const &ci)
{
    auto const &dev = shader.createInfo.device;

    Pipeline pipeline{
        .type = Pipeline::Type::GRAPHICS,
        .device = shader.createInfo.device
    };

    pipeline.layout = makePipelineLayout(dev, ci.layout, shader, ci.allocator);
    auto const &reflection = *static_cast<Reflection const *>(pipeline.layout._reflection);

    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(ci.input.bindings.size()),
        .pVertexBindingDescriptions = ci.input.bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(ci.input.attributes.size()),
        .pVertexAttributeDescriptions = ci.input.attributes.data(),
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = ci.input.topology,
        .primitiveRestartEnable = ci.input.primitiveRestart
    };

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = std::max<uint32_t>(ci.viewport.viewportCount, ci.viewport.viewports.size()),
        .pViewports = ci.viewport.viewports.data(),
        .scissorCount = std::max<uint32_t>(ci.viewport.scissorCount, ci.viewport.scissors.size()),
        .pScissors = ci.viewport.scissors.data(),
    };

    VkPipelineRenderingCreateInfo renderingCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = (uint32_t) ci.attachments.color.size(),
        .pColorAttachmentFormats = ci.attachments.color.data(),
        .depthAttachmentFormat = ci.attachments.depth,
        .stencilAttachmentFormat = ci.attachments.stencil
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = (uint32_t) ci.dynamicState.size(),
        .pDynamicStates = ci.dynamicState.data()
    };
    VkPipelineColorBlendStateCreateInfo blendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = ci.blending.logicOpEnable,
        .logicOp = ci.blending.logicOp,
        .attachmentCount = (uint32_t) ci.blending.attachments.size(),
        .pAttachments = ci.blending.attachments.data(),
        .blendConstants = {ci.blending.constant.r, ci.blending.constant.g, ci.blending.constant.b, ci.blending.constant.a}
    };
    VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .flags = ci.rasterization.flags,
        .depthClampEnable = ci.rasterization.depthClampEnable,
        .rasterizerDiscardEnable = ci.rasterization.rasterizerDiscardEnable,
        .polygonMode = ci.rasterization.polygonMode,
        .cullMode = ci.rasterization.cullMode,
        .frontFace = ci.rasterization.frontFace,
        .depthBiasEnable = ci.rasterization.depthBiasEnable,
        .depthBiasConstantFactor = ci.rasterization.depthBiasConstantFactor,
        .depthBiasClamp = ci.rasterization.depthBiasClamp,
        .depthBiasSlopeFactor = ci.rasterization.depthBiasSlopeFactor,
        .lineWidth = ci.rasterization.lineWidth,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .flags = ci.depthStencil.flags,
        .depthTestEnable = ci.depthStencil.depthTestEnable,
        .depthWriteEnable = ci.depthStencil.depthWriteEnable,
        .depthCompareOp = ci.depthStencil.depthCompareOp,
        .depthBoundsTestEnable = ci.depthStencil.depthBoundsTestEnable,
        .stencilTestEnable = ci.depthStencil.stencilTestEnable,
        .front = ci.depthStencil.front,
        .back = ci.depthStencil.back,
        .minDepthBounds = ci.depthStencil.minDepthBounds,
        .maxDepthBounds = ci.depthStencil.maxDepthBounds,
    };
    VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .flags = ci.multisample.flags,
        .rasterizationSamples = ci.multisample.rasterizationSamples,
        .sampleShadingEnable = ci.multisample.sampleShadingEnable,
        .minSampleShading = ci.multisample.minSampleShading,
        .pSampleMask = ci.multisample.sampleMask.data(),
        .alphaToCoverageEnable = ci.multisample.alphaToCoverageEnable,
        .alphaToOneEnable = ci.multisample.alphaToOneEnable,
    };

    VkGraphicsPipelineCreateInfo pipelineCI{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .flags = ci.flags,
        .stageCount = (uint32_t) reflection.stages.size(),
        .pStages = reflection.stages.data(),
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &blendState,
        .pDynamicState = &dynamicState,
        .layout = pipeline.layout.layout
    };
    CHECK_VK_RES(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline.pipeline));

    pipeline.valid = true;
    return pipeline;
}
Pipeline vk::makePipeline(Shader const &shader, ComputePipelineCreateInfo const &ci)
{
    auto const &dev = shader.createInfo.device;

    Pipeline pipeline{
        .type = Pipeline::Type::GRAPHICS,
        .device = shader.createInfo.device
    };

    pipeline.layout = makePipelineLayout(dev, ci.layout, shader, ci.allocator);
    auto const &reflection = *static_cast<Reflection const *>(pipeline.layout._reflection);

    VkComputePipelineCreateInfo pipelineCI{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .flags = ci.flags,
        .stage = *std::find_if(reflection.stages.begin(), reflection.stages.end(), [](VkPipelineShaderStageCreateInfo const &stage){return stage.stage == VK_SHADER_STAGE_COMPUTE_BIT;}),
        .layout = pipeline.layout.layout,
    };
    CHECK_VK_RES(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline.pipeline));

    pipeline.valid = true;
    return pipeline;
}
Pipeline vk::makePipeline(Shader const &shader, RaytracingPipelineCreateInfo const &ci)
{
    auto const &dev = shader.createInfo.device;
    
    Pipeline pipeline{
        .type = Pipeline::Type::GRAPHICS,
        .device = shader.createInfo.device,
    };

    pipeline.layout = makePipelineLayout(dev, ci.layout, shader, ci.allocator);
    // auto const &reflection = *static_cast<Reflection const *>(pipeline.layout._reflection);

    assert(false && "not implemented!");

    pipeline.valid = false;
    return pipeline;
}

void vk::writeDescriptors(Pipeline const &pipeline, std::vector<DescriptorWrite> const &writes)
{
    assert(pipeline.layout._reflection);
    auto const &reflection = *static_cast<Reflection *>(pipeline.layout._reflection);

    std::vector<VkWriteDescriptorSet> descWrites;
    descWrites.reserve(writes.size());
    for(auto const &write : writes)
    {
        descWrites.emplace_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = pipeline.layout.descSets.get(write.dstSet),
            .dstBinding = write.dstBinding,
            .dstArrayElement = write.dstArrayElement,
            .descriptorCount = write.size(),
            .descriptorType = reflection.descSets.at(write.dstSet).get(write.dstBinding).descriptorType,
            .pImageInfo = write.imageInfo.data(),
            .pBufferInfo = write.bufferInfo.data(),
            .pTexelBufferView = write.texelBufferView.data(),
        });
    }

    vkUpdateDescriptorSets(pipeline.device, descWrites.size(), descWrites.data(), 0, nullptr);
}

void vk::destroy(Pipeline &pipeline)
{
    if(!pipeline.device)
        return;
    if(pipeline.layout.descPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(pipeline.device, pipeline.layout.descPool, nullptr);

    for(auto layout : pipeline.layout.descLayouts.dense())
    {
        if(layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(pipeline.device, layout, nullptr);
    }

    if(pipeline.layout._reflection)
        delete static_cast<Reflection *>(pipeline.layout._reflection);

    vkDestroyPipelineLayout(pipeline.device, pipeline.layout.layout, nullptr);

    vkDestroyPipeline(pipeline.device, pipeline.pipeline, nullptr);
}
