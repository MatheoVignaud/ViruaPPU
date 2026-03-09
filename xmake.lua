add_rules("mode.debug", "mode.release")

set_languages("c17")
set_defaultmode("release")

add_requires("openmp")


target("VirtuaPPU")
    set_kind("static")
    add_deps("openmp")
    add_includedirs("include", {public = true})
    add_headerfiles("include/**.h")
    add_files("src/*.c")
    add_defines("USE_OPENMP")
    if is_plat("windows") then
        add_cflags("/openmp", {tools = {"cl", "clang_cl"}})
    else
        add_cflags("-fopenmp", {tools = {"gcc", "clang"}})
        add_ldflags("-fopenmp", {tools = {"gcc", "clang"}})
    end
