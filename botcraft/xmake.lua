-- XMake configuration for botcraft library

-- Define protocol version mapping function
local function get_protocol_version(game_version)
    local version_map = {
        ["1.12.2"] = "340",
        ["1.13"] = "393",
        ["1.13.1"] = "401",
        ["1.13.2"] = "404",
        ["1.14"] = "477",
        ["1.14.1"] = "480",
        ["1.14.2"] = "485",
        ["1.14.3"] = "490",
        ["1.14.4"] = "498",
        ["1.15"] = "573",
        ["1.15.1"] = "575", 
        ["1.15.2"] = "578",
        ["1.16"] = "735",
        ["1.16.1"] = "736",
        ["1.16.2"] = "751",
        ["1.16.3"] = "753",
        ["1.16.4"] = "754",
        ["1.16.5"] = "754",
        ["1.17"] = "755",
        ["1.17.1"] = "756",
        ["1.18"] = "757",
        ["1.18.1"] = "757",
        ["1.18.2"] = "758",
        ["1.19"] = "759",
        ["1.19.1"] = "760",
        ["1.19.2"] = "760",
        ["1.19.3"] = "761",
        ["1.19.4"] = "762",
        ["1.20"] = "763",
        ["1.20.1"] = "763",
        ["1.20.2"] = "764",
        ["1.20.3"] = "765",
        ["1.20.4"] = "765",
        ["1.20.5"] = "766",
        ["1.20.6"] = "766",
        ["1.21"] = "767",
        ["1.21.1"] = "767",
        ["1.21.2"] = "768",
        ["1.21.3"] = "768",
        ["1.21.4"] = "769",
        ["1.21.5"] = "770",
        ["1.21.6"] = "771",
        ["1.21.7"] = "772",
        ["1.21.8"] = "772"
    }
    return version_map[game_version] or "772"
end

target("botcraft")
    set_kind(has_config("static_libs") and "static" or "shared")
    set_languages("cxx17")

    -- Add header directories
    add_includedirs("include", {public = true})
    add_includedirs("private_include")
    
    -- Generate Version.hpp file
    local game_version = get_config("game_version") or "1.21.8"
    local protocol_version = get_protocol_version(game_version)
    
    add_configfiles("cmake/Version.hpp.in", {
        filename = "include/botcraft/Version.hpp",
        variables = {
            PROTOCOL_VERSION = protocol_version,
            BOTCRAFT_GAME_VERSION = game_version
        }
    })

    -- Add generated header directory
    add_includedirs("$(buildir)/include", {public = true})

    -- Add header files
    add_headerfiles("include/**.hpp")

    -- Add source files
    add_files("src/**.cpp")
    
    -- Exclude renderer files if OpenGL GUI is not enabled
    if not has_config("opengl_gui") then
        remove_files("src/Renderer/**.cpp")
    end

    -- Add package dependencies
    add_packages("asio")
    add_packages("zlib")
    add_packages("openssl")
    
    -- Depend on protocolCraft
    add_deps("protocolCraft")

    -- Define ASSETS_PATH
    local game_version = get_config("game_version") or "1.21.8"
    add_defines("ASSETS_PATH=\"./Assets/" .. game_version .. "\"")

    -- Platform-specific configuration
    if is_plat("windows") then
        add_defines("BOTCRAFT_EXPORTS")
        if has_config("static_libs") then
            add_defines("BOTCRAFT_STATIC")
        end
        
        if has_config("windows_better_sleep") then
            add_defines("BETTER_SLEEP")
        end
    end

    -- If compression is enabled
    if has_config("compression") then
        add_packages("zlib")
        add_defines("USE_COMPRESSION")
    end

    -- If encryption is enabled
    if has_config("encryption") then
        add_packages("openssl")
        add_defines("USE_ENCRYPTION")
    end

    -- If OpenGL GUI is enabled
    if has_config("opengl_gui") then
        add_packages("opengl", "glfw", "glm", "glad", "stb")
        add_defines("USE_GUI")
        add_defines("USE_RENDERER")
        
        if has_config("imgui") then
            add_packages("imgui")
            add_defines("USE_IMGUI")
        end
    end

    -- Set output directory
    set_targetdir("$(builddir)/lib")

    -- Platform-specific symbol export
    if is_plat("windows") and not has_config("static_libs") then
        add_defines("BOTCRAFT_EXPORTS")
        set_symbols("debug")
    end

    -- Add thread support
    if is_plat("linux", "macosx") then
        add_syslinks("pthread")
    elseif is_plat("windows") then
        add_syslinks("ws2_32", "wsock32", "winmm")
    end

    -- Precompiled header support (similar to CMake's BOTCRAFT_USE_PRECOMPILED_HEADERS)
    if is_mode("release") then
        set_pcxxheader("include/botcraft/Game/ManagersClient.hpp")
    end
target_end()