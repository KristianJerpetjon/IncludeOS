if(${ARCH} STREQUAL "x86_64")
  set(OPENSSL_MODULES
    openssl/init.cpp
    openssl/client.cpp
    openssl/server.cpp
    https/openssl_server.cpp
    http/client.cpp
  )
  set(OPENSSL_LIBS openssl_ssl openssl_crypto)
endif()
set(BOTAN_MODULES https/botan_server.cpp)

SET(ETH_SRCS
     ethernet/ethernet.cpp
     ethernet/ethernet_8021q.cpp
     )


SET(IP4_SRCS
    ip4/arp.cpp
    ip4/ip4.cpp
    ip4/reassembly.cpp
    ip4/icmp4.cpp
    ip4/udp.cpp
    ip4/udp_socket.cpp
    )



SET(IP6_SRCS
    ip6/ip6.cpp
    ip6/icmp6.cpp
    ip6/ndp.cpp
    )


SET(TCP_SRCS
    tcp/tcp.cpp
    tcp/connection.cpp
    tcp/connection_states.cpp
    tcp/write_queue.cpp
    tcp/rttm.cpp
    tcp/listener.cpp
    tcp/read_buffer.cpp
    tcp/read_request.cpp
    tcp/stream.cpp
    tcp/tcp_conntrack.cpp
    )


set(HTTP_SRCS
    http/header.cpp
    http/header_fields.cpp
    http/message.cpp
    http/request.cpp
    http/response.cpp
    http/status_codes.cpp
    http/time.cpp
    http/version.cpp
    http/mime_types.cpp
    http/cookie.cpp
    http/client_connection.cpp
    http/basic_client.cpp
    http/server_connection.cpp
    http/server.cpp
    http/response_writer.cpp
    )


SET(DHCP_SRCS
    dhcp/dh4client.cpp
    dhcp/dhcpd.cpp
    )


set(DNS_SRCS
    dns/dns.cpp
    dns/client.cpp
    )


set(NAT_SRCS
    nat/nat.cpp
    nat/napt.cpp
    )

set(SRCS

    checksum.cpp

    buffer_store.cpp
    inet.cpp
    super_stack.cpp
    configure.cpp
    conntrack.cpp
    vlan_manager.cpp
    ws/websocket.cpp


    )

SET(OBJLIST
        ${NET_SRCS}
        ${HTTP_SRCS}
        ${ETH_SRCS}
        ${IP4_SRCS}
        ${IP6_SRCS}
        ${TCP_SRCS}
        ${NAT_SRCS}
        ${DNS_SRCS}
        ${DHCP_SRCS}
    )


add_library(net OBJECT ${SRCS} ${OPENSSL_MODULES} ${BOTAN_MODULES} ${OBJLIST})
