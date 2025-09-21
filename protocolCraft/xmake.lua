-- XMake configuration for protocolCraft library
target("protocolCraft")
    set_kind(has_config("static_libs") and "static" or "shared")
    set_languages("cxx17")

    -- Add header directories
    add_includedirs("include", {public = true})

    -- Add header files
    add_headerfiles("include/**.hpp")

    -- Add source files
    add_files("src/**.cpp")

    -- Add package dependencies
    add_packages("asio")

    -- Platform-specific configuration
    if is_plat("windows") then
        add_defines("PROTOCOLCRAFT_EXPORTS")
        if has_config("static_libs") then
            add_defines("PROTOCOLCRAFT_STATIC")
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

    -- Set output directory
    set_targetdir("$(builddir)/lib")

    -- Platform-specific symbol export
    if is_plat("windows") and not has_config("static_libs") then
        add_defines("PROTOCOLCRAFT_EXPORTS")
        set_symbols("debug")
    end

    -- Add thread support
    if is_plat("linux", "macosx") then
        add_syslinks("pthread")
    end
target_end()