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

#include <vector>
#include <string>

namespace vk
{

struct ShaderCreateInfo
{
    std::string src; ///< The path to the source file. Can be empty to disable shader compilation.
    std::string bin; ///< The path to the binary root directory. Can be empty to disable writing and collecting shader binaries.
    VkDevice device = VK_NULL_HANDLE; ///< The logical device. Leave null to not create shader modules.
    std::vector<std::string> includeDirs; ///< Local ("") include directories. First most relevant. Source directory added implicitly.
    std::vector<std::string> systemIncludeDirs; ///< System (<>) include directories. First most relevant.
    std::vector<std::pair<std::string, std::string>> definitions; ///< Preprocessor definitions.
    bool debugInfo = true; ///< Compile with debug info.
    bool force = false; ///< Forcefully outdate the cached shader binaries and try to recompile.
};

/// @brief The compiled spirv program.
struct Shader
{
    struct Binary
    {
        VkShaderStageFlagBits stage;
        std::vector<uint32_t> spirv;
        VkShaderModule module = VK_NULL_HANDLE;
        std::string path = ""; ///< Path to the .spv binary
        std::string name = ""; ///< The name of the stage. Helps to identify the stage.
    };

    bool valid = false;
    std::vector<Binary> binaries;
    std::string source;
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