aux_source_directory(. SRC_LIST)
aux_source_directory(./network NETWORK_SRC_LIST)
aux_source_directory(./service SERVICE_SRC_LIST)
aux_source_directory(./db DB_SRC_LIST)
aux_source_directory(./model MODEL_SRC_LIST)
aux_source_directory(./redis REDIS_SRC_LIST)

add_executable(
    WeLinkServer
    ${SRC_LIST}
    ${NETWORK_SRC_LIST}
    ${SERVICE_SRC_LIST}
    ${DB_SRC_LIST} 
    ${MODEL_SRC_LIST}
    ${REDIS_SRC_LIST}
)

target_link_libraries(
    WeLinkServer
    muduo_net
    muduo_base
    pthread
    mysqlclient
    hiredis
)