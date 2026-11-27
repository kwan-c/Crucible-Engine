# ------------------------------------------------------------ #
# Connection Server Executable
# ------------------------------------------------------------ #
# CrucibleConnectionServer: the LAN connection server (lobby registration and lists).
# A small standalone console program, deployed on its own (any PC on the network): no Engine
# library, no Vulkan, Mono or GameNetworkingSockets. It shares only the header-only protocol
# with the engine (Engine/src/Network/LobbyServerProtocol.h, ByteStream.h), so the game and
# the server always agree on the packet format.
project(ConnectionServer)

file(GLOB_RECURSE CONNECTION_SERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE CONNECTION_SERVER_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(CrucibleConnectionServer
    ${CONNECTION_SERVER_SOURCES}
    ${CONNECTION_SERVER_HEADERS}
    "${CMAKE_SOURCE_DIR}/Engine/src/Network/LobbyServerProtocol.h"
)
target_include_directories(CrucibleConnectionServer PRIVATE "${CMAKE_SOURCE_DIR}/Engine/src")
target_compile_definitions(CrucibleConnectionServer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CrucibleConnectionServer PRIVATE ws2_32 iphlpapi)
if(MSVC)
    target_compile_options(CrucibleConnectionServer PRIVATE /W4 /permissive- /MP)
    # A console program in every configuration (the root file makes Release builds WINDOWS).
    target_link_options(CrucibleConnectionServer PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
endif()
