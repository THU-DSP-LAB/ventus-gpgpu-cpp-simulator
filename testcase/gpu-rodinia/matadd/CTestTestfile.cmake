# CMake generated Testfile for 
# Source directory: /eda/work/yangzx/ventus/pocl/examples/matadd
# Build directory: /eda/work/yangzx/ventus/pocl/build/examples/matadd
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(examples/matadd "/eda/work/yangzx/ventus/pocl/build/examples/matadd/matadd")
set_tests_properties(examples/matadd PROPERTIES  COST "3.0" DEPENDS "pocl_version_check" LABELS "internal;vulkan" PASS_REGULAR_EXPRESSION "OK" PROCESSORS "1" _BACKTRACE_TRIPLES "/eda/work/yangzx/ventus/pocl/examples/matadd/CMakeLists.txt;35;add_test;/eda/work/yangzx/ventus/pocl/examples/matadd/CMakeLists.txt;0;")
