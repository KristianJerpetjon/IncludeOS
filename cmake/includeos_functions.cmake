#sets the proper architecture
function(includeos_set_arch ARCH)

  message(STATUS "Target CPU ${ARCH}")

  set(TRIPLE "${ARCH}-pc-linux-elf" PARENT_SCOPE)
  set(CMAKE_CXX_COMPILER_TARGET ${TRIPLE}  PARENT_SCOPE)
  set(CMAKE_C_COMPILER_TARGET ${TRIPLE}  PARENT_SCOPE)

  message(STATUS "Target triple ${ARCH}-pc-linux-elf")

endfunction(includeos_set_arch)

function(includeos_test_compiler)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-std=c++17 HAVE_FLAG_STD_CXX17)
  if(NOT HAVE_FLAG_STD_CXX17)
    message(FATAL_ERROR "The provided compiler: ${CMAKE_CXX_COMPILER} \n \
    does not support c++17 standard please make sure you are using one of the following:\n \
    CLANG >= 5 (remeber export CC=`wich clang` && export CXX=`which clang++`)\n \
    GCC >= 7")
  endif()
endfunction(includeos_test_compiler)

find_package(Git QUIET)

function(includeos_init_submodule MOD)
  message(STATUS "Init git submodule: " ${MOD})
  execute_process(COMMAND git submodule update --init ${MOD} WORKING_DIRECTORY ${INCLUDEOS_ROOT})
endfunction()
