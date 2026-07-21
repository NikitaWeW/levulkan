#include "Shader.hpp"
#include "Utility.hpp"
#include "Logging.hpp"
#include "nlohmann/json.hpp"
#include "spirv-tools/optimizer.hpp"

#ifdef SHADER_ENABLE_GLSL
#include "glslang/Public/ShaderLang.h"
#include "glslang/SPIRV/GlslangToSpv.h"
#endif

#ifdef SHADER_ENABLE_SLANG
#include "slang.h"
#include "slang-com-ptr.h"
#endif

#include <filesystem>
#include <set>
#include <ranges>

using namespace vk;
namespace fs = std::filesystem;
using namespace nlohmann;

/// @brief Default include class for normal include convention
/// of search backward through the stack of active include paths (for nested includes).
/// Source: https://github.com/KhronosGroup/glslang StandAlone/DirStackFileIncluder.h
/// Modified to support system includes.
class DirStackFileIncluder {
private:
    // If no path markers, return current working directory.
    // Otherwise, strip file name and return path leading up to it.
    virtual std::string getDirectory(std::string_view path) const
    {
        size_t last = path.find_last_of("/\\");
        return last == std::string::npos ? "." : std::string(path.substr(0, last));
    }
protected:
    std::vector<std::string> mLocalDirectoryStack;
    int mLocalDirectoryCount = 0;
    std::vector<std::string> mSystemDirectoryStack;
    int mSystemDirectoryCount = 0;
    std::set<std::string> mIncludedFiles;
public:
    void pushLocal(std::string_view dir) {
        mLocalDirectoryStack.emplace_back(dir);
        mLocalDirectoryCount = (int) mLocalDirectoryStack.size();
    }
    void pushSystem(std::string_view dir) {
        mSystemDirectoryStack.emplace_back(dir);
        mSystemDirectoryCount = (int) mSystemDirectoryStack.size();
    }

    std::string resolveLocal(std::string_view headerName, std::string_view includerName, size_t inclusionDepth) {
        return resolveInclude(headerName, includerName, mLocalDirectoryCount, inclusionDepth, mLocalDirectoryStack);
    }
    std::string resolveSystem(std::string_view headerName, std::string_view includerName, size_t inclusionDepth) {
        return resolveInclude(headerName, includerName, mSystemDirectoryCount, inclusionDepth, mSystemDirectoryStack);
    }

    std::set<std::string> const &getIncludedFiles() const { return mIncludedFiles; }

    
    std::string resolveInclude(std::string_view headerName, std::string_view includerName, int externalDirectoryCount, int depth, std::vector<std::string> &stack)
    {
        // Discard popped include directories, and
        // initialize when at parse-time first level.
        stack.resize(depth + externalDirectoryCount);
        if(depth == 1)
            stack.back() = getDirectory(includerName);

        // Find a directory that works, using a reverse search of the include stack.
        for(auto it = stack.rbegin(); it != stack.rend(); ++it) {
            std::string path = *it + '/' + headerName.data();
            std::replace(path.begin(), path.end(), '\\', '/');
            if(fs::exists(path)) {
                stack.push_back(getDirectory(path));
                mIncludedFiles.insert(path);
                return path;
            }
        }

        return "";
    }
};
struct ShaderStage {
    std::string source;
    std::string name;
    std::string sourceFile;
    VkShaderStageFlagBits stage;
};

static constexpr std::string_view STAGE_IDENTIFIER = "#stage";
static constexpr std::string_view METADATA_FILENAME = "metadata.json";

