#include "Shader.hpp"
#include "Utility.hpp"
#include "Logging.hpp"
#include "glslang/Public/ShaderLang.h"
#include "glslang/SPIRV/GlslangToSpv.h"

#include <filesystem>
#include <set>
#include <ranges>

using namespace vk;
namespace fs = std::filesystem;

/// @brief Default include class for normal include convention
/// of search backward through the stack of active include paths (for nested includes).
/// Source: https://github.com/KhronosGroup/glslang StandAlone/DirStackFileIncluder.h
/// Modified to support system includes.
class DirStackFileIncluder : public glslang::TShader::Includer {
public:
    DirStackFileIncluder() = default;

    virtual IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override
    {
        return readPath(headerName, includerName, externalLocalDirectoryCount, (int)inclusionDepth, localDirectoryStack);
    }

    virtual IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override
    {
        return readPath(headerName, includerName, externalSystemDirectoryCount, (int)inclusionDepth, systemDirectoryStack);
    }

    // Externally set directories. E.g., from a command-line -I<dir>.
    //  - Most-recently pushed are checked first.
    //  - All these are checked after the parse-time stack of local directories
    //    is checked.
    //  - This only applies to the "local" form of #include.
    //  - Makes its own copy of the path.
    virtual void pushExternalLocalDirectory(const std::string& dir)
    {
        localDirectoryStack.push_back(dir);
        externalLocalDirectoryCount = (int)localDirectoryStack.size();
    }

    // Externally set directories. E.g., from a command-line -I<dir>.
    //  - Most-recently pushed are checked first.
    //  - All these are checked after the parse-time stack of local directories
    //    is checked.
    //  - This only applies to the <system> form of #include.
    //  - Makes its own copy of the path.
    virtual void pushExternalSystemDirectory(std::string const &dir)
    {
        systemDirectoryStack.push_back(dir);
        externalLocalDirectoryCount = (int)systemDirectoryStack.size();
    }

    virtual void releaseInclude(IncludeResult* result) override
    {
        if (result != nullptr) {
            delete [] static_cast<tUserDataElement*>(result->userData);
            delete result;
        }
    }

    virtual std::set<std::string> getIncludedFiles()
    {
        return includedFiles;
    }

    virtual ~DirStackFileIncluder() override { }

protected:
    typedef char tUserDataElement;
    std::vector<std::string> localDirectoryStack;
    int externalLocalDirectoryCount = 0;
    std::vector<std::string> systemDirectoryStack;
    int externalSystemDirectoryCount = 0;
    std::set<std::string> includedFiles;

    // Search for a valid "local" path based on combining the stack of include
    // directories and the nominal name of the header.
    virtual IncludeResult* readPath(const char* headerName, const char* includerName, int externalDirectoryCount, int depth, std::vector<std::string> &stack)
    {
        // Discard popped include directories, and
        // initialize when at parse-time first level.
        stack.resize(depth + externalDirectoryCount);
        if (depth == 1)
            stack.back() = getDirectory(includerName);

        // Find a directory that works, using a reverse search of the include stack.
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            std::string path = *it + '/' + headerName;
            std::replace(path.begin(), path.end(), '\\', '/');
            std::ifstream file(path, std::ios_base::binary | std::ios_base::ate);
            if (file) {
                stack.push_back(getDirectory(path));
                includedFiles.insert(path);
                return newIncludeResult(path, file, (int)file.tellg());
            }
        }

