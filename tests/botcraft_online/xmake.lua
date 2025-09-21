-- botcraft online tests
target("botcraft_online_tests")
    set_kind("binary")
    set_languages("cxx17")
    
    -- Add source files
    add_files("src/**.cpp")
    
    -- Add dependencies
    add_deps("botcraft")
    add_packages("catch2")
    
    -- Set output directory
    set_targetdir("$(buildir)/bin")
    
    -- Platform-specific configuration
    if is_plat("windows") then
        add_syslinks("ws2_32", "wsock32")
    elseif is_plat("linux", "macosx") then
        add_syslinks("pthread")
    end
target_end()