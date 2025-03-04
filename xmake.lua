if os.getenv("SYSTEMC_HOME") == nil then
    raise("Please set the SYSTEMC_HOME environment variable first!")
end

add_rules("mode.release", "mode.debug")

target("main")
    set_kind("binary")
    set_languages("c++20")
    add_files("src/**.cpp", "src/**.c")

    add_links("systemc")
    add_linkdirs("$(env SYSTEMC_HOME)/lib-linux64")
    add_includedirs("$(env SYSTEMC_HOME)/include")
    add_rpathdirs("$(env SYSTEMC_HOME)/lib-linux64")

    set_rundir(".")
