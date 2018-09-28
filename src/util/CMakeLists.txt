set(SRCS
    memstream.c
    async.cpp
    statman.cpp
    statman_liu.cpp
    logger.cpp
    sha1.cpp
    syslog_facility.cpp
    syslogd.cpp
    uri.cpp
    percent_encoding.cpp
    tar.cpp
    path_to_regex.cpp
    config.cpp
    autoconf.cpp
    crc32.cpp
)

add_library(util OBJECT ${SRCS})
