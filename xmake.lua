if os.getenv("SYSTEMC_HOME") == nil then
    raise("Please set the SYSTEMC_HOME environment variable first!")
end
add_rules("mode.release", "mode.debug")

package("ramulator2")
    add_deps("cmake")
    set_sourcedir(path.join(os.scriptdir(), "dependencies/ramulator2"))
    on_install(function (package)
        local configs = {}
        import("package.tools.cmake").install(package, configs)
    end)
package_end()
add_requires("ramulator2")

includes("dependencies/membox/xmake.lua")

target("VentusCycleSim")
    set_kind("shared")
    set_languages("c++20")
    -- add_includedirs("dependencies/membox/src")
    add_deps("SV")
    add_packages("ramulator2")
    add_rpathdirs(path.join(os.scriptdir(), "dependencies/ramulator2")) -- TODO: why xmake doesn't add rpath build/.packages/...
    add_includedirs("dependencies/ramulator2/src")
    add_linkdirs(path.join(os.scriptdir(), "dependencies/ramulator2"))
    add_links("ramulator")
    add_files("src/sm/*.cpp", "src/utils/*.cpp", "src/utils/*.c")
    add_files("src/context_model.cpp", "src/CTA_Scheduler.cpp", "src/parameters.cpp", "src/top_gpgpu.cpp")
    add_files("src/ramulator.cpp")
    add_files("src/ventus_cyclesim.cpp", "src/ventus_cyclesim_impl.cpp")
    add_links("systemc")
    add_linkdirs("$(env SYSTEMC_HOME)/lib-linux64")
    add_includedirs("$(env SYSTEMC_HOME)/include")
    add_rpathdirs("$(env SYSTEMC_HOME)/lib-linux64")
    -- 如果是debug模式，设置spdlog库的输出级别为trace
    if is_mode("debug") then
        add_defines("SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
    end
    add_defines("VENTUS_CYCLESIM_PROJECT_DIR=\"" .. os.scriptdir() .. "\"")

target("main")
    set_kind("binary")
    set_languages("c++20")
    add_deps("VentusCycleSim")
    add_files("src/cmdarg.cpp", "src/parse_kernel.cpp", "src/main.cpp", "src/task.cpp")
    add_defines("VENTUS_CYCLESIM_PROJECT_DIR=\"" .. os.scriptdir() .. "\"")

    set_rundir(".")
