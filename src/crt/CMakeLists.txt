SET(SRCS
    c_abi.c
    ctype_b_loc.c
    ctype_tolower_loc.c
    string.c
    quick_exit.cpp
    cxx_abi.cpp
    )
add_library(crt OBJECT ${SRCS})
