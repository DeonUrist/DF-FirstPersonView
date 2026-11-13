project(fpview)

set(FPVIEW_HDRS
    common.h
    media.h
    menu.inc
    render.h
    scene.h
    spatter.h
    textures.h
)
set_source_files_properties(${FPVIEW_HDRS} PROPERTIES HEADER_FILE_ONLY TRUE)

dfhack_plugin(fpview
    fpview.cpp
    media.cpp
    render.cpp
    scene.cpp
    spatter.cpp
    textures.cpp
    ${FPVIEW_HDRS}
    LINK_LIBRARIES lua)

if(BUILD_PLUGINS)
    target_include_directories(fpview PRIVATE ${SDL2_INCLUDE_DIRS})
endif()