        return nullptr;
    }

    // Do actual reading of the file, filling in a new include result.
    virtual IncludeResult* newIncludeResult(const std::string& path, std::ifstream& file, int length) const
    {
        char* content = new tUserDataElement [length];
        file.seekg(0, file.beg);
        file.read(content, length);
        return new IncludeResult(path, content, length, content);
    }

    // If no path markers, return current working directory.
    // Otherwise, strip file name and return path leading up to it.
    virtual std::string getDirectory(const std::string path) const
    {
        size_t last = path.find_last_of("/\\");
        return last == std::string::npos ? "." : path.substr(0, last);
    }
};

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
    {"all"          , VK_SHADER_STAGE_ALL                        }, 
    {"preamble"     , VK_SHADER_STAGE_ALL                        }, // Append to all stages in the shader
};
static const std::unordered_map<VkShaderStageFlagBits, EShLanguage /* glslang_stage_t */> gVulkanStageToGlslang = {
    {VK_SHADER_STAGE_VERTEX_BIT                  , EShLangVertex         /* GLSLANG_STAGE_VERTEX         */},
    {VK_SHADER_STAGE_GEOMETRY_BIT                , EShLangGeometry       /* GLSLANG_STAGE_GEOMETRY       */},
    {VK_SHADER_STAGE_FRAGMENT_BIT                , EShLangFragment       /* GLSLANG_STAGE_FRAGMENT       */},
    {VK_SHADER_STAGE_COMPUTE_BIT                 , EShLangCompute        /* GLSLANG_STAGE_COMPUTE        */},
    {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT , EShLangTessEvaluation /* GLSLANG_STAGE_TESSEVALUATION */},
    {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT    , EShLangTessControl    /* GLSLANG_STAGE_TESSCONTROL    */},
    {VK_SHADER_STAGE_RAYGEN_BIT_KHR              , EShLangRayGen         /* GLSLANG_STAGE_RAYGEN         */},
    {VK_SHADER_STAGE_INTERSECTION_BIT_KHR        , EShLangIntersect      /* GLSLANG_STAGE_INTERSECT      */},
    {VK_SHADER_STAGE_ANY_HIT_BIT_KHR             , EShLangAnyHit         /* GLSLANG_STAGE_ANYHIT         */},
    {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR         , EShLangClosestHit     /* GLSLANG_STAGE_CLOSESTHIT     */},
    {VK_SHADER_STAGE_MISS_BIT_KHR                , EShLangMiss           /* GLSLANG_STAGE_MISS           */},
    {VK_SHADER_STAGE_CALLABLE_BIT_KHR            , EShLangCallable       /* GLSLANG_STAGE_CALLABLE       */},
    {VK_SHADER_STAGE_MESH_BIT_EXT                , EShLangMesh           /* GLSLANG_STAGE_MESH           */},
};
static constexpr std::string_view STAGE_IDENTIFIER = "#stage";
static constexpr std::string_view VERSION_IDENTIFIER = "#version";

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
static std::string readFileString(std::string_view filename) 
{
    std::ifstream file(std::string{filename});

    if(!file.is_open()) 
    {
        LOG_ERROR("Failed to open file \"{}\"", filename);
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
static void writeFileBinary(std::string_view filename, char const *data, size_t size)
{
    auto dir = fs::path(filename).parent_path().string();
    std::filesystem::create_directories(dir);
    std::ofstream file(std::string{filename}, std::ios::out | std::ios::binary | std::ios::trunc);
    assert(file);

    file.write(data, size);
}
static void collectBinaries(Shader &program)
{
    // Parse the metadata and add binaries.
    for(auto const &directoryEntry : std::filesystem::recursive_directory_iterator{program.createInfo.bin})
    {
        if(!directoryEntry.is_regular_file())
            continue;

        auto binPath = directoryEntry.path().string();

        // path/to/binary/VK_SHADER_STAGE_XXX.name.spv
        auto stage = directoryEntry.path().stem();
        auto name = stage.extension().string();
        stage = stage.stem();
        if(!name.empty() && name[0] == '.')
            name.erase(0, 1);
        if(gVulkanStageStringToEnum.find(stage) == gVulkanStageStringToEnum.end())
        {
            LOG_WARN("Unknown file binary: \"{}\"", binPath);
            continue;
        }

        program.binaries.emplace_back(Shader::Binary{
            .stage = gVulkanStageStringToEnum.at(stage.string()),
            .spirv = readFileBinary<uint32_t>(binPath),
            .path = binPath,
            .name = name,
        });
    }
}

// Thanks to https://github.com/KhronosGroup/glslang/issues/2207#issuecomment-632927839
static TBuiltInResource InitResources()
{
    TBuiltInResource Resources;

    Resources.maxLights                                 = 32;
    Resources.maxClipPlanes                             = 6;
    Resources.maxTextureUnits                           = 32;
    Resources.maxTextureCoords                          = 32;
    Resources.maxVertexAttribs                          = 64;
    Resources.maxVertexUniformComponents                = 4096;
    Resources.maxVaryingFloats                          = 64;
    Resources.maxVertexTextureImageUnits                = 32;
    Resources.maxCombinedTextureImageUnits              = 80;
    Resources.maxTextureImageUnits                      = 32;
    Resources.maxFragmentUniformComponents              = 4096;
    Resources.maxDrawBuffers                            = 32;
    Resources.maxVertexUniformVectors                   = 128;
    Resources.maxVaryingVectors                         = 8;
    Resources.maxFragmentUniformVectors                 = 16;
    Resources.maxVertexOutputVectors                    = 16;
    Resources.maxFragmentInputVectors                   = 15;
    Resources.minProgramTexelOffset                     = -8;
    Resources.maxProgramTexelOffset                     = 7;
    Resources.maxClipDistances                          = 8;
    Resources.maxComputeWorkGroupCountX                 = 65535;
    Resources.maxComputeWorkGroupCountY                 = 65535;
    Resources.maxComputeWorkGroupCountZ                 = 65535;
    Resources.maxComputeWorkGroupSizeX                  = 1024;
    Resources.maxComputeWorkGroupSizeY                  = 1024;
    Resources.maxComputeWorkGroupSizeZ                  = 64;
    Resources.maxComputeUniformComponents               = 1024;
    Resources.maxComputeTextureImageUnits               = 16;
    Resources.maxComputeImageUniforms                   = 8;
    Resources.maxComputeAtomicCounters                  = 8;
    Resources.maxComputeAtomicCounterBuffers            = 1;
    Resources.maxVaryingComponents                      = 60;
    Resources.maxVertexOutputComponents                 = 64;
    Resources.maxGeometryInputComponents                = 64;
    Resources.maxGeometryOutputComponents               = 128;
    Resources.maxFragmentInputComponents                = 128;
    Resources.maxImageUnits                             = 8;
    Resources.maxCombinedImageUnitsAndFragmentOutputs   = 8;
    Resources.maxCombinedShaderOutputResources          = 8;
    Resources.maxImageSamples                           = 0;
    Resources.maxVertexImageUniforms                    = 0;
    Resources.maxTessControlImageUniforms               = 0;
    Resources.maxTessEvaluationImageUniforms            = 0;
    Resources.maxGeometryImageUniforms                  = 0;
    Resources.maxFragmentImageUniforms                  = 8;
    Resources.maxCombinedImageUniforms                  = 8;
    Resources.maxGeometryTextureImageUnits              = 16;
    Resources.maxGeometryOutputVertices                 = 256;
    Resources.maxGeometryTotalOutputComponents          = 1024;
    Resources.maxGeometryUniformComponents              = 1024;
    Resources.maxGeometryVaryingComponents              = 64;
    Resources.maxTessControlInputComponents             = 128;
    Resources.maxTessControlOutputComponents            = 128;
    Resources.maxTessControlTextureImageUnits           = 16;
    Resources.maxTessControlUniformComponents           = 1024;
    Resources.maxTessControlTotalOutputComponents       = 4096;
    Resources.maxTessEvaluationInputComponents          = 128;
    Resources.maxTessEvaluationOutputComponents         = 128;
    Resources.maxTessEvaluationTextureImageUnits        = 16;
    Resources.maxTessEvaluationUniformComponents        = 1024;
    Resources.maxTessPatchComponents                    = 120;
    Resources.maxPatchVertices                          = 32;
    Resources.maxTessGenLevel                           = 64;
    Resources.maxViewports                              = 16;
    Resources.maxVertexAtomicCounters                   = 0;
    Resources.maxTessControlAtomicCounters              = 0;
    Resources.maxTessEvaluationAtomicCounters           = 0;
    Resources.maxGeometryAtomicCounters                 = 0;
    Resources.maxFragmentAtomicCounters                 = 8;
    Resources.maxCombinedAtomicCounters                 = 8;
    Resources.maxAtomicCounterBindings                  = 1;
    Resources.maxVertexAtomicCounterBuffers             = 0;
    Resources.maxTessControlAtomicCounterBuffers        = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers     = 0;
    Resources.maxGeometryAtomicCounterBuffers           = 0;
    Resources.maxFragmentAtomicCounterBuffers           = 1;
    Resources.maxCombinedAtomicCounterBuffers           = 1;
    Resources.maxAtomicCounterBufferSize                = 16384;
    Resources.maxTransformFeedbackBuffers               = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances                          = 8;
    Resources.maxCombinedClipAndCullDistances           = 8;
    Resources.maxSamples                                = 4;
    Resources.maxMeshOutputVerticesNV                   = 256;
    Resources.maxMeshOutputPrimitivesNV                 = 512;
    Resources.maxMeshWorkGroupSizeX_NV                  = 32;
    Resources.maxMeshWorkGroupSizeY_NV                  = 1;
    Resources.maxMeshWorkGroupSizeZ_NV                  = 1;
    Resources.maxTaskWorkGroupSizeX_NV                  = 32;
    Resources.maxTaskWorkGroupSizeY_NV                  = 1;
    Resources.maxTaskWorkGroupSizeZ_NV                  = 1;
    Resources.maxMeshViewCountNV                        = 4;

    Resources.limits.nonInductiveForLoops                 = 1;
    Resources.limits.whileLoops                           = 1;
    Resources.limits.doWhileLoops                         = 1;
    Resources.limits.generalUniformIndexing               = 1;
    Resources.limits.generalAttributeMatrixVectorIndexing = 1;
    Resources.limits.generalVaryingIndexing               = 1;
    Resources.limits.generalSamplerIndexing               = 1;
    Resources.limits.generalVariableIndexing              = 1;
    Resources.limits.generalConstantMatrixVectorIndexing  = 1;

    return Resources;
}


// Thanks to https://stackoverflow.com/a/217605
// Trim from the start (in place)
static void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}
// Thanks to https://stackoverflow.com/a/217605
// Trim from the end (in place)
static void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}
// Thanks to https://stackoverflow.com/a/14266139
std::vector<std::string> split(std::string s, std::string const &delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    std::string token;
    while((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        tokens.push_back(token);
        s.erase(0, pos + delimiter.length());
    }
    tokens.push_back(s);

    return tokens;
}

struct ShaderStage
{
    std::string source;
    std::string name;
    VkShaderStageFlagBits stage;
};
static void printUsage()
{
    std::vector<std::string> names;
    for(auto const &[name, _] : gStageNameToVulkanEnum)
        names.emplace_back(name);

    LOG_ERROR("Usage: #stage \"<stage name>\" \"[optional stage label (identify the stage)]\" -- declare a new shader stage");
    LOG_ERROR("Valid stage names: {}", names);
    LOG_ERROR("#stage all will make a preamble for each shader");
}

// Split sources
static std::vector<ShaderStage> splitSource(Shader &program)
{
    std::vector<ShaderStage> stages(1);
    uint32_t currentStage = 0; // 0 - preamble
    std::istringstream stream(program.source);

    std::string preamble;
    preamble.append("#extension GL_GOOGLE_cpp_style_line_directive : enable\n");
    preamble.append("#extension GL_GOOGLE_include_directive : enable\n"); // Parser refuses to process the includer without the extension enabled
    for(auto const &[name, value] : program.createInfo.definitions)
        preamble.append("#define " + name + ' ' + value + '\n');

    size_t lineNum = 1;
    std::string line;
    while(std::getline(stream, line)) 
    {
        auto directive = line.find_first_not_of(" \t");
        if(directive != std::string::npos && line.compare(directive, STAGE_IDENTIFIER.size(), STAGE_IDENTIFIER) == 0) {
            line = line.substr(directive + STAGE_IDENTIFIER.size());
            auto tokens = split(line, "\" \"");
            for(auto &token : tokens)
            {
                ltrim(token);
                rtrim(token);
                std::erase(token, '\"');
            }
            if(tokens.empty() || tokens.size() > 2)
            {
                LOG_ERROR("\"{}:{}\": invalid tokens!", program.createInfo.src, lineNum);
                printUsage();
                continue;
            }

            auto stageName = tokens[0];

            if(gStageNameToVulkanEnum.find(stageName) != gStageNameToVulkanEnum.end()) {
                auto stage = gStageNameToVulkanEnum.at(stageName);
                if(stage == VK_SHADER_STAGE_ALL) {
                    currentStage = 0;
                } else {
                    stages.emplace_back(ShaderStage{.stage = stage});
                    currentStage = stages.size() - 1;
                }
            } else {
                LOG_ERROR("\"{}:{}\": unknown stage name: \"{}\"", program.createInfo.src, line, stageName);
                printUsage();
                continue;
            }

            stages[currentStage].source.append("#line " + std::to_string(lineNum) + " \"" + program.createInfo.src + "\"\n\n");
            if(tokens.size() >= 2)
                stages[currentStage].name = tokens[1];
        } else if(directive != std::string::npos && line.compare(directive, VERSION_IDENTIFIER.size(), VERSION_IDENTIFIER) == 0) {
            stages[currentStage].source.insert(0, line + '\n' + preamble);
            stages[currentStage].source.append("#line " + std::to_string(lineNum) + " \"" + program.createInfo.src + "\"\n\n");
        } else {
            stages[currentStage].source.append(line + '\n');
        }

        ++lineNum;
    }

    auto all = stages[0];
    stages.erase(stages.begin()); // remove preamble
    for(auto &stage : stages)
    {
        stage.source.insert(0, all.source);

        // move #version to top of the source
        size_t versionPos = stage.source.find("#version");
        if(versionPos != std::string::npos)
        {
            auto lineSize = stage.source.find('\n', versionPos) - versionPos + 1;
            auto version = stage.source.substr(versionPos, lineSize);
            stage.source.erase(versionPos, lineSize);
            stage.source.insert(0, version);
        }
    }

    return stages;
}
// Compile the sources using glslang
static bool compileSources(Shader &program, std::vector<ShaderStage> const &sources)
{
    [[maybe_unused]] static class GlslangProcess
    {
    public:
        GlslangProcess() { glslang::InitializeProcess(); }
        ~GlslangProcess() { glslang::FinalizeProcess(); }
    } process;

    DirStackFileIncluder includer;
    for(auto const &dir : program.createInfo.includeDirs | std::views::reverse)
        includer.pushExternalLocalDirectory(dir);
    for(auto const &dir : program.createInfo.systemIncludeDirs | std::views::reverse)
        includer.pushExternalSystemDirectory(dir);
    includer.pushExternalLocalDirectory(fs::path(program.createInfo.src).parent_path().string());

    glslang::TProgram glslProgram;
    EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    std::vector<std::unique_ptr<glslang::TShader>> glslShaders;
    static auto resources = InitResources();

    for(auto &stage : sources)
    {
        if(!gVulkanStageToGlslang.contains(stage.stage))
        {
            LOG_WARN("Unknown shader stage: {}", string_VkShaderStageFlagBits(stage.stage));
            continue;
        }

        auto glslShader = std::make_unique<glslang::TShader>(gVulkanStageToGlslang.at(stage.stage));
        auto cString = stage.source.c_str();
        int l = stage.source.size();
        auto name = program.createInfo.src.c_str();

        glslShader->setEnvInput(glslang::EShSourceGlsl, gVulkanStageToGlslang.at(stage.stage), glslang::EShClientVulkan, 100);
        glslShader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        glslShader->setEnvTarget(glslang::EshTargetSpv, glslang::EShTargetSpv_1_6);
        glslShader->setDebugInfo(program.createInfo.debugInfo);
        glslShader->setSourceFile(program.createInfo.src.c_str());
        // glslShader->setPreamble(preamble.c_str());
        glslShader->setStringsWithLengthsAndNames(&cString, &l, &name, 1);

        if(!glslShader->parse(&resources, 130, false, messages, includer))
        {
            LOG_ERROR("GLSL parsing of \"{}\" failed: \n{}\n{}", program.createInfo.src, glslShader->getInfoLog(), glslShader->getInfoDebugLog());
            return false;
        }

        glslProgram.addShader(glslShader.get());
        glslShaders.emplace_back(std::move(glslShader));
    }
    if(!glslProgram.link(messages))
    {
        LOG_ERROR("GLSL linking of \"{}\" failed: \n{}\n{}", program.createInfo.src, glslProgram.getInfoLog(), glslProgram.getInfoDebugLog());
        return false;
    }


    for(auto &stage : sources)
    {
        if(!gVulkanStageToGlslang.contains(stage.stage))
        {
            LOG_WARN("Unknown shader stage: {}", string_VkShaderStageFlagBits(stage.stage));
            continue;
        }

        auto &bin = program.binaries.emplace_back();
        bin.stage = stage.stage;
        bin.name = stage.name;

        glslang::TIntermediate *intermediate = glslProgram.getIntermediate(gVulkanStageToGlslang.at(stage.stage));
        spv::SpvBuildLogger logger;
        glslang::SpvOptions options;

        glslang::GlslangToSpv(*intermediate, bin.spirv, &logger, &options);

        if(!logger.getAllMessages().empty())
            LOG_WARN("Stage {} \"{}\" from \"{}\" spirv gen messages:\n{}", string_VkShaderStageFlagBits(stage.stage), stage.name, program.createInfo.src, logger.getAllMessages());
    }

    return true;
}


Shader vk::makeShader(ShaderCreateInfo const &ci)
{
    Shader program;
    program.createInfo = ci;
    if(!fs::exists(program.createInfo.src) || !fs::is_regular_file(program.createInfo.src))
    {
        LOG_ERROR("Invalid src path: \"{}\"", program.createInfo.src);
        return program;
    }
    if(fs::exists(program.createInfo.bin) && !fs::is_directory(program.createInfo.bin))
    {
        LOG_ERROR("Invalid bin path: \"{}\"", program.createInfo.bin);
        return program;
    }

    bool canCompile = fs::exists(program.createInfo.src);
    bool canCollect = fs::exists(program.createInfo.bin) && fs::is_directory(program.createInfo.bin);
    bool canWrite   = !program.createInfo.bin.empty();

    if(canCompile)
        program.source = readFileString(program.createInfo.src);

    bool outdated = fs::exists(program.createInfo.src) && fs::exists(program.createInfo.bin) && (std::filesystem::last_write_time(program.createInfo.src).time_since_epoch() > std::filesystem::last_write_time(program.createInfo.bin).time_since_epoch());
    outdated = outdated || program.createInfo.force;
    if(outdated && canCompile)
        LOG_INFO("\"{}\" shader binaries are outdated! Recompiling", program.createInfo.bin);

    if(canCollect && !outdated) 
    {
        LOG_TRACE("Collecting binaries from \"{}\"", program.createInfo.bin);
        collectBinaries(program);
    } else if(canCompile) {
        LOG_TRACE("Compiling from source \"{}\"", program.createInfo.src);

        if(!compileSources(program, splitSource(program)))
            return program;

        if(canWrite)
        {
            fs::remove_all(program.createInfo.bin);
            for(auto &bin : program.binaries)
            {
                auto name = bin.name == "" ? "" : "." + bin.name;
                bin.path = (fs::path(program.createInfo.bin)/string_VkShaderStageFlagBits(bin.stage)).string() + name + ".spv";
                writeFileBinary(bin.path, reinterpret_cast<char const *>(bin.spirv.data()), bin.spirv.size() * sizeof(bin.spirv[0]));
            }
    
            fs::last_write_time(program.createInfo.bin, std::chrono::file_clock::now());
        }
    } else {
        LOG_ERROR("Cannot find binaries in \"{}\" or compile from source \"{}\" shaders!", program.createInfo.bin, program.createInfo.src);
        return program;
    }

    // Create vulkan modules from binaries.
    if(program.createInfo.device)
    {
        for(auto &bin : program.binaries)
        {
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = bin.spirv.size() * sizeof(bin.spirv[0]);
            createInfo.pCode = bin.spirv.data();
            VK_CHK(vkCreateShaderModule(program.createInfo.device, &createInfo, nullptr, &bin.module));
        }
    } else {
        LOG_WARN("Not creating shader modules for \"{}\"/\"{}\", because device is VK_NULL_HANDLE.", program.createInfo.src, program.createInfo.bin);
    }

    program.valid = true;
    return program;
}

void vk::destroy(Shader &shader)
{
    for(auto &bin : shader.binaries)
    {
        if(bin.module && shader.createInfo.device)
        {
            vkDestroyShaderModule(shader.createInfo.device, bin.module, nullptr);
            bin.module = VK_NULL_HANDLE;
        }
    }
    shader.valid = false;
}
