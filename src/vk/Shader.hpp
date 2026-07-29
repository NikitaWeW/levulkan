/*
                   ..         :             Vulkan helper functionality.
                 .+@-        =@#:           Copyright (c) 2026 Nikita Martynau 
                -@@@.        =@@@*          https://opensource.org/license/mit 
               *@@@%         =@@@@%.        insert git repo url here
             .%@@@@%         +@@@@@@:                    
             %@@@@@%         #@@@@@@@.      Custom GLSL preprocessor and glslang utility/wrapper.
            #@@@@@@@.       .@@@@@@@@#      Allows to have all the stages in one file via the new #stage directive
           =@@@@@@@@+       #@@@@@@@@@-     and manage spirv shader binaries.
           @@@@@@@@@@+.   :#@@@@@@@@@@%                  
          -@@@@@@@@@@@@##%@@@@@@@@@@@@@.    888    d8P   
          +@@@@@@@@@@@@@@@@@@@@@@@@@@@@-    888   d8P    
          *@@@@@@@@@@@@@@@@@@@@@@@@@@@@-    888  d8P     
          +@@@@@@@@@@@@@@@@@@@@@@@@@@@@.    888d88K      
          :@@@#+*%@@@@@@@@@@@@#**@@@@@#     8888888b     
           #@#    -%@@@@@@@@*:   :@@@@.     888  Y88b    
           .@@.    .%@@@@@@=     .@@@:      888   Y88b   
            .%%:    *@@@@@@     :%@#.       888    Y88b  
              +@#+=+@@@@@@@*--+%@%-   .-==:              
               :+%@@@@@@@@@@@@%*-    :+=.-+-             
               :-::-=+****++=-::---+***+-=+:             
                .==-----------=+****+-:.-:               
               .:--------=+=--==+=-                      
             :==-:=================-.                    
           .===:-=============-:=====-                   
           :=-:====:===========-:======                  
         .-+======--=====-:=====-:====:                  
      :-++*+======:=======:======-==-.                   
  .:===+=:.  .:---========-=====-:.                      
:==-:.             :==:::..-=:                           
 .                  --     :-                            
*/
#pragma once
#include "vulkan.h"
#include "spirv-tools/optimizer.hpp"
#include "spirv-tools/libspirv.hpp"
#include "spirv_reflect.h"

#include <map>
#include <optional>
#include <vector>
#include <string>

namespace vk {

enum class ShaderBackend {
    NONE = 0, GLSL, SLANG
};
enum class SpirvVersion {
    SpirvVersion_1_0 = (1 << 16),            
    SpirvVersion_1_1 = (1 << 16) | (1 << 8), 
    SpirvVersion_1_2 = (1 << 16) | (2 << 8), 
    SpirvVersion_1_3 = (1 << 16) | (3 << 8), 
    SpirvVersion_1_4 = (1 << 16) | (4 << 8), 
    SpirvVersion_1_5 = (1 << 16) | (5 << 8), 
    SpirvVersion_1_6 = (1 << 16) | (6 << 8), 
};
struct ShaderCreateInfo {
    ShaderBackend backend = ShaderBackend::NONE; ///< The source language.
    std::string src; ///< The path to the source. Can be empty to disable shader compilation. If src is a directory, the files inside the directory are collected as stages. The source can be split using #stage directives otherwise.
    std::string bin; ///< The path to the binary root directory. Can be empty to disable writing and collecting shader binaries.
    VkDevice device = VK_NULL_HANDLE; ///< The logical device. Leave null to not create shader modules.
    bool force = false; ///< Forcefully outdate the cached shader binaries and try to recompile.
    
    uint32_t targetVersion = VK_API_VERSION_1_3;
    SpirvVersion spirvVersion = SpirvVersion::SpirvVersion_1_6;
    std::vector<std::string> includeDirs; ///< Local ("") include directories. First most relevant. Source directory added implicitly.
    std::vector<std::string> systemIncludeDirs; ///< System (<>) include directories. First most relevant.
    std::vector<std::pair<std::string, std::string>> definitions; ///< Preprocessor definitions.
    bool debugInfo = true; ///< Compile with debug info.
    bool optimize = true; ///< Optimize source as well as spirv.
    bool reflect = true; ///< Generate spirv reflection.
    bool strip = false; ///< Strip reflection and debug info.
};

/// @brief The compiled spirv program.
struct Shader {
    struct BinDescriptor
    {
        VkShaderStageFlagBits stage;
        std::string name = ""; ///< The name of the stage.
        std::string entry = "main";
        uint32_t binary = 0;
    };
    struct Binary
    {
        std::string path = "";
        std::vector<uint32_t> spirv;
        VkShaderModule module = VK_NULL_HANDLE;
        std::optional<SpvReflectShaderModule> reflection;
    };

    bool valid = false;
    std::vector<BinDescriptor> binDescriptors;
    std::vector<Binary> binaries;
    ShaderCreateInfo createInfo;
};

/// @brief Make a shader program from the file.
/// Parser splits shaders stages using #shader directive
/// #stage all will append the block to all the defined stages.
/// At the beginning of the source the the stage is implicitly "#stage all "optional stage name/label""
/// For a full list of valid stage names look into Shader.cpp
/// @returns Shader with valid flag set to true if successful.
Shader makeShader(ShaderCreateInfo const &ci);
void destroy(Shader &shader);

} // namespace vk