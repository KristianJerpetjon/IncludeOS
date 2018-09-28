#
# CMake script for the OS library
#
add_definitions(-DARCH_${ARCH})
add_definitions(-DARCH="${ARCH}")
if (smp)
add_definitions(-DINCLUDEOS_SMP_ENABLE)
endif()

include_directories(${LIBCXX_INCLUDE_DIR})
include_directories(${MUSL_INCLUDE_DIR})
include_directories(${SOLO5_INCLUDE_DIR})
include_directories(${INCLUDEOS_ROOT}/src/include)
include_directories(${INCLUDEOS_ROOT}/api)
include_directories(${INCLUDEOS_ROOT}/mod/)
include_directories(${INCLUDEOS_ROOT}/mod/GSL/)
include_directories(${INCLUDEOS_ROOT}/mod/rapidjson/include)
include_directories(${INCLUDEOS_ROOT}/mod/uzlib/src) # tinf.h for tar
include_directories(${INCLUDEOS_ROOT}/lib/LiveUpdate)
include_directories(${BOTAN_DIR})
include_directories(${OPENSSL_DIR}/include)


set(SRCS
  version.cpp
)

set(LIBRARIES
    kernel
    util
    net
    fs
    posix
    crt
    virtio
    hw
)

SET(OBJECTS)
foreach(LIB ${LIBRARIES})
    add_subdirectory(${LIB})
    SET(OBJECTS ${OBJECTS} "$<TARGET_OBJECTS:${LIB}>" )
endforeach()

add_library(os STATIC ${SRCS} ${OBJECTS})

add_dependencies(os PrecompiledLibraries botan ${OPENSSL_LIBS})

# disable sanitizers on c_abi and cxx_abi, etc.
set_source_files_properties(crt/c_abi.c PROPERTIES COMPILE_FLAGS "-fno-sanitize=all")
set_source_files_properties(crt/cxx_abi.cpp PROPERTIES COMPILE_FLAGS "-fno-sanitize=all")
set_source_files_properties(version.cpp PROPERTIES COMPILE_DEFINITIONS "OS_VERSION=\"${OS_VERSION}\"")

add_subdirectory(arch/${ARCH})
add_subdirectory(platform/x86_pc)
add_subdirectory(platform/x86_nano)
if(WITH_SOLO5)
add_subdirectory(platform/x86_solo5)
endif(WITH_SOLO5)
add_subdirectory(drivers)
add_subdirectory(plugins)

# Add musl
add_subdirectory(musl)

#
# Installation
#
set(CMAKE_INSTALL_MESSAGE LAZY) # to avoid spam
install(TARGETS os DESTINATION includeos/${ARCH}/lib)

install(DIRECTORY ${INCLUDEOS_ROOT}/src/memdisk/ DESTINATION includeos/memdisk
        FILES_MATCHING PATTERN "*.*")

install(FILES service_name.cpp DESTINATION includeos/src)
