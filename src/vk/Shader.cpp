/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    Custom GLSL preprocessor.
   \$  /   $$ | \$$\   Allows to split (#include) and merge (#stage) sources 
    \_/    \__|  \__|  and manage spirv shader binaries.
*/
#include "vk.hpp"
#include "Logging.hpp"
#include "glslang/Include/glslang_c_interface.h"
#include "glslang/Public/resource_limits_c.h"

using namespace vk;
namespace fs = std::filesystem;

#ifndef STRIP_SHADER_COMPILATION
#define STRIP_SHADER_COMPILATION 0
#endif // STRIP_SHADER_COMPILATION

#if !STRIP_SHADER_COMPILATION
static const std::unordered_map<std::string, VkShaderStageFlagBits> gStageNameToVulkanEnum = {
    {"vertex"       , VK_SHADER_STAGE_VERTEX_BIT                 },
    {"geometry"     , VK_SHADER_STAGE_GEOMETRY_BIT               },
    {"fragment"     , VK_SHADER_STAGE_FRAGMENT_BIT               },
    {"compute"      , VK_SHADER_STAGE_COMPUTE_BIT                },
    {"tesscontrol"  , VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT   },
    {"tesevaluation", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
    {"mesh"         , VK_SHADER_STAGE_MESH_BIT_EXT               },
    {"raygen"       , VK_SHADER_STAGE_RAYGEN_BIT_KHR             },
    {"rayint"       , VK_SHADER_STAGE_INTERSECTION_BIT_KHR       },
    {"rayhit"       , VK_SHADER_STAGE_ANY_HIT_BIT_KHR            },
    {"rayclosesthit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR        },
    {"raymiss"      , VK_SHADER_STAGE_MISS_BIT_KHR               },
    {"raycallable"  , VK_SHADER_STAGE_CALLABLE_BIT_KHR           },
    {"all"          , VK_SHADER_STAGE_ALL                        }, // Append to all stages in the shader
};
static const std::unordered_map<VkShaderStageFlagBits, glslang_stage_t> gVulkanStageToGlslang = {
    {VK_SHADER_STAGE_VERTEX_BIT                 , GLSLANG_STAGE_VERTEX        },
    {VK_SHADER_STAGE_GEOMETRY_BIT               , GLSLANG_STAGE_GEOMETRY      },
    {VK_SHADER_STAGE_FRAGMENT_BIT               , GLSLANG_STAGE_FRAGMENT      },
    {VK_SHADER_STAGE_COMPUTE_BIT                , GLSLANG_STAGE_COMPUTE       },
    {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, GLSLANG_STAGE_TESSEVALUATION},
    {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT   , GLSLANG_STAGE_TESSCONTROL   },
    {VK_SHADER_STAGE_RAYGEN_BIT_KHR             , GLSLANG_STAGE_RAYGEN        },
    {VK_SHADER_STAGE_INTERSECTION_BIT_KHR       , GLSLANG_STAGE_INTERSECT     },
    {VK_SHADER_STAGE_ANY_HIT_BIT_KHR            , GLSLANG_STAGE_ANYHIT        },
    {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR        , GLSLANG_STAGE_CLOSESTHIT    },
    {VK_SHADER_STAGE_MISS_BIT_KHR               , GLSLANG_STAGE_MISS          },
    {VK_SHADER_STAGE_CALLABLE_BIT_KHR           , GLSLANG_STAGE_CALLABLE      },
    {VK_SHADER_STAGE_MESH_BIT_EXT               , GLSLANG_STAGE_MESH          },
};
static constexpr std::string_view INCLUDE_IDENTIFIER = "#include";
static constexpr std::string_view STAGE_IDENTIFIER = "#stage";
static constexpr std::string_view LINE_IDENTIFIER = "#line";
#endif // STRIP_SHADER_COMPILATION

static const std::unordered_map<std::string, VkShaderStageFlagBits> gVulkanStageStringToEnum = {
    {"VK_SHADER_STAGE_VERTEX_BIT"                  , VK_SHADER_STAGE_VERTEX_BIT                 },
    {"VK_SHADER_STAGE_GEOMETRY_BIT"                , VK_SHADER_STAGE_GEOMETRY_BIT               },
    {"VK_SHADER_STAGE_FRAGMENT_BIT"                , VK_SHADER_STAGE_FRAGMENT_BIT               },
    {"VK_SHADER_STAGE_COMPUTE_BIT"                 , VK_SHADER_STAGE_COMPUTE_BIT                },
    {"VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT"    , VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT   },
    {"VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT" , VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
    {"VK_SHADER_STAGE_MESH_BIT_EXT"                , VK_SHADER_STAGE_MESH_BIT_EXT               },
    {"VK_SHADER_STAGE_RAYGEN_BIT_KHR"              , VK_SHADER_STAGE_RAYGEN_BIT_KHR             },
    {"VK_SHADER_STAGE_INTERSECTION_BIT_KHR"        , VK_SHADER_STAGE_INTERSECTION_BIT_KHR       },
    {"VK_SHADER_STAGE_ANY_HIT_BIT_KHR"             , VK_SHADER_STAGE_ANY_HIT_BIT_KHR            },
    {"VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR"         , VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR        },
    {"VK_SHADER_STAGE_MISS_BIT_KHR"                , VK_SHADER_STAGE_MISS_BIT_KHR               },
    {"VK_SHADER_STAGE_CALLABLE_BIT_KHR"            , VK_SHADER_STAGE_CALLABLE_BIT_KHR           },
};

template<typename T = char>
static std::vector<T> readFileBinary(std::string_view filename) 
{
    std::ifstream file(std::string{filename}, std::ios::ate | std::ios::binary);

    if(!file.is_open()) 
    {
        LOG_ERROR("Failed to open file \"{}\"", filename);
        return {};
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<T> buffer((fileSize + sizeof(T) - 1) / sizeof(T));

    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), fileSize);

    return buffer;
}
static void writeFileBinary(std::string_view filename, char const *data, size_t size)
{
    auto dir = filename.substr(0, filename.find_last_of('/'));
    std::filesystem::create_directories(dir);
    std::ofstream file(std::string{filename}, std::ios::out | std::ios::binary | std::ios::trunc);
    assert(file);

    file.write(data, size);
}
static void collectBinaries(Shader &program)
{
    // Parse the metadata and add binaries.
    for(auto const &directoryEntry : std::filesystem::recursive_directory_iterator{program.binPath})
    {
        auto binPath = directoryEntry.path().string();

        auto stageName = directoryEntry.path().stem().string();
        if(gVulkanStageStringToEnum.find(stageName) == gVulkanStageStringToEnum.end())
        {
            LOG_WARN("Unknown file binary: \"{}\"", binPath);
            continue;
        }
        auto stage = gVulkanStageStringToEnum.at(stageName);

        program.binaries.emplace_back(Shader::Binary{
            .stage = stage,
            .spirv = readFileBinary<uint32_t>(binPath),
            .path = binPath,
        });
    }
}

#if !STRIP_SHADER_COMPILATION

// thanks to https://stackoverflow.com/a/217605
// Trim from the start (in place)
static void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}
// thanks to https://stackoverflow.com/a/217605
// Trim from the end (in place)
static void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}
/// @brief Process the include directives in the file.
/// @return Processed string of the file.
static std::string readFileWithIncludes(std::string path, std::string prevDir = "")
{
    LOG_TRACE("path = \"{}\"; prevDir = \"{}\"; prevDir + '/' + path = \"{}\"", path, prevDir, prevDir + '/' + path);
	std::ifstream file;

    std::string fullPath = "";
    if(std::filesystem::exists(prevDir + '/' + path)) // Relative
        fullPath = prevDir + '/' + path;
    else if(std::filesystem::exists(path)) // Absolute
        fullPath = path;
    else {
		LOG_ERROR("Could not open the file \"{}\"!", path);
		return std::string(INCLUDE_IDENTIFIER) + " \"" + path + '\"';
	}

    file = std::ifstream(fullPath);
    prevDir = fullPath.substr(0, std::min(fullPath.find_last_of("/\\") + 1, fullPath.size()));

    assert(file.is_open());

    auto dir = path.substr(0, path.find_last_of('/'));

    size_t line = 1;
	std::string fullSourceCode = "", lineBuffer;
    fullSourceCode += std::string(LINE_IDENTIFIER) + ' ' + std::to_string(line) + " \"" + fullPath + "\"\n";
	while(std::getline(file, lineBuffer))
	{
		if(lineBuffer.find(INCLUDE_IDENTIFIER) != std::string::npos)
		{
            // Trim the line
            ltrim(lineBuffer);
            rtrim(lineBuffer);
			// Remove the include identifier, this will cause the path to remain
			lineBuffer.erase(0, INCLUDE_IDENTIFIER.size());
			// Remove quotation marks from the include-string, in case there are any
            lineBuffer.erase(std::remove(lineBuffer.begin(), lineBuffer.end(), '\"'), lineBuffer.cend());
            // Trim the line again for a good measure
            ltrim(lineBuffer);
            rtrim(lineBuffer);

			fullSourceCode += readFileWithIncludes(lineBuffer, dir);
            fullSourceCode += std::string(LINE_IDENTIFIER) + ' ' + std::to_string(line) + " \"" + fullPath + "\"\n";
			continue;
		} else {
            fullSourceCode += lineBuffer + '\n';
        }
        ++line;
	}

	file.close();

	return fullSourceCode;
}
static std::map<VkShaderStageFlagBits, std::string> splitSources(Shader &program)
{
    std::map<VkShaderStageFlagBits, std::string> stages;
    VkShaderStageFlagBits currentStage = VK_SHADER_STAGE_ALL;
    auto &src = program.src.data;

    src.insert(0, std::string(STAGE_IDENTIFIER) + " all\n");

    size_t pos = 0;
    while(true) 
    {
        auto directive = src.find(STAGE_IDENTIFIER, pos);
        if(directive == std::string::npos)
            break;

        auto stageStart = src.find_first_not_of(" \t", directive) + STAGE_IDENTIFIER.size();
        auto chunkStart = src.find_first_of('\n', directive);
        auto stageName = src.substr(stageStart, chunkStart - stageStart);
        ltrim(stageName);
        rtrim(stageName);

        if(gStageNameToVulkanEnum.find(stageName) != gStageNameToVulkanEnum.end())
        {
            currentStage = gStageNameToVulkanEnum.at(stageName);
        } else {
            LOG_ERROR("\"{}\": unknown stage name: \"{}\":\n{}", program.src.path, stageName, src.substr(directive, src.find_first_of('\n', directive) - directive));
        }

        auto &stage = stages[currentStage];
        auto nextDirective = src.find(STAGE_IDENTIFIER, chunkStart);
        pos = nextDirective;
        stage.append(src.substr(chunkStart, nextDirective - chunkStart));
    }

    auto all = stages[VK_SHADER_STAGE_ALL];
    stages.erase(VK_SHADER_STAGE_ALL);
    for(auto &[stage, src] : stages)
    {
        src.insert(0, all);
        LOG_TRACE("Stage {}:\n{:4}", string_VkShaderStageFlagBits(stage), src);
    }

    return stages;
}
static bool compileSources(Shader &program, std::map<VkShaderStageFlagBits, std::string> const &sources)
{
    glslang_program_t *glslProgram = glslang_program_create();
    std::vector<glslang_shader_t *> glslShaders;

    for(auto &[stage, source] : sources)
    {
        if(gVulkanStageToGlslang.find(stage) == gVulkanStageToGlslang.end())
        {
            LOG_WARN("Unknown shader stage: {}", string_VkShaderStageFlagBits(stage));
            continue;
        }

        const glslang_input_t input = {
            .language = GLSLANG_SOURCE_GLSL,
            .stage = gVulkanStageToGlslang.at(stage),
            .client = GLSLANG_CLIENT_VULKAN,
            .client_version = GLSLANG_TARGET_VULKAN_1_3,
            .target_language = GLSLANG_TARGET_SPV,
            .target_language_version = GLSLANG_TARGET_SPV_1_6,
            .code = source.c_str(),
            .default_version = 130,
            .default_profile = GLSLANG_NO_PROFILE,
            .force_default_version_and_profile = false,
            .forward_compatible = false,
            .messages = GLSLANG_MSG_DEFAULT_BIT,
            .resource = glslang_default_resource(),
        };

        glslang_shader_t *glslShader = glslang_shader_create(&input);

        if(!glslang_shader_preprocess(glslShader, &input))	{
            LOG_ERROR("GLSL preprocessing of \"{}\" failed: \n{}\n{}", program.src.path, glslang_shader_get_info_log(glslShader), glslang_shader_get_info_debug_log(glslShader));
            glslang_shader_delete(glslShader);
            return false;
        }

        if(!glslang_shader_parse(glslShader, &input)) {
            LOG_ERROR("GLSL parsing of \"{}\" failed: \n{}\n{}", program.src.path, glslang_shader_get_info_log(glslShader), glslang_shader_get_info_debug_log(glslShader));
            glslang_shader_delete(glslShader);
            return false;
        }
        
        glslang_program_add_shader(glslProgram, glslShader);
        glslShaders.emplace_back(glslShader);
    }

    if(!glslang_program_link(glslProgram, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        LOG_ERROR("GLSL linking of \"{}\" failed: \n{}\n{}", program.src.path, glslang_program_get_info_log(glslProgram), glslang_program_get_info_debug_log(glslProgram));
        glslang_program_delete(glslProgram);
        for(auto glslShader : glslShaders)
            glslang_shader_delete(glslShader);

        return true;
    }

    for(auto &[stage, data] : sources)
    {
        glslang_program_SPIRV_generate(glslProgram, gVulkanStageToGlslang.at(stage));

        auto size = glslang_program_SPIRV_get_size(glslProgram);
        auto &bin = program.binaries.emplace_back();
        bin.stage = stage;
        bin.spirv.resize(size);
        glslang_program_SPIRV_get(glslProgram, bin.spirv.data());
    }

    const char* spirv_messages = glslang_program_SPIRV_get_messages(glslProgram);
    if(spirv_messages)
        LOG_WARN("Spirv messages: {}", spirv_messages);

    glslang_program_delete(glslProgram);
    for(auto glslShader : glslShaders)
        glslang_shader_delete(glslShader);

    return true;
}

#endif // STRIP_SHADER_COMPILATION

Shader vk::makeShader(std::string_view src, std::string_view bin, VkDevice dev)
{
    Shader program;
    if(!fs::exists(src) || !fs::is_regular_file(src))
    {
        LOG_ERROR("Invalid src path: \"{}\"", src);
        return program;
    }
    if(fs::exists(bin) && !fs::is_directory(bin))
    {
        LOG_ERROR("Invalid bin path: \"{}\"", bin);
        return program;
    }

    bool canCompile = fs::exists(src);

    program.binPath = bin;
    if(canCompile)
    {
        program.src.path = src;
#if !STRIP_SHADER_COMPILATION
        program.src.data = readFileWithIncludes(program.src.path);
#endif // STRIP_SHADER_COMPILATION
    }

    bool outdated = canCompile && fs::exists(program.binPath) && std::filesystem::last_write_time(program.src.path).time_since_epoch() > std::filesystem::last_write_time(program.binPath).time_since_epoch();
    if(outdated && canCompile)
    {
        LOG_INFO("\"{}\" shader binaries are outdated! Recompiling");
    }

    if(fs::exists(program.binPath) && !outdated && fs::is_directory(program.binPath))
    {
        collectBinaries(program);
#if !STRIP_SHADER_COMPILATION
    } else if(canCompile) {

        if(!compileSources(program, splitSources(program)))
            return program;

        for(auto &bin : program.binaries)
        {
            bin.path = (fs::path(program.binPath)/string_VkShaderStageFlagBits(bin.stage)).string() + ".spv";
            LOG_VAR(bin.path);
            writeFileBinary(bin.path, reinterpret_cast<char const *>(bin.spirv.data()), bin.spirv.size() * sizeof(bin.spirv[0]));
        }
    } else {
#endif // STRIP_SHADER_COMPILATION
        LOG_ERROR("Cannot find binaries \"{}\" or compile from source \"{}\" shaders!", bin, src);
        return program;
    }

    // Create vulkan modules from binaries.
    program.device = dev;
    for(auto &bin : program.binaries)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = bin.spirv.size() * sizeof(bin.spirv[0]);
        createInfo.pCode = bin.spirv.data();
        CHK(vkCreateShaderModule(program.device, &createInfo, nullptr, &bin.module));
    }

    program.valid = true;
    return program;
}

void vk::destroy(Shader &shader)
{
    for(auto &bin :shader.binaries)
        if(bin.module)
            vkDestroyShaderModule(shader.device, bin.module, nullptr);
}
