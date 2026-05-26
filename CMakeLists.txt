cmake_minimum_required(VERSION 3.28)

project(MySQLWrapper VERSION 2.0.0 LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(MYSQLWRAPPER_BUILD_MODULES "Build C++23 module interface" OFF)
option(MYSQLWRAPPER_BUILD_TESTS "Build tests" ON)
option(MYSQLWRAPPER_BUILD_EXAMPLES "Build examples" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)
find_package(Threads REQUIRED)

add_library(mysqlwrapper)
add_library(MySQLWrapper::mysqlwrapper ALIAS mysqlwrapper)

target_sources(mysqlwrapper
    PRIVATE
        src/mysql_wrapper.cpp
)

if(MYSQLWRAPPER_BUILD_MODULES)
    target_sources(mysqlwrapper
        PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS modules
            FILES modules/mysql.wrapper.cppm
    )
endif()

target_sources(mysqlwrapper
    PUBLIC
        FILE_SET public_headers TYPE HEADERS
        BASE_DIRS include
        FILES include/mysqlwrapper/mysql_wrapper.hpp
)

target_compile_features(mysqlwrapper PUBLIC cxx_std_23)

target_include_directories(mysqlwrapper
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(mysqlwrapper
    PUBLIC
        Threads::Threads
    PRIVATE
        PkgConfig::MYSQLCLIENT
)

target_compile_options(mysqlwrapper PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

install(TARGETS mysqlwrapper
    EXPORT MySQLWrapperTargets
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET cxx_modules DESTINATION ${CMAKE_INSTALL_LIBDIR}/cxx/modules/mysqlwrapper
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/mysqlwrapper.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mysqlwrapper.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mysqlwrapper.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/MySQLWrapperConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/MySQLWrapperConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MySQLWrapperConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MySQLWrapper
)

install(EXPORT MySQLWrapperTargets
    FILE MySQLWrapperTargets.cmake
    NAMESPACE MySQLWrapper::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MySQLWrapper
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/MySQLWrapperConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/MySQLWrapperConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MySQLWrapper
)

if(MYSQLWRAPPER_BUILD_TESTS)
    enable_testing()
    add_executable(mysqlwrapper_tests tests/mysqlwrapper_tests.cpp)
    target_compile_features(mysqlwrapper_tests PRIVATE cxx_std_23)
    target_link_libraries(mysqlwrapper_tests PRIVATE mysqlwrapper)
    add_test(NAME mysqlwrapper_tests COMMAND mysqlwrapper_tests)

    add_executable(mysqlwrapper_integration_tests tests/mysqlwrapper_integration_tests.cpp)
    target_compile_features(mysqlwrapper_integration_tests PRIVATE cxx_std_23)
    target_link_libraries(mysqlwrapper_integration_tests PRIVATE mysqlwrapper)
    add_test(NAME mysqlwrapper_integration_tests COMMAND mysqlwrapper_integration_tests)
endif()

if(MYSQLWRAPPER_BUILD_EXAMPLES)
    add_executable(mysqlwrapper_example examples/basic.cpp)
    target_compile_features(mysqlwrapper_example PRIVATE cxx_std_23)
    target_link_libraries(mysqlwrapper_example PRIVATE mysqlwrapper)
endif()