static const std::unordered_map<std::string, VkShaderStageFlagBits> gExtensionToVulkanEnum = {
    {"vert",  VK_SHADER_STAGE_VERTEX_BIT},
    {"vs",    VK_SHADER_STAGE_VERTEX_BIT},
    {"vsh",   VK_SHADER_STAGE_VERTEX_BIT},
    {"frag",  VK_SHADER_STAGE_FRAGMENT_BIT},
    {"fs",    VK_SHADER_STAGE_FRAGMENT_BIT},
    {"fsh",   VK_SHADER_STAGE_FRAGMENT_BIT},
    {"ps",    VK_SHADER_STAGE_FRAGMENT_BIT},
    {"comp",  VK_SHADER_STAGE_COMPUTE_BIT},
    {"csh",   VK_SHADER_STAGE_COMPUTE_BIT},
    {"cs",    VK_SHADER_STAGE_COMPUTE_BIT},
    {"geom",  VK_SHADER_STAGE_GEOMETRY_BIT},
    {"gsh",   VK_SHADER_STAGE_GEOMETRY_BIT},
    {"gs",    VK_SHADER_STAGE_GEOMETRY_BIT},
    {"tesc",  VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
    {"tcs",   VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
    {"tese",  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
    {"tes",   VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
    {"rgen",  VK_SHADER_STAGE_RAYGEN_BIT_KHR},
    {"rint",  VK_SHADER_STAGE_INTERSECTION_BIT_KHR},
    {"rahit", VK_SHADER_STAGE_ANY_HIT_BIT_KHR},
    {"rchit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
    {"rmiss", VK_SHADER_STAGE_MISS_BIT_KHR},
    {"rcall", VK_SHADER_STAGE_CALLABLE_BIT_KHR},
    {"task",  VK_SHADER_STAGE_TASK_BIT_EXT},
    {"mesh",  VK_SHADER_STAGE_MESH_BIT_EXT},
    {"ms",    VK_SHADER_STAGE_MESH_BIT_EXT},
    {"msh",   VK_SHADER_STAGE_MESH_BIT_EXT},
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
static std::vector<T> readFileBinary(std::string_view filename)  {
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
static std::string readFileString(std::string_view filename)  {
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
static void writeFileBinary(std::string_view filename, char const *data, size_t size) {
    auto dir = fs::path(filename).parent_path().string();
    if(!dir.empty())
        std::filesystem::create_directories(dir);
    std::ofstream file(std::string{filename}, std::ios::out | std::ios::binary | std::ios::trunc);
    assert(file);

    file.write(data, size);
}
static void writeFileString(std::string_view filename, std::string_view str) {
    auto dir = fs::path(filename).parent_path().string();
    if(!dir.empty())
        std::filesystem::create_directories(dir);
    std::ofstream file(std::string{filename}, std::ios::out | std::ios::trunc);
    assert(file);

    file << str;
}
static void collectBinaries(Shader &program, json const &metadata) {
    for(auto const &entry : metadata["binaries"])
    {
        auto stage      = entry["stage"].get<std::string>();
        auto binPath    = entry["bin"].get<std::string>();
        auto name       = entry["name"].get<std::string>();
        auto entryPoint = entry["entry"].get<std::string>();

        assert(fs::exists(binPath));
        assert(gVulkanStageStringToEnum.contains(stage));

        uint32_t binIndex = 0;
        for(; binIndex < program.binaries.size(); ++binIndex)
        {
            if(program.binaries[binIndex].path == binPath)
                break;
        }
        if(binIndex >= program.binaries.size())
            program.binaries.emplace_back(Shader::Binary{
                .path = binPath,
                .spirv = readFileBinary<uint32_t>(binPath),
            });

        
        program.binDescriptors.emplace_back(Shader::BinDescriptor{
            .stage = gVulkanStageStringToEnum.at(stage),
            .name = name,
            .entry = entryPoint,
            .binary = binIndex
        });
    }
}
static void writeBinaries(Shader &program, std::vector<std::string> const &includes) {
    json metadata;
    metadata["src"] = program.createInfo.src;
    metadata["includes"] = includes;
    fs::remove_all(program.createInfo.bin);
    for(uint i = 0; i < program.binaries.size(); ++i)
    {
        auto &bin = program.binaries[i];
        bin.path = fs::path(program.createInfo.bin)/(std::to_string(i) + ".spv");
        writeFileBinary(bin.path, reinterpret_cast<char const *>(bin.spirv.data()), bin.spirv.size() * sizeof(bin.spirv[0]));
    }

    for(auto const &desc : program.binDescriptors)
    {
        auto const &bin = program.binaries[desc.binary];

        auto &entry = metadata["binaries"].emplace_back();
        entry["bin"]     = bin.path;
        entry["name"]    = desc.name;
        entry["entry"]   = desc.entry;
        entry["stage"]   = string_VkShaderStageFlagBits(desc.stage);
    }

    writeFileString((fs::path(program.createInfo.bin)/METADATA_FILENAME).string(), metadata.dump(4));
    fs::last_write_time(program.createInfo.bin, std::chrono::file_clock::now());
}
static bool isOutdated(Shader &program, json const &metadata) {
    if(!fs::exists(program.createInfo.bin))
        return false;
    if(program.createInfo.force)
        return true;

    bool outdated = false; 
    auto binWriteTime = std::filesystem::last_write_time(program.createInfo.bin).time_since_epoch();
    if(fs::exists(program.createInfo.src) && fs::exists(program.createInfo.bin))
    {
        if(metadata["src"].get<std::string>() != program.createInfo.src)
            return true;

        outdated = (std::filesystem::last_write_time(program.createInfo.src).time_since_epoch() > binWriteTime);
        for(auto include : metadata["includes"])
        {
            if(outdated)
                return true;
            outdated = (std::filesystem::last_write_time(include.get<std::string>()).time_since_epoch() > binWriteTime);
        }

        if(fs::is_directory(program.createInfo.src)) {
            for(auto dirEntry : fs::recursive_directory_iterator(program.createInfo.src)) {
                if(outdated)
                    return true;
                outdated = dirEntry.last_write_time().time_since_epoch() > binWriteTime;
            }
        }
    }

    return outdated;
}
static std::vector<ShaderStage> collectSources(Shader &program) {
    std::vector<ShaderStage> stages;
    assert(fs::exists(program.createInfo.src) && fs::is_directory(program.createInfo.src));

    for(auto dirEntry : fs::recursive_directory_iterator(program.createInfo.src)) {
        if(fs::is_directory(dirEntry))
            continue;

        auto extension = dirEntry.path().extension().string();
        if(!extension.empty())
            extension.erase(0, 1);
        
        if(!gExtensionToVulkanEnum.contains(extension)) {
            LOG_WARN("Unknown file in source tree \"{}\": \"{}\"", program.createInfo.src, dirEntry.path().string());
            continue;
        }

        stages.emplace_back(ShaderStage{
            .source = readFileString(dirEntry.path().string()),
            .name   = dirEntry.path().stem().string(),
            .sourceFile   = dirEntry.path().string(),
            .stage  = gExtensionToVulkanEnum.at(extension),
        });
    }

    return stages;
}

static void optimizeSpirv(Shader &program) {
    // TODO
}
static void obfuscateSpirv(Shader &program) {
    // TODO
}

#ifdef SHADER_ENABLE_GLSL
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
static void printUsage() {
    std::vector<std::string> names;
    for(auto const &[name, _] : gStageNameToVulkanEnum)
        names.emplace_back(name);

    LOG_ERROR("Usage: #stage \"<stage name>\" \"[optional stage label (identify the stage)]\" -- declare a new shader stage");
    LOG_ERROR("Valid stage names: {}", names);
    LOG_ERROR("#stage all will make a preamble for each shader");
}
class GlslIncluder : protected DirStackFileIncluder, public glslang::TShader::Includer {
public:
    GlslIncluder() = default;

    virtual IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override {
        return newIncludeResult(resolveLocal(headerName, includerName, inclusionDepth));
    }

    virtual IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override {
        return newIncludeResult(resolveSystem(headerName, includerName, inclusionDepth));
    }

    // Externally set directories. E.g., from a command-line -I<dir>.
    //  - Most-recently pushed are checked first.
    //  - All these are checked after the parse-time stack of local directories
    //    is checked.
    //  - This only applies to the "local" form of #include.
    //  - Makes its own copy of the path.
    virtual void pushExternalLocalDirectory(std::string const &dir)
    {
        DirStackFileIncluder::pushLocal(dir);
    }

    virtual void pushExternalSystemDirectory(std::string const &dir)
    {
        DirStackFileIncluder::pushSystem(dir);
    }

    virtual void releaseInclude(IncludeResult* result) override
    {
        if (result != nullptr) {
            delete [] static_cast<char*>(result->userData);
            delete result;
        }
    }

    virtual std::set<std::string> getIncludedFiles()
    {
        return DirStackFileIncluder::getIncludedFiles();
    }

    virtual ~GlslIncluder() override { }

protected:
    // Do actual reading of the file, filling in a new include result.
    virtual IncludeResult* newIncludeResult(std::string const &path) const
    {
        std::ifstream file(path, std::ios::ate);
        if(!file)
            return nullptr;
        
        int length = file.tellg();
        char* content = new char [length];
        file.seekg(0, file.beg);
        file.read(content, length);
        return new IncludeResult(path, content, length, content);
    }
};
static const std::unordered_map<VkShaderStageFlagBits, EShLanguage> gVulkanStageToGlslang = {
    {VK_SHADER_STAGE_VERTEX_BIT                  , EShLangVertex         },
    {VK_SHADER_STAGE_GEOMETRY_BIT                , EShLangGeometry       },
    {VK_SHADER_STAGE_FRAGMENT_BIT                , EShLangFragment       },
    {VK_SHADER_STAGE_COMPUTE_BIT                 , EShLangCompute        },
    {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT , EShLangTessEvaluation },
    {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT    , EShLangTessControl    },
    {VK_SHADER_STAGE_RAYGEN_BIT_KHR              , EShLangRayGen         },
    {VK_SHADER_STAGE_INTERSECTION_BIT_KHR        , EShLangIntersect      },
    {VK_SHADER_STAGE_ANY_HIT_BIT_KHR             , EShLangAnyHit         },
    {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR         , EShLangClosestHit     },
    {VK_SHADER_STAGE_MISS_BIT_KHR                , EShLangMiss           },
    {VK_SHADER_STAGE_CALLABLE_BIT_KHR            , EShLangCallable       },
    {VK_SHADER_STAGE_MESH_BIT_EXT                , EShLangMesh           },
};
static const std::unordered_map<uint32_t, glslang::EshTargetClientVersion> gVulkanVersionToGlslang = {
    {VK_API_VERSION_1_0, glslang::EShTargetVulkan_1_0},
    {VK_API_VERSION_1_1, glslang::EShTargetVulkan_1_1},
    {VK_API_VERSION_1_2, glslang::EShTargetVulkan_1_2},
    {VK_API_VERSION_1_3, glslang::EShTargetVulkan_1_3},
    {VK_API_VERSION_1_4, glslang::EShTargetVulkan_1_4},
};
static const std::unordered_map<SpirvVersion, glslang::EShTargetLanguageVersion> gSpirvVersionToGlslang = {
    { SpirvVersion::SpirvVersion_1_0, glslang::EShTargetLanguageVersion::EShTargetSpv_1_0 },
    { SpirvVersion::SpirvVersion_1_1, glslang::EShTargetLanguageVersion::EShTargetSpv_1_1 },
    { SpirvVersion::SpirvVersion_1_2, glslang::EShTargetLanguageVersion::EShTargetSpv_1_2 },
    { SpirvVersion::SpirvVersion_1_3, glslang::EShTargetLanguageVersion::EShTargetSpv_1_3 },
    { SpirvVersion::SpirvVersion_1_4, glslang::EShTargetLanguageVersion::EShTargetSpv_1_4 },
    { SpirvVersion::SpirvVersion_1_5, glslang::EShTargetLanguageVersion::EShTargetSpv_1_5 },
    { SpirvVersion::SpirvVersion_1_6, glslang::EShTargetLanguageVersion::EShTargetSpv_1_6 },
};

// Thanks to https://github.com/KhronosGroup/glslang/issues/2207#issuecomment-632927839
static TBuiltInResource InitResources() {
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
static std::vector<ShaderStage> splitGlslSources(Shader &program) {
    std::vector<ShaderStage> stages(1);
    uint32_t currentStage = 0; // 0 - preamble
    auto source = readFileString(program.createInfo.src);
    std::istringstream stream(source);

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
                    stages.emplace_back(ShaderStage{
                        .sourceFile = program.createInfo.src,
                        .stage = stage, 
                    });
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
        // } else if(directive != std::string::npos && line.compare(directive, #version.size(), #version) == 0) {
        //     stages[currentStage].source.insert(0, line + '\n' + preamble);
        //     stages[currentStage].source.append("#line " + std::to_string(lineNum) + " \"" + program.createInfo.src + "\"\n\n");
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
static bool compileGlsl(Shader &program, std::vector<std::string> &outIncludes) {
    [[maybe_unused]] static class GlslangProcess
    {
    public:
        GlslangProcess() { glslang::InitializeProcess(); }
        ~GlslangProcess() { glslang::FinalizeProcess(); }
    } process;

    GlslIncluder includer;
    for(auto const &dir : program.createInfo.includeDirs | std::views::reverse)
        includer.pushExternalLocalDirectory(dir);
    for(auto const &dir : program.createInfo.systemIncludeDirs | std::views::reverse)
        includer.pushExternalSystemDirectory(dir);
    includer.pushExternalLocalDirectory(fs::path(program.createInfo.src).parent_path().string());

    glslang::TProgram glslProgram;
    EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    std::vector<std::unique_ptr<glslang::TShader>> glslShaders;
    static auto resources = InitResources();

    std::vector<ShaderStage> sources;
    assert(fs::exists(program.createInfo.src));
    if(fs::is_directory(program.createInfo.src)) {
        sources = collectSources(program);
    } else {
        sources = splitGlslSources(program);
    }

    std::string preamble;
    preamble.append("#extension GL_GOOGLE_cpp_style_line_directive : enable\n");
    preamble.append("#extension GL_GOOGLE_include_directive : enable\n"); // Parser refuses to process the includer without the extension enabled
    for(auto const &[name, value] : program.createInfo.definitions)
        preamble.append("#define " + name + ' ' + value + '\n');

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
        auto name = stage.sourceFile.c_str();

        glslShader->setEnvInput(glslang::EShSourceGlsl, gVulkanStageToGlslang.at(stage.stage), glslang::EShClientVulkan, 100);
        glslShader->setEnvClient(glslang::EShClientVulkan, gVulkanVersionToGlslang.at(program.createInfo.targetVersion));
        glslShader->setEnvTarget(glslang::EshTargetSpv, gSpirvVersionToGlslang.at(program.createInfo.spirvVersion));
        glslShader->setDebugInfo(program.createInfo.debugInfo);
        glslShader->setSourceFile(stage.sourceFile.c_str());
        glslShader->setPreamble(preamble.c_str());
        glslShader->setStringsWithLengthsAndNames(&cString, &l, &name, 1);

        if(!glslShader->parse(&resources, 130, false, messages, includer))
        {
            LOG_ERROR("GLSL parsing of \"{}\" failed: \n{}\n{}", stage.sourceFile, glslShader->getInfoLog(), glslShader->getInfoDebugLog());
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

        auto &desc = program.binDescriptors.emplace_back();
        desc.stage = stage.stage;
        desc.name  = stage.name;
        desc.entry = "main";
        desc.binary = program.binaries.size();
        auto &bin = program.binaries.emplace_back();

        glslang::TIntermediate *intermediate = glslProgram.getIntermediate(gVulkanStageToGlslang.at(stage.stage));
        spv::SpvBuildLogger logger;
        glslang::SpvOptions options;

        glslang::GlslangToSpv(*intermediate, bin.spirv, &logger, &options);

        if(!logger.getAllMessages().empty())
            LOG_WARN("Stage {} \"{}\" from \"{}\" spirv gen messages:\n{}", string_VkShaderStageFlagBits(stage.stage), stage.name, program.createInfo.src, logger.getAllMessages());
    }

    outIncludes.reserve(includer.getIncludedFiles().size());
    for(auto const &include : includer.getIncludedFiles())
        outIncludes.emplace_back(include);

    return true;
}
#endif

#ifdef SHADER_ENABLE_SLANG
static std::unordered_map<ShaderCreateInfo::Optimization, SlangOptimizationLevel> gOptimizationToSlang{
    {ShaderCreateInfo::Optimization::None,       SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_NONE   },
    {ShaderCreateInfo::Optimization::Default,    SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_HIGH   },
    {ShaderCreateInfo::Optimization::Aggressive, SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_MAXIMAL},
};
static std::unordered_map<SlangStage, VkShaderStageFlagBits> gSlangToVulkanStage{
    { SLANG_STAGE_VERTEX,          VK_SHADER_STAGE_VERTEX_BIT                  },      
    { SLANG_STAGE_HULL,            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT    },
    { SLANG_STAGE_DOMAIN,          VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT },
    { SLANG_STAGE_GEOMETRY,        VK_SHADER_STAGE_GEOMETRY_BIT                },    
    { SLANG_STAGE_FRAGMENT,        VK_SHADER_STAGE_FRAGMENT_BIT                },    
    { SLANG_STAGE_COMPUTE,         VK_SHADER_STAGE_COMPUTE_BIT                 },     
    { SLANG_STAGE_RAY_GENERATION,  VK_SHADER_STAGE_RAYGEN_BIT_KHR              },  
    { SLANG_STAGE_INTERSECTION,    VK_SHADER_STAGE_INTERSECTION_BIT_KHR        },
    { SLANG_STAGE_ANY_HIT,         VK_SHADER_STAGE_ANY_HIT_BIT_KHR             }, 
    { SLANG_STAGE_CLOSEST_HIT,     VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR         },
    { SLANG_STAGE_MISS,            VK_SHADER_STAGE_MISS_BIT_KHR                },    
    { SLANG_STAGE_CALLABLE,        VK_SHADER_STAGE_CALLABLE_BIT_KHR            },
    { SLANG_STAGE_MESH,            VK_SHADER_STAGE_MESH_BIT_EXT                },    
    { SLANG_STAGE_AMPLIFICATION,   VK_SHADER_STAGE_TASK_BIT_EXT                },
};
static void diagnoseSlang(Slang::ComPtr<slang::IBlob> const &diagnosticsBlob) {
    if(!diagnosticsBlob)
        return;

    spdlog::level::level_enum level;

    std::string_view str(static_cast<char const *>(diagnosticsBlob->getBufferPointer()));

    if(str.find(" warning ") < str.find(" error "))
        level = spdlog::level::warn;
    else 
        level = spdlog::level::err;

    LOG(level, "Slang diagnostics: {}", str);
}
static bool compileSlang(Shader &program, std::vector<std::string> &outIncludes) {
    [[maybe_unused]] static class SlangSession
    {
    public:
        SlangSession() {
            if(SLANG_FAILED(slang::createGlobalSession(session.writeRef()))) {
                LOG_ERROR("Failed to create slang session!");
            }
        }
        ~SlangSession() {  }
        Slang::ComPtr<slang::IGlobalSession> session;

        slang::IGlobalSession *operator->() { return session.get(); }
    } thread_local globalSession;
    
    if(!globalSession.session)
        return false;

    if(fs::is_directory(program.createInfo.src)) {
        LOG_ERROR("Slang shader src path \"{}\" can not be a directory!", program.createInfo.src);
        return false;
    }
    
    slang::TargetDesc targetDesc{
        .format = SLANG_SPIRV,
        .profile = globalSession->findProfile("spirv_1_6"),
    };

    std::vector<slang::PreprocessorMacroDesc> definitions;
    for(auto const &[name, value] : program.createInfo.definitions)
        definitions.emplace_back(name.c_str(), value.c_str());

    std::vector<slang::CompilerOptionEntry> options{
        slang::CompilerOptionEntry{slang::CompilerOptionName::EmitSpirvDirectly, {slang::CompilerOptionValueKind::Int, 1}},
        slang::CompilerOptionEntry{slang::CompilerOptionName::Optimization, {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(gOptimizationToSlang.at(program.createInfo.optimization))}},
        slang::CompilerOptionEntry{slang::CompilerOptionName::DebugInformation, {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(program.createInfo.debugInfo ? SlangDebugInfoLevel::SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SlangDebugInfoLevel::SLANG_DEBUG_INFO_LEVEL_NONE)}}
    };
    if(program.createInfo.obfuscate)
        options.emplace_back(slang::CompilerOptionEntry{slang::CompilerOptionName::Obfuscate, {slang::CompilerOptionValueKind::Int, 1}});

    std::vector<char const *> includeDirs;
    for(auto const &dir : program.createInfo.includeDirs)
        includeDirs.emplace_back(dir.c_str());
    for(auto const &dir : program.createInfo.systemIncludeDirs)
        includeDirs.emplace_back(dir.c_str());

    slang::SessionDesc sessionDesc{
        .targets = &targetDesc,
        .targetCount = 1,
        // .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
        .searchPaths = includeDirs.data(),
        .searchPathCount = static_cast<SlangInt>(includeDirs.size()),
        .preprocessorMacros = definitions.data(),
        .preprocessorMacroCount = static_cast<SlangInt>(definitions.size()),
        .compilerOptionEntries = options.data(),
        .compilerOptionEntryCount = static_cast<uint32_t>(options.size()),
    };

    Slang::ComPtr<slang::ISession> session;
    globalSession->createSession(sessionDesc, session.writeRef());

    std::vector<ShaderStage> sources;
    assert(fs::exists(program.createInfo.src));
    if(fs::is_directory(program.createInfo.src)) {
        sources = collectSources(program);
    } else {
        sources.emplace_back(ShaderStage{
            .source = readFileString(program.createInfo.src),
            .name = fs::path(program.createInfo.src).stem().string(),
            .sourceFile = program.createInfo.src,
            .stage = VK_SHADER_STAGE_ALL,
        });
    }

    std::unordered_set<std::string> includes;
    DirStackFileIncluder includerToParseIncludePathsBecauseSlangsIFilesystemIsUndocumentedPieceOfFuckingUnbelievableDumpsterFireAndSegfaultsForNoReasonAndWhyDontTheyJustAddAWayToAddACustomIncluderWithoutThisMessMaybeIDontWantToLoadShadersFromFileMaybeTheyAreInMemoryAlreadyAnywayItsNotTheCompilersResponsibility;
    auto &includer = includerToParseIncludePathsBecauseSlangsIFilesystemIsUndocumentedPieceOfFuckingUnbelievableDumpsterFireAndSegfaultsForNoReasonAndWhyDontTheyJustAddAWayToAddACustomIncluderWithoutThisMessMaybeIDontWantToLoadShadersFromFileMaybeTheyAreInMemoryAlreadyAnywayItsNotTheCompilersResponsibility;

    for(auto const &dir : program.createInfo.includeDirs)
        includer.pushLocal(dir);
    for(auto const &dir : program.createInfo.systemIncludeDirs)
        includer.pushSystem(dir);

    for(auto const &source : sources)
    {
        size_t pos = 0; 
        while(pos < source.source.size()) 
        {
            auto newLine = source.source.find('\n', pos + 1);
            auto directive = source.source.find_first_not_of(" \t", pos);
            if(directive < newLine && source.source.compare(directive, std::string_view("#include").size(), "#include") == 0)
            {
                auto begin = source.source.find_first_of("\"<", directive) + 1;
                auto end   = source.source.find_first_of("\">", begin);
                if(begin > newLine || end > newLine) {
                    LOG_WARN("incorrect include directive! {}", source.source.substr(directive, newLine - directive));
                } else {
                    bool local = source.source[begin-1] == '\"';
                    auto path = local ? includer.resolveLocal(source.source.substr(begin, end - begin), source.sourceFile, 1) : includer.resolveSystem(source.source.substr(begin, end - begin), source.sourceFile, 1);

                    if(path.empty())
                        LOG_WARN("Failed to include {}", source.source.substr(directive, newLine - directive));
                    else
                        includes.emplace(path);
                }
            }
            pos = newLine;
        }
    }
    outIncludes = std::vector<std::string>(includes.begin(), includes.end());

    struct EntryPoint {
        uint index;
        Slang::ComPtr<slang::IEntryPoint> entry;
    };
    struct Module {
        Slang::ComPtr<slang::IModule> module;
        std::vector<EntryPoint> entryPoints;
        uint sourceIndex;
    };
    std::vector<Module> modules;
    for(uint i = 0; i < sources.size(); ++i)
    {
        auto const &source = sources[i];
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        auto &module = modules.emplace_back(Module{
            .module = Slang::ComPtr(session->loadModuleFromSourceString(
                source.name.c_str(),
                source.sourceFile.c_str(),
                source.source.c_str(),
                diagnosticsBlob.writeRef())),
            .sourceIndex = i
        });

        diagnoseSlang(diagnosticsBlob);

        if(!module.module)
        {
            LOG_ERROR("Failed to load module \"{}\"", source.sourceFile);
            return false;
        }
    }

    uint entryPointIndex = 0;
    std::vector<slang::IComponentType *> componentTypes;
    for(auto &module : modules)
    {
        int32_t entryPointCount = module.module->getDefinedEntryPointCount();

        for(int32_t i = 0; i < entryPointCount; ++i)
            module.module->getDefinedEntryPoint(i, module.entryPoints.emplace_back(entryPointIndex++).entry.writeRef());

        componentTypes.emplace_back(module.module);
        for(auto const &entryPoint : module.entryPoints)
            componentTypes.emplace_back(entryPoint.entry);
    }


    Slang::ComPtr<slang::IComponentType> composedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = session->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef());
            
            diagnoseSlang(diagnosticsBlob);

        if(SLANG_FAILED(result)) {
            LOG_ERROR("Failed to compose the program \"{}\"!", program.createInfo.src);
            return false;
        }
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = composedProgram->link(
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef());
            
            diagnoseSlang(diagnosticsBlob);

        if(SLANG_FAILED(result)) {
            LOG_ERROR("Failed to link the program \"{}\"!", program.createInfo.src);
            return false;
        }
    }

    Slang::ComPtr<slang::IBlob> spirvCode;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = linkedProgram->getTargetCode(
            0, // targetIndex
            spirvCode.writeRef(),
            diagnosticsBlob.writeRef());
               
            diagnoseSlang(diagnosticsBlob);

        if(SLANG_FAILED(result)) {
            LOG_ERROR("Failed to link the program \"{}\"!", program.createInfo.src);
            return false;
        }
    }

    auto &binary = program.binaries.emplace_back();
    binary.spirv.resize(spirvCode->getBufferSize() / sizeof(binary.spirv[0]));
    std::memcpy(binary.spirv.data(), spirvCode->getBufferPointer(), spirvCode->getBufferSize());

    for(auto const &module : modules)
    {
        auto const &source = sources[module.sourceIndex];
        for(auto const &entry : module.entryPoints)
        {
            program.binDescriptors.emplace_back(Shader::BinDescriptor{
                .stage = gSlangToVulkanStage.at(linkedProgram->getLayout()->getEntryPointByIndex(entry.index)->getStage()),
                .name = source.name,
                .entry = entry.entry->getFunctionReflection()->getName(),
                .binary = static_cast<uint32_t>(program.binaries.size() - 1),
            });
        }
    }

    return true;
}
#endif

static const std::unordered_map<ShaderBackend, std::function<bool (Shader &, std::vector<std::string> &)>> gShaderBackends{
#ifdef SHADER_ENABLE_GLSL
    {ShaderBackend::GLSL, compileGlsl},
#endif
#ifdef SHADER_ENABLE_SLANG
    {ShaderBackend::SLANG, compileSlang},
#endif
};

Shader vk::makeShader(ShaderCreateInfo const &ci) {
    Shader program;
    program.createInfo = ci;

    auto metadataPath = fs::path(program.createInfo.bin)/METADATA_FILENAME;
    if(fs::exists(program.createInfo.bin) && !fs::exists(metadataPath))
    {
        LOG_ERROR("Missing {} in {}!", metadataPath.filename().string(), program.createInfo.bin);
        return program;
    }

    json metadata;
    if(fs::exists(metadataPath))
    {
        metadata = json::parse(readFileString(metadataPath.string()));
        assert(!metadata.empty());
    }

    bool canCompile = fs::exists(program.createInfo.src);
    bool canCollect = fs::exists(program.createInfo.bin);
    bool canWrite   = !program.createInfo.bin.empty();
    bool outdated   = isOutdated(program, metadata);

    if(outdated && canCompile)
        LOG_TRACE("\"{}\" shader binaries are outdated! Recompiling", program.createInfo.bin);

    if(canCollect && !outdated) {
        LOG_TRACE("Collecting binaries from \"{}\"", program.createInfo.bin);
        collectBinaries(program, metadata);
    }  else if(canCompile) {
        LOG_TRACE("Compiling from source \"{}\"", program.createInfo.src);

        if(program.createInfo.backend == ShaderBackend::NONE)
            LOG_ERROR("Backend not set!");
        if(!gShaderBackends.contains(program.createInfo.backend)) {
            LOG_ERROR("Backend not supported!");
            return program;
        }

        std::vector<std::string> includes;
        if(!gShaderBackends.at(program.createInfo.backend)(program, includes))
            return program;

        if(program.createInfo.optimization == ShaderCreateInfo::Optimization::Aggressive)
            optimizeSpirv(program);
        if(program.createInfo.obfuscate)
            obfuscateSpirv(program);

        if(canWrite)
            writeBinaries(program, includes);
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
            auto res = vkCreateShaderModule(program.createInfo.device, &createInfo, nullptr, &bin.module);

            if(res != VK_SUCCESS) {
                LOG_ERROR("Failed to create shader module for program \"{}\"/\"{}\"", program.createInfo.src, program.createInfo.bin);
                return program;
            }
        }
    } else {
        LOG_WARN("Not creating shader modules for \"{}\"/\"{}\", because device is VK_NULL_HANDLE.", program.createInfo.src, program.createInfo.bin);
    }

    program.valid = true;
    return program;
}

void vk::destroy(Shader &shader) {
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
