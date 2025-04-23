add_rules("mode.debug", "mode.release")

includes("../../dependencies/membox/xmake.lua")

target("sim")
    set_kind("binary")
    set_languages("c++20")
    add_deps("SV")
    set_warnings("all")

    -- 添加源文件
    add_files("sc_l1cache.cpp", "l1_tlm_adapter.cpp", "l2_tlm.cpp", "compile.cpp","utils.cpp","tag_array.cpp")

    -- 包含路径
    add_includedirs(".",  "../../dependencies/membox/src", "$(env SYSTEMC_HOME)/include")

    -- 链接 SystemC、fmt、pthread
    add_links("systemc", "fmt", "pthread")
    add_linkdirs("$(env SYSTEMC_HOME)/lib-linux64")

    -- 编译参数
    add_cxxflags("-fmax-errors=10")

    set_targetdir("output")
    -- 设置运行目录
    set_rundir(".")
