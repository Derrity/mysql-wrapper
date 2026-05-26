#include "mysqlwrapper/mysql_wrapper.hpp"

#include <cassert>
#include <chrono>
#include <iostream>

using namespace mysqlw;

namespace {

void test_result_layout() {
    Result result(
        {
            Column{.name = "id", .type = ColumnType::signed_integer, .nullable = false},
            Column{.name = "name", .type = ColumnType::text, .nullable = false},
            Column{.name = "active", .type = ColumnType::boolean, .nullable = false}
        },
        {
            Result::RowStorage{std::int64_t{42}, std::string("Ada"), true}
        });

    assert(!result.empty());
    assert(result.row_count() == 1);
    assert(result.column_count() == 3);
    assert(result.column_index("name") == 1);

    const auto row = result[0];
    assert(get_or_throw<std::int64_t>(row["id"]) == 42);
    assert(get_or_throw<std::string>(row["name"]) == "Ada");
    assert(get_or_throw<bool>(row["active"]));
}

void test_type_mismatch() {
    const Value value = std::string("not an integer");
    const auto converted = get_as<std::int64_t>(value);
    assert(!converted);
    assert(converted.error().code == ErrorCode::type_mismatch);
}

void test_failed_connection_returns_expected() {
    ConnectionConfig config;
    config.host = "127.0.0.1";
    config.port = 1;
    config.user = "invalid";
    config.password = "invalid";
    config.database = "invalid";
    config.initial_pool_size = 1;
    config.max_pool_size = 1;
    config.worker_count = 1;
    config.connect_timeout = std::chrono::seconds{1};
    config.read_timeout = std::chrono::seconds{1};
    config.write_timeout = std::chrono::seconds{1};
    config.acquire_timeout = std::chrono::milliseconds{100};

    Database database(config);
    const auto result = database.query("SELECT 1");
    assert(!result);
    assert(result.error().code == ErrorCode::connection_failed);

    auto future = database.query_async("SELECT 1");
    const auto status = future.wait_for(std::chrono::seconds{3});
    assert(status == std::future_status::ready);
    const auto async_result = future.get();
    assert(!async_result);
}

} // namespace

int main() {
    test_result_layout();
    test_type_mismatch();
    test_failed_connection_returns_expected();
    std::cout << "mysqlwrapper tests passed\n";
}
