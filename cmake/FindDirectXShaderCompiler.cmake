
find_path(DirectXShaderCompiler_INCLUDE_DIR
    NAMES dxcapi.h
    PATH_SUFFIXES include include/dxc
)

find_library(DirectXShaderCompiler_LIBRARY
    NAMES dxcompiler
    PATH_SUFFIXES lib bin x64/bin arm64/bin
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DirectXShaderCompiler
    REQUIRED_VARS DirectXShaderCompiler_INCLUDE_DIR DirectXShaderCompiler_LIBRARY
)

if(DirectXShaderCompiler_FOUND AND NOT TARGET Microsoft::DirectXShaderCompiler)
    add_library(Microsoft::DirectXShaderCompiler SHARED IMPORTED)
    set_target_properties(Microsoft::DirectXShaderCompiler PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DirectXShaderCompiler_INCLUDE_DIR}"
    )

    if(WIN32)
        set_target_properties(Microsoft::DirectXShaderCompiler PROPERTIES
            IMPORTED_IMPLIB "${DirectXShaderCompiler_LIBRARY}"
        )
    else()
        set_target_properties(Microsoft::DirectXShaderCompiler PROPERTIES
            IMPORTED_LOCATION "${DirectXShaderCompiler_LIBRARY}"
        )
    endif()
endif()