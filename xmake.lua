set_project("MySQLWrapper")
set_version("2.0.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.asan", "mode.tsan", "mode.ubsan")
set_policy("build.c++.modules", true)
set_policy("build.warning", true)

option("modules")
    set_default(false)
    set_showmenu(true)
    set_description("Build the C++23 module interface")
option_end()

option("tests")
    set_default(true)
    set_showmenu(true)
    set_description("Build unit tests")
option_end()

option("examples")
    set_default(false)
    set_showmenu(true)
    set_description("Build examples")
option_end()

package("mysqlclient-pkgconfig")
    set_kind("library")
    set_homepage("https://www.mysql.com")
    set_description("MySQL client library discovered through pkg-config")

    on_fetch(function (package, opt)
        import("lib.detect.pkgconfig")
        local result = pkgconfig.libinfo("mysqlclient")
        if result then
            return result
        end
    end)
package_end()

add_requires("mysqlclient-pkgconfig", {system = true})

target("mysqlwrapper")
    set_kind("$(kind)")
    add_files("src/mysql_wrapper.cpp")
    add_headerfiles("include/(mysqlwrapper/*.hpp)")
    add_includedirs("include", {public = true})
    add_packages("mysqlclient-pkgconfig")
    add_syslinks("pthread")

    if has_config("modules") then
        add_rules("c++.build.modules")
        add_files("modules/mysql.wrapper.cppm", {public = true})
    end

target("mysqlwrapper_tests")
    set_kind("binary")
    set_default(has_config("tests"))
    add_files("tests/mysqlwrapper_tests.cpp")
    add_deps("mysqlwrapper")
    add_tests("default")

target("mysqlwrapper_integration_tests")
    set_kind("binary")
    set_default(has_config("tests"))
    add_files("tests/mysqlwrapper_integration_tests.cpp")
    add_deps("mysqlwrapper")
    add_tests("default")

target("mysqlwrapper_example")
    set_kind("binary")
    set_default(has_config("examples"))
    add_files("examples/basic.cpp")
    add_deps("mysqlwrapper")
