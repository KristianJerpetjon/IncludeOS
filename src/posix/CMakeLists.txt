SET(SRCS
      fd.cpp
      file_fd.cpp
      tcp_fd.cpp
      udp_fd.cpp
      unix_fd.cpp
    )


add_library(posix OBJECT ${SRCS})
