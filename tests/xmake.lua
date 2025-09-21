-- XMake configuration for tests

-- Include test subdirectories
includes("botcraft")
includes("protocolCraft")

-- If online tests are enabled
if has_config("build_tests_online") and has_config("compression") then
    includes("botcraft_online")
end