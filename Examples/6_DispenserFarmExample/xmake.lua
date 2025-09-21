-- DispenserFarmExample example
target("DispenserFarmExample")
    set_kind("binary")
    set_languages("cxx17")
    
    add_files("src/*.cpp")
    add_includedirs("include")
    add_deps("botcraft")
    
    -- Add necessary package dependencies
    add_packages("openssl", "zlib")
    
    set_targetdir("$(buildir)/bin")
    
    if is_plat("windows") then
        add_syslinks("ws2_32", "wsock32", "winmm")
    elseif is_plat("linux", "macosx") then
        add_syslinks("pthread")
    end
target_end()