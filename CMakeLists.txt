cmake_minimum_required(VERSION 3.10)
project(MySQLWrapper VERSION 1.0.0 LANGUAGES CXX)

# C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# 查找MySQL
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

# 查找线程库
find_package(Threads REQUIRED)

# 创建库
add_library(mysqlwrapper SHARED
        MySQLWrapper.cpp
        MySQLWrapper.h
)

# 包含目录
target_include_directories(mysqlwrapper PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MYSQL_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(mysqlwrapper PUBLIC
        ${MYSQL_LIBRARIES}
        Threads::Threads
)

# 编译定义
target_compile_definitions(mysqlwrapper PRIVATE
        MYSQLWRAPPER_EXPORTS
)

# 安装规则
install(TARGETS mysqlwrapper
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
)

install(FILES MySQLWrapper.h
        DESTINATION include
)

# 创建静态库版本
add_library(mysqlwrapper_static STATIC
        MySQLWrapper.cpp
        MySQLWrapper.h
)

target_include_directories(mysqlwrapper_static PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MYSQL_INCLUDE_DIRS}
)

target_link_libraries(mysqlwrapper_static PUBLIC
        ${MYSQL_LIBRARIES}
        Threads::Threads
)

install(TARGETS mysqlwrapper_static
        ARCHIVE DESTINATION lib
)

# 创建pkg-config文件
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/mysqlwrapper.pc"
        "${CMAKE_CURRENT_BINARY_DIR}/mysqlwrapper.pc"
        @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mysqlwrapper.pc"
        DESTINATION lib/pkgconfig
)

# 示例程序（可选）
option(BUILD_EXAMPLES "Build example programs" OFF)

if(BUILD_EXAMPLES)
    add_executable(example example.cpp)
    target_link_libraries(example mysqlwrapper)
endif()

# 测试（可选）
option(BUILD_TESTS "Build test programs" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_executable(test_mysqlwrapper test_mysqlwrapper.cpp)
    target_link_libraries(test_mysqlwrapper mysqlwrapper)
    add_test(NAME MySQLWrapperTest COMMAND test_mysqlwrapper)
endif()

# 打印配置信息
message(STATUS "MySQL Include Dirs: ${MYSQL_INCLUDE_DIRS}")
message(STATUS "MySQL Libraries: ${MYSQL_LIBRARIES}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")