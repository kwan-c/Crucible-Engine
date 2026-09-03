# ------------------------------------------------------------ #
# Third-Party Dependencies
# ------------------------------------------------------------ #

# GLFW
add_subdirectory(glfw EXCLUDE_FROM_ALL)
set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")

# Glad
add_library(glad glad/src/glad.c)
target_include_directories(glad PUBLIC glad/include)
set_target_properties(glad PROPERTIES FOLDER "ThirdParty")

# Add more here...


# Create an interface library that bundles all common dependencies
add_library(EngineThirdParty INTERFACE)
target_link_libraries(EngineThirdParty INTERFACE
    glfw
    glad
    
    # Add more here...
)