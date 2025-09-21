-- XMake configuration file to replace CMake build system
set_version("3.0.0")
set_project("Botcraft")

-- Set C++ standard
set_languages("cxx17")

-- Add platform-specific configurations
if is_plat("windows") then
    add_defines("WIN32_LEAN_AND_MEAN")
    add_cxflags("/bigobj", "/W3")
    -- Export all DLL symbols on Windows
    add_defines("CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS")
end

-- Configuration options (corresponding to CMake options)
option("opengl_gui")
    set_default(false)
    set_showmenu(true)
    set_description("Activate if you want to use OpenGL renderer")
option_end()

option("imgui")
    set_default(false)
    set_showmenu(true)
    set_description("Activate if you want to use display information on screen with ImGui")
option_end()

option("compression")
    set_default(true)
    set_showmenu(true)
    set_description("Activate if compression is enabled on the server")
option_end()

option("encryption")
    set_default(true)
    set_showmenu(true)
    set_description("Activate if you want to connect to a server in online mode")
option_end()

option("build_examples")
    set_default(true)
    set_showmenu(true)
    set_description("Set to compile examples with the library")
option_end()

option("build_tests")
    set_default(false)
    set_showmenu(true)
    set_description("Activate if you want to build tests")
option_end()

option("build_tests_online")
    set_default(false)
    set_showmenu(true)
    set_description("Activate if you want to enable additional on server tests (requires Java)")
option_end()

option("windows_better_sleep")
    set_default(true)
    set_showmenu(true)
    set_description("Set to true to use better thread sleep on Windows")
option_end()

option("static_libs")
    set_default(false)
    set_showmenu(true)
    set_description("Build static libraries instead of shared ones")
option_end()

-- Game version configuration
option("game_version")
    set_default("1.21.8")
    set_showmenu(true)
    set_description("Minecraft game version")
    set_values("1.12.2", "1.13", "1.13.1", "1.13.2", "1.14", "1.14.1", "1.14.2", "1.14.3", "1.14.4", 
               "1.15", "1.15.1", "1.15.2", "1.16", "1.16.1", "1.16.2", "1.16.3", "1.16.4", "1.16.5",
               "1.17", "1.17.1", "1.18", "1.18.1", "1.18.2", "1.19", "1.19.1", "1.19.2", "1.19.3", "1.19.4",
               "1.20", "1.20.1", "1.20.2", "1.20.3", "1.20.4", "1.20.5", "1.20.6", 
               "1.21", "1.21.1", "1.21.2", "1.21.3", "1.21.4", "1.21.5", "1.21.6", "1.21.7", "1.21.8")
option_end()

-- Add package dependencies
add_requires("asio", {configs = {header_only = true}})
add_requires("zlib")
add_requires("openssl")

if has_config("opengl_gui") then
    add_requires("opengl")
    add_requires("glfw")
    add_requires("glm")
    add_requires("glad")
    add_requires("stb")
    
    if has_config("imgui") then
        add_requires("imgui", {configs = {glfw_opengl3 = true}})
    end
end

if has_config("build_tests") then
    add_requires("catch2")
end

-- Include sub-projects
includes("protocolCraft")
includes("botcraft")

-- Set global macro definitions
local game_version = get_config("game_version") or "1.21.8"
local protocol_version = get_protocol_version(game_version)

add_defines("PROTOCOL_VERSION=" .. protocol_version)
add_defines("BOTCRAFT_GAME_VERSION=\"" .. game_version .. "\"")

if has_config("build_examples") then
    includes("Examples")
end

if has_config("build_tests") then
    includes("tests")
end