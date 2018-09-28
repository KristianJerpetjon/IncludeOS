set(SRCS
    virtio.cpp
    virtio_queue.cpp
)

add_library(virtio OBJECT ${SRCS})
