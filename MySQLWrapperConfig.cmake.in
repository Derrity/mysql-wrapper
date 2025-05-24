@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# 查找依赖
find_dependency(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

# 导入目标
include("${CMAKE_CURRENT_LIST_DIR}/MySQLWrapperTargets.cmake")

# 兼容性
set(MySQLWrapper_FOUND TRUE)
set(MySQLWrapper_LIBRARIES MySQLWrapper::mysqlwrapper)
set(MySQLWrapper_INCLUDE_DIRS ${MYSQL_INCLUDE_DIRS})

check_required_components(MySQLWrapper)
