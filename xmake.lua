set_xmakever("3.0.0")

-- Requires CommonLibF4: set COMMONLIBF4_PATH to a checkout (CI clones the pinned nomad fork).
if is_plat("windows") then
    local commonlib_path = os.getenv("COMMONLIBF4_PATH") or "../commonlibf4-nomad"
    if not os.isdir(path.join(commonlib_path, "include", "RE")) then
        raise("COMMONLIBF4_PATH does not point at a CommonLibF4 checkout: '" .. commonlib_path .. "'")
    end
    includes(commonlib_path)
end

set_project("VolClouds")
set_version("0.1.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

if is_plat("windows") then
    add_requires("spdlog v1.16.0", "fmt", "nlohmann_json")
end

add_rules("mode.release")

target("VolClouds")
    set_kind("shared")
    set_arch("x64")
    set_filename("VolClouds.dll")

    if is_plat("windows") then
        add_deps("commonlibf4")
        add_packages("spdlog", "fmt", "nlohmann_json")
    end

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    if is_plat("windows") then
        add_syslinks("bcrypt", "version", "User32", "Ole32", "OleAut32", "crypt32", "Shell32",
            "d3d11", "dxgi", "d3dcompiler")
    end

    set_targetdir("build/release")
