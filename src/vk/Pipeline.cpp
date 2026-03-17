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
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER                   : return VK_DESCRIPTOR_TYPE_SAMPLER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER    : return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE             : return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE             : return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER      : return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER      : return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER            : return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER            : return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC    : return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC    : return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT          : return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
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
    struct DescSetBinding
    {
        std::string name;
        VkDescriptorSetLayoutBinding binding;
    };
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::map<std::pair<uint32_t, uint32_t>, DescSetBinding> descBindings; // Looks stupid
    std::map<std::pair<uint32_t, uint32_t>, VkPushConstantRange> pushConstants; // It is
    std::vector<VkVertexInputAttributeDescription> inputVariables;
    std::vector<VkVertexInputAttributeDescription> inputVariables;
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
            auto &binding = reflection.descBindings[{descBinding->set, descBinding->binding}];
            binding.binding.binding = descBinding->binding;
            binding.binding.descriptorType = toVulkanDescriptorType(descBinding->descriptor_type);
            binding.binding.descriptorCount = descBinding->count;
            binding.binding.stageFlags |= bin.stage;
            binding.name = binding.name;
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
        {
            reflection.inputVariables.emplace_back(VkVertexInputAttributeDescription{
                .location = inputVariable->location,
                .format = toVulkanFormat(inputVariable->format),
            });
        }

        // Output
        SPV_CHK(spvReflectEnumerateOutputVariables(&module, &count, nullptr), bin.path, continue);
        std::vector<SpvReflectInterfaceVariable *> outputVariables(count);
        SPV_CHK(spvReflectEnumerateOutputVariables(&module, &count, outputVariables.data()), bin.path, continue);
        for(auto outputVariable : outputVariables)
        {
            reflection.outputVariables.emplace_back(VkVertexInputAttributeDescription{
                .location = outputVariable->location,
                .format = toVulkanFormat(outputVariable->format),
            });
        }

        spvReflectDestroyShaderModule(&module);
    }

    return reflection;
}

static Pipeline makeGraphicsPipeline(Shader const &shader, PipelineCreateInfo const &ci)
{

}

Pipeline vk::makePipeline(Shader const &shader, PipelineCreateInfo const &ci)
{
    auto type = determineType(shader);

    switch(type)
    {
        case Pipeline::Type::GRAPHICS:
        return makeGraphicsPipeline(shader, ci);
        default:
        return {};
    }
}

void vk::destroy(Pipeline &pipeline)
{

}
