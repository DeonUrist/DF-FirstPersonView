project(fpview)

set(FPVIEW_HDRS
    common.h
    farland.h
    media.h
    menu.inc
    models.h
    models_builtin.inc
    render.h
    render3d.h
    gpu.h
    scene.h
    sky.h
    sound.h
    spatter.h
    textures.h
)
set_source_files_properties(${FPVIEW_HDRS} PROPERTIES HEADER_FILE_ONLY TRUE)

dfhack_plugin(fpview
    fpview.cpp
    farland.cpp
    media.cpp
    models.cpp
    render.cpp
    render3d.cpp
    gpu.cpp
    scene.cpp
    sky.cpp
    sound.cpp
    spatter.cpp
    textures.cpp
    ${FPVIEW_HDRS}
    LINK_LIBRARIES lua)

if(BUILD_PLUGINS)
    target_include_directories(fpview PRIVATE ${SDL2_INCLUDE_DIRS})
    # Symbols for crash dumps: a .pdb for a debugger and a .map that lists
    # every function's address in plain text. The code itself stays the
    # optimised release build (/OPT:REF,ICF: /DEBUG alone would turn them off).
    if(MSVC)
        target_compile_options(fpview PRIVATE /Z7)
        target_link_options(fpview PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)
        # For hunting memory errors: set the environment variable FPVIEW_ASAN
        # and touch this file. The game then needs
        # clang_rt.asan_dynamic-x86_64.dll (from the compiler's bin folder)
        # next to its exe; the report goes to the game's stderr.log.
        if(DEFINED ENV{FPVIEW_ASAN})
            # (no container annotations: vectors and strings are shared with
            # the game and DFHack, which are not built this way)
            target_compile_options(fpview PRIVATE /fsanitize=address)
            target_compile_definitions(fpview PRIVATE _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
        endif()
    endif()
endif()
