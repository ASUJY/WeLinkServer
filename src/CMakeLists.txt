aux_source_directory(. SRC_LIST)
aux_source_directory(./network NETWORK_SRC_LIST)

add_executable(
    WeLinkServer ${SRC_LIST}
)

target_link_libraries(
    WeLinkServer
    muduo_net
    muduo_base
    pthread
)