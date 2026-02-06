add_rules("mode.debug", "mode.release")

set_languages("c++20")
set_optimize("fastest")
set_defaultmode("release")

add_requires("libsdl3")
add_requires("openmp")
add_requires("cimg")
add_requires("libpng")
add_requires("libjpeg-turbo")
add_requires("nlohmann_json")

target("Process_Assets")
    set_kind("phony")
    add_deps("Asset Processor")
    on_build(function ()
        import("core.project.project")
        local asset_processor = path.join(project.directory(), "bin", "tools", "Asset Processor")
        if is_host("windows") then
            asset_processor = asset_processor .. ".exe"
        end
        os.execv(asset_processor, {"assets"})
    end)

target("Test1")
    add_includedirs("include")
    add_files("src/**.cpp")
    add_packages("libsdl3" , "openmp")
    -- Force OpenMP flags for GCC/Clang/MinGW; MSVC picks it via /openmp automatically from package
    --add_cxxflags("-fopenmp", {tools = {"gcc", "clang", "gxx", "clangxx"}})
    --add_ldflags("-fopenmp", {tools = {"gcc", "clang", "gxx", "clangxx"}})
    --add_defines("USE_OPENMP")

    -- process assets before building
    add_deps("Process_Assets")

    set_targetdir("bin")

target("Asset Processor")
    add_includedirs("include")
    add_files("tools/asset_processor/**.cpp")
    set_rundir("$(projectdir)")
    set_runargs("assets")
    add_packages("cimg", "libpng", "libjpeg-turbo", "nlohmann_json")
    add_defines("cimg_display=0", "cimg_use_png", "cimg_use_jpeg")
    if is_plat("windows") then
        add_syslinks("gdi32")
    end

    set_targetdir("bin/tools")

target("JSON to Header")
    add_includedirs("include")
    add_files("tools/json_to_header/**.cpp")
    set_rundir("$(projectdir)/assets")
    set_runargs("assets.json", "assets.h")
    add_packages("nlohmann_json")
    set_targetdir("bin/tools")
