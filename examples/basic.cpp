#include "mysqlwrapper/mysql_wrapper.hpp"

#include <iostream>

int main() {
    mysqlw::ConnectionConfig config;
    config.host = "127.0.0.1";
    config.user = "root";
    config.password = "password";
    config.database = "test";

    mysqlw::Database database(config);
    auto result = database.query("SELECT id, name FROM users WHERE id = ?", 1);
    if (!result) {
        std::cerr << mysqlw::to_string(result.error().code) << ": " << result.error().message << '\n';
        return 1;
    }

    for (std::size_t row_index = 0; row_index < result->row_count(); ++row_index) {
        const auto row = (*result)[row_index];
        std::cout << mysqlw::get_or_throw<std::int64_t>(row["id"]) << ' '
                  << mysqlw::get_or_throw<std::string>(row["name"]) << '\n';
    }
}
