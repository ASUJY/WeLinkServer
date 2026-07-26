cmake_minimum_required(VERSION 3.16)
project(char)

# 配置编译选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

# 配置最终的可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 加载 FetchContent 模块，CMake 内置的远程依赖下载工具
include(FetchContent)
FetchContent_Declare(
    googletest  # 声明一个名为 googletest 的外部依赖
    # 指定 git 仓库地址
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0 # 指定拉取的标签版本 v1.14.0（固定版本，防止自动升级）
)
FetchContent_MakeAvailable(googletest)  # 自动下载、配置、编译该依赖，让项目可以直接链接 gtest/gtest_main

# 配置头文件的搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/network)
include_directories(${PROJECT_SOURCE_DIR}/include/service)
include_directories(${PROJECT_SOURCE_DIR}/include/model)
include_directories(${PROJECT_SOURCE_DIR}/include/db)
include_directories(${PROJECT_SOURCE_DIR}/include/redis)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty)


# 加载子目录
add_subdirectory(src)