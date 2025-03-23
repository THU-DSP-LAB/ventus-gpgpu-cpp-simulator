if os.getenv("SYSTEMC_HOME") == nil then
    raise("Please set the SYSTEMC_HOME environment variable first!")
end

add_rules("mode.release", "mode.debug")

target("VentusCycleSim")
    set_kind("shared")
    set_languages("c++20")
    add_files("src/sm/*.cpp", "src/utils/*.cpp", "src/utils/*.c")
    add_files("src/context_model.cpp", "src/CTA_Scheduler.cpp", "src/parameters.cpp", "src/top_gpgpu.cpp")
    add_files("src/ventus_cyclesim.cpp", "src/ventus_cyclesim_impl.cpp")
    add_links("systemc")
    add_linkdirs("$(env SYSTEMC_HOME)/lib-linux64")
    add_includedirs("$(env SYSTEMC_HOME)/include")
    add_rpathdirs("$(env SYSTEMC_HOME)/lib-linux64")

target("main")
    set_kind("binary")
    set_languages("c++20")
    add_deps("VentusCycleSim")
    add_files("src/cmdarg.cpp", "src/context_model.cpp", "src/main.cpp", "src/task.cpp")
    add_links("systemc")
    add_linkdirs("$(env SYSTEMC_HOME)/lib-linux64")
    add_includedirs("$(env SYSTEMC_HOME)/include")
    add_rpathdirs("$(env SYSTEMC_HOME)/lib-linux64")

    set_rundir(".")
