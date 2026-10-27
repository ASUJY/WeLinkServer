cmake_minimum_required(VERSION 3.16)
project(chatServer)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 配置编译选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)


# 配置最终的可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/scripts/
    DESTINATION ${PROJECT_SOURCE_DIR}/bin
)

# 可选：开关单元测试
option(BUILD_TESTS "Build unit tests" OFF)
# 可选：开关压力测试
option(BUILD_LOADGEN "Build the TCP register/login load generator" OFF)
if(BUILD_TESTS)
    # 静态代码检测，clang-tidy:代码规范、空指针、内存隐患；cppcheck:未初始化变量、资源泄漏；
    # set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=*)
    # 内存 / 越界检测(运行时测试)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
    # 加载 FetchContent 模块，CMake 内置的远程依赖下载工具
    include(FetchContent)
    FetchContent_Declare(
        googletest  # 声明一个名为 googletest 的外部依赖
        GIT_REPOSITORY https://github.com/google/googletest.git # 指定 git 仓库地址
        GIT_TAG v1.14.0 # 指定拉取的标签版本 v1.14.0（固定版本，防止自动升级）
    )
    FetchContent_MakeAvailable(googletest)  # 自动下载、配置、编译该依赖，让项目可以直接链接 gtest/gtest_main
    enable_testing() # 开启测试
endif()

# 配置头文件的搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/network)
include_directories(${PROJECT_SOURCE_DIR}/include/pool)
include_directories(${PROJECT_SOURCE_DIR}/include/service)
include_directories(${PROJECT_SOURCE_DIR}/include/model)
include_directories(${PROJECT_SOURCE_DIR}/include/db)
include_directories(${PROJECT_SOURCE_DIR}/include/redis)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty)


# 加载子目录
add_subdirectory(src)

if(BUILD_LOADGEN)
    add_executable(loadTest loadtest/loadtest.cpp)
    target_link_libraries(loadTest pthread)
endif()

if(BUILD_TESTS)
    # 收集全部业务源文件
    file(GLOB SRC_LIST
        ${PROJECT_SOURCE_DIR}/src/config.cpp
        ${PROJECT_SOURCE_DIR}/src/db/*.cpp
        ${PROJECT_SOURCE_DIR}/src/redis/*.cpp
        ${PROJECT_SOURCE_DIR}/src/model/*.cpp
        ${PROJECT_SOURCE_DIR}/src/service/*.cpp
        ${PROJECT_SOURCE_DIR}/src/network/*.cpp
        ${PROJECT_SOURCE_DIR}/src/pool/*.cpp
    )
    # 编译静态库 chatserver_lib
    add_library(chatserver_lib STATIC ${SRC_LIST})
    target_link_libraries(chatserver_lib
        muduo_net muduo_base
        mysqlclient
        hiredis
        pthread
    )
    add_subdirectory(test)
endif()