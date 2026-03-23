/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    Pipeline creator.
   \$  /   $$ | \$$\   Creates the pipeline from the shader     
    \_/    \__|  \__|  reflection and other options.
*/
#include "vk.hpp"
#include "spirv_reflect.h"
#include "Logging.hpp"

using namespace vk;

static std::string string_SpvReflectResult(SpvReflectResult res)
{
    switch (res) {
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
static std::string string_PipelineType(Pipeline::Type type)
{
    switch (type) {
        case Pipeline::Type::INVALID:    return "Pipeline::Type::INVALID";   
        case Pipeline::Type::GRAPHICS:   return "Pipeline::Type::GRAPHICS";  
        case Pipeline::Type::COMPUTE:    return "Pipeline::Type::COMPUTE";   
        case Pipeline::Type::RAYTRACING: return "Pipeline::Type::RAYTRACING";
        default: return "Unhandled Pipeline::Type";
    };
}
// Only vertex-capable
static uint32_t formatSize(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8_SRGB:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R8G8_SRGB:
            return 2;
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_R8G8B8_UINT:
        case VK_FORMAT_R8G8B8_SINT:
        case VK_FORMAT_R8G8B8_SRGB:
            return 3;

        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return 4;
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SINT:
        case VK_FORMAT_R16_SFLOAT:
            return 2;
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R16G16_SFLOAT:
            return 4;
        case VK_FORMAT_R16G16B16_UNORM:
        case VK_FORMAT_R16G16B16_SNORM:
        case VK_FORMAT_R16G16B16_UINT:
        case VK_FORMAT_R16G16B16_SINT:
        case VK_FORMAT_R16G16B16_SFLOAT:
            return 6;
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_SINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32_SFLOAT:
            return 4;
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32_SFLOAT:
            return 8;
        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32_SINT:
        case VK_FORMAT_R32G32B32_SFLOAT:
            return 12;
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        case VK_FORMAT_A2R10G10B10_UINT_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
            return 4;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
            return 4;
        case VK_FORMAT_R64_UINT:
        case VK_FORMAT_R64_SINT:
        case VK_FORMAT_R64_SFLOAT:
            return 8;
        case VK_FORMAT_R64G64_UINT:
        case VK_FORMAT_R64G64_SINT:
        case VK_FORMAT_R64G64_SFLOAT:
            return 16;
        case VK_FORMAT_R64G64B64_UINT:
        case VK_FORMAT_R64G64B64_SINT:
        case VK_FORMAT_R64G64B64_SFLOAT:
            return 24;
        case VK_FORMAT_R64G64B64A64_UINT:
        case VK_FORMAT_R64G64B64A64_SINT:
        case VK_FORMAT_R64G64B64A64_SFLOAT:
            return 32;
        default:
            return 0;
    }
}

#define SPV_CHK(x, name, action) { SpvReflectResult _result = x; if(_result != SPV_REFLECT_RESULT_SUCCESS) { LOG_ERROR("{}:{}: Failed to {}: {} for binary \"{}\".", __FILE__, __LINE__, #x, string_SpvReflectResult(_result), name); action; } }
static Pipeline::Type determineType(Shader const &shader)
{
    if(shader.binaries.empty())
        LOG_ERROR("No binaries in shader \"{}\"/\"{}\" provided to determineType", shader.createInfo.src, shader.createInfo.bin);

    switch(shader.binaries[0].stage)
    {
        case VK_SHADER_STAGE_COMPUTE_BIT:
            return Pipeline::Type::COMPUTE;

        case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
        case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
        case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
        case VK_SHADER_STAGE_MISS_BIT_KHR:
        case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
        case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
            return Pipeline::Type::RAYTRACING;

        case VK_SHADER_STAGE_VERTEX_BIT:
        case VK_SHADER_STAGE_GEOMETRY_BIT:
        case VK_SHADER_STAGE_FRAGMENT_BIT:
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
        case VK_SHADER_STAGE_MESH_BIT_EXT:
            return Pipeline::Type::GRAPHICS;

        default:
            return Pipeline::Type::INVALID;
    }
}
static VkDescriptorType toVulkanDescriptorType(SpvReflectDescriptorType const &type)
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
        default: return (VkDescriptorType) 0;
    }
}
static VkFormat toVulkanFormat(SpvReflectFormat const &format)
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
    std::map<uint32_t, SparseSet<VkDescriptorSetLayoutBinding>> descSets; // ordered
    std::map<uint32_t, SparseSet<VkDescriptorBindingFlags>> descFlags; 
    std::map<std::pair<uint32_t, uint32_t>, VkPushConstantRange> pushConstants;
    std::unordered_map<uint32_t, VkFormat> input;
    std::unordered_map<uint32_t, VkFormat> output;
};
static Reflection reflect(Shader const &shader)
{
    Reflection reflection;
    for(auto const &bin : shader.binaries)
    {
        reflection.stages.emplace_back(VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = bin.stage,
            .module = bin.module,
            .pName = "main"
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
            auto &binding = reflection.descSets[descBinding->set][descBinding->binding];
            binding.binding = descBinding->binding;
            binding.descriptorType = toVulkanDescriptorType(descBinding->descriptor_type);
            binding.descriptorCount = std::max(descBinding->count, binding.descriptorCount);
            binding.stageFlags |= bin.stage;

            reflection.descFlags[descBinding->set][descBinding->binding]; // Create if doesent contain
            if(descBinding->array.dims_count > 0 && descBinding->array.dims[0] == 0)
                reflection.descFlags[descBinding->set][descBinding->binding] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
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
            constant.stageFlags |= bin.stage;
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

static void makeDescriptors(Pipeline &pipeline, PipelineCreateInfo const &ci, Reflection const &reflection)
{
    auto &dev = pipeline.device;
    // Descriptor set layouts
    pipeline.descLayouts.reserve(reflection.descSets.size());
    uint32_t descCount = 0;
    std::unordered_map<uint32_t, size_t> setToLayoutIndex;
    // Just hope the order is right and the set indices are consecutive
    for(auto const &[set, bindings] : reflection.descSets)
    {
        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsCI{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = (uint32_t) reflection.descFlags.at(set).size(),
            .pBindingFlags = reflection.descFlags.at(set).dense().data(),
        };
        VkDescriptorSetLayoutCreateInfo layoutCI{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &flagsCI,
            .flags = ci.descriptorSetFlags.contains(set) ? ci.descriptorSetFlags.at(set) : 0,
            .bindingCount = (uint32_t) bindings.size(),
            .pBindings = bindings.dense().data(),
        };
        setToLayoutIndex[set] = pipeline.descLayouts.size();
        vkCreateDescriptorSetLayout(dev, &layoutCI, nullptr, &pipeline.descLayouts.emplace_back());
        descCount += bindings.size();
    }
    descCount *= ci.framesInFlight;

    // Descriptor pool
    SparseSet<VkDescriptorPoolSize> poolSizes;
    for(auto const &[set, bindings] : reflection.descSets)
    {
        for(auto const &binding : bindings.dense())
        {
            auto const &flags = reflection.descFlags.at(set).get(binding.binding);
            auto &size = poolSizes[binding.descriptorType];
            size.type = binding.descriptorType;
            size.descriptorCount += flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT ? ci.maxVariableCountSize : binding.descriptorCount;
        }
    }
    for(auto &size : std::ranges::subrange{poolSizes.denseBegin(), poolSizes.denseEnd()})
        size.descriptorCount *= ci.framesInFlight;
    VkDescriptorPoolCreateInfo poolCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = ci.maxDescriptorSets * ci.framesInFlight,
        .poolSizeCount = (uint32_t) poolSizes.size(),
        .pPoolSizes = poolSizes.dense().data()
    };
    CHK(vkCreateDescriptorPool(dev, &poolCI, nullptr, &pipeline.descPool));

    // Allocate descriptors
    pipeline.descSets.resize(ci.framesInFlight);
    for(auto const &[set, bindings] : reflection.descSets)
    {
        uint32_t count;
        std::optional<VkDescriptorSetVariableDescriptorCountAllocateInfo> variableSizedBinding;
        for(auto binding : bindings.dense())
        {
            auto const &flags = reflection.descFlags.at(set).get(binding.binding);
            if(flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
            {
                auto countIter = std::find_if(
                    ci.descriptorWrites.begin(), 
                    ci.descriptorWrites.end(), 
                    [&](PipelineCreateInfo::DescriptorWrite const &write){ 
                        return write.binding == Pipeline::DescriptorBinding{set, binding.binding}; 
                });

                if(countIter != ci.descriptorWrites.end())
                {
                    count = countIter->count;
                    variableSizedBinding = {
                        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
                        .descriptorSetCount = 1,
                        .pDescriptorCounts = &count
                    };
                    break;
                } else {
                    LOG_ERROR("You must specify a write for a variable sized array descriptor binding layout(set={}, binding={})", set, binding.binding);
                }
            }
        }
        VkDescriptorSetAllocateInfo descSetAlloc{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = variableSizedBinding.has_value() ? &variableSizedBinding.value() : nullptr,
            .descriptorPool = pipeline.descPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &pipeline.descLayouts[setToLayoutIndex.at(set)]
        };
        for(uint i = 0; i < ci.framesInFlight; ++i)
            CHK(vkAllocateDescriptorSets(dev, &descSetAlloc, &pipeline.descSets[i][set]));
    }

    // Write descriptors
    std::vector<VkWriteDescriptorSet> descWrites;
    descWrites.reserve(ci.descriptorWrites.size() * ci.framesInFlight);
    for(auto const &write : ci.descriptorWrites)
    {
        for(uint i = 0; i < ci.framesInFlight; ++i)
        {
            descWrites.emplace_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = pipeline.descSets[i].get(write.binding.set),
                .dstBinding = write.binding.binding,
                .dstArrayElement = write.dstArrayElement,
                .descriptorCount = write.count,
                .descriptorType = reflection.descSets.at(write.binding.set).get(write.binding.binding).descriptorType,
                .pImageInfo = write.imageInfo.data(),
                .pBufferInfo = write.bufferInfo.data(),
                .pTexelBufferView = write.texelBufferView.data(),
            });
        }
    }
    vkUpdateDescriptorSets(dev, descWrites.size(), descWrites.data(), 0, nullptr);
}
static Pipeline makeGraphicsPipeline(Shader const &shader, PipelineCreateInfo const &ci)
{
    auto const &dev = shader.createInfo.device;
    Reflection reflection = reflect(shader);
    for(auto [binding, flag] : ci.descriptorBindingFlags)
        reflection.descFlags[binding.set][binding.binding] |= flag;

    Pipeline pipeline{
        .type = Pipeline::Type::GRAPHICS,
        .device = shader.createInfo.device,
        .images = ci.images,
        .buffers = ci.buffers,
    };


    makeDescriptors(pipeline, ci, reflection);

    // Push constants
    std::vector<VkPushConstantRange> pushConstants;
    pushConstants.reserve(reflection.pushConstants.size());
    for(auto const &pair : reflection.pushConstants)
        pushConstants.emplace_back(pair.second);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = (uint32_t) pipeline.descLayouts.size(),
        .pSetLayouts = pipeline.descLayouts.data(),
        .pushConstantRangeCount = (uint32_t) pushConstants.size(),
        .pPushConstantRanges = pushConstants.data(),
    };

    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(ci.vertexInputBindings.size()),
        .pVertexBindingDescriptions = ci.vertexInputBindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(ci.vertexInputAttributes.size()),
        .pVertexAttributeDescriptions = ci.vertexInputAttributes.data(),
    };

    pipeline.valid = true;
    return pipeline;
}

Pipeline vk::makePipeline(Shader const &shader, PipelineCreateInfo const &ci)
{
    auto type = determineType(shader);

    switch(type)
    {
    case Pipeline::Type::GRAPHICS:
        return makeGraphicsPipeline(shader, ci);
    default:
        LOG_ERROR("Invalid pipeline type: {}", string_PipelineType(type));
        return {};
    }
}

void vk::destroy(Pipeline &pipeline)
{

}
