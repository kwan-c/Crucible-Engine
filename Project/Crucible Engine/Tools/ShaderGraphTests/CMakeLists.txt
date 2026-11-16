# ShaderGraphTests (see main.cpp): tests for the shader graph's data, file and code generator
# (Editor/src/ShaderGraph). A program of its own, like CrucibleCook: it builds those few sources
# itself, because they need neither the engine nor a window - only rapidjson.
#
# Run it after changing anything in Editor/src/ShaderGraph (the node table included):
#     Build/Tools/ShaderGraphTests/Debug/ShaderGraphTests.exe
# It prints what failed and returns 1 if anything did.
add_executable(ShaderGraphTests
    main.cpp
    ../../Editor/src/ShaderGraph/ShaderGraph.cpp
    ../../Editor/src/ShaderGraph/ShaderGraph.h
    ../../Editor/src/ShaderGraph/ShaderGraphNodes.cpp
    ../../Editor/src/ShaderGraph/ShaderGraphNodes.h
    ../../Editor/src/ShaderGraph/ShaderGraphGenerator.cpp
    ../../Editor/src/ShaderGraph/ShaderGraphGenerator.h
)
target_include_directories(ShaderGraphTests PRIVATE ../../Editor/src)
target_link_libraries(ShaderGraphTests PRIVATE rapidjson)
target_compile_definitions(ShaderGraphTests PRIVATE NOMINMAX)
if(MSVC)
    target_compile_options(ShaderGraphTests PRIVATE /W4 /permissive-)
    target_link_options(ShaderGraphTests PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
endif()
set_target_properties(ShaderGraphTests PROPERTIES FOLDER "Tools")
