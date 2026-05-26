#include "mysqlwrapper/mysql_wrapper.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace mysqlw;

namespace {

const char* env_or_null(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? nullptr : value;
}

std::string env_or(const char* name, std::string fallback) {
    const char* value = env_or_null(name);
    return value == nullptr ? std::move(fallback) : std::string(value);
}

std::uint16_t env_port_or(const char* name, std::uint16_t fallback) {
    const char* value = env_or_null(name);
    if (value == nullptr) {
        return fallback;
    }
    return static_cast<std::uint16_t>(std::stoul(value));
}

ConnectionConfig integration_config() {
    ConnectionConfig config;
    config.host = env_or("MYSQLWRAPPER_TEST_HOST", "127.0.0.1");
    config.port = env_port_or("MYSQLWRAPPER_TEST_PORT", 3306);
    config.user = env_or("MYSQLWRAPPER_TEST_USER", "root");
    config.password = env_or("MYSQLWRAPPER_TEST_PASSWORD", "mysqlwrapper");
    config.database = env_or("MYSQLWRAPPER_TEST_DATABASE", "mysqlwrapper_test");
    config.initial_pool_size = 2;
    config.max_pool_size = 8;
    config.worker_count = 2;
    config.connect_timeout = std::chrono::seconds{5};
    config.read_timeout = std::chrono::seconds{5};
    config.write_timeout = std::chrono::seconds{5};
    config.acquire_timeout = std::chrono::seconds{5};
    return config;
}

void require_ok(const Expected<ExecuteResult>& result, const char* context) {
    if (!result) {
        std::cerr << context << " failed: " << to_string(result.error().code) << ": "
                  << result.error().message << '\n';
        std::abort();
    }
}

Result require_result(Expected<Result>&& result, const char* context) {
    if (!result) {
        std::cerr << context << " failed: " << to_string(result.error().code) << ": "
                  << result.error().message << '\n';
        std::abort();
    }
    return std::move(*result);
}

void reset_schema(Database& db) {
    require_ok(db.execute("DROP TABLE IF EXISTS mysqlwrapper_items"), "drop table");
    require_ok(db.execute(
                   "CREATE TABLE mysqlwrapper_items ("
                   "id BIGINT PRIMARY KEY AUTO_INCREMENT,"
                   "name VARCHAR(64) NOT NULL,"
                   "quantity BIGINT NOT NULL,"
                   "price DOUBLE NOT NULL,"
                   "enabled TINYINT NOT NULL,"
                   "payload VARBINARY(64) NULL"
                   ")"),
               "create table");
}

void test_prepared_insert_query_and_blob(Database& db) {
    std::vector<std::byte> payload{std::byte{0x01}, std::byte{0x02}, std::byte{0x7f}};
    auto insert = db.execute(
        "INSERT INTO mysqlwrapper_items (name, quantity, price, enabled, payload) VALUES (?, ?, ?, ?, ?)",
        "widget",
        7,
        12.5,
        true,
        std::span<const std::byte>(payload));
    require_ok(insert, "prepared insert");
    assert(insert->affected_rows == 1);
    assert(insert->last_insert_id > 0);

    auto result = require_result(
        db.query("SELECT name, quantity, price, enabled, payload FROM mysqlwrapper_items WHERE id = ?",
                 insert->last_insert_id),
        "prepared select");
    assert(result.row_count() == 1);

    const auto row = result[0];
    assert(get_or_throw<std::string>(row["name"]) == "widget");
    assert(get_or_throw<std::int64_t>(row["quantity"]) == 7);
    assert(get_or_throw<double>(row["price"]) == 12.5);
    assert(get_or_throw<std::int64_t>(row["enabled"]) == 1);

    const auto stored_payload = get_or_throw<Blob>(row["payload"]);
    assert(stored_payload == payload);
}

void test_transaction_commit_and_rollback(Database& db) {
    auto committed = db.transaction([](Transaction& tx) -> Expected<void> {
        auto result = tx.execute(
            "INSERT INTO mysqlwrapper_items (name, quantity, price, enabled, payload) VALUES (?, ?, ?, ?, ?)",
            "committed",
            1,
            1.0,
            true,
            nullptr);
        if (!result) {
            return std::unexpected(result.error());
        }
        return {};
    });
    assert(committed);

    auto rolled_back = db.transaction([](Transaction& tx) -> Expected<void> {
        auto result = tx.execute(
            "INSERT INTO mysqlwrapper_items (name, quantity, price, enabled, payload) VALUES (?, ?, ?, ?, ?)",
            "rolled-back",
            1,
            1.0,
            true,
            nullptr);
        if (!result) {
            return std::unexpected(result.error());
        }
        return std::unexpected(DbError{
            .code = ErrorCode::transaction_failed,
            .operation = Operation::rollback,
            .message = "intentional rollback"
        });
    });
    assert(!rolled_back);

    auto result = require_result(
        db.query("SELECT name, COUNT(*) AS total FROM mysqlwrapper_items "
                 "WHERE name IN ('committed', 'rolled-back') GROUP BY name ORDER BY name"),
        "transaction verification");
    assert(result.row_count() == 1);
    assert(get_or_throw<std::string>(result[0]["name"]) == "committed");
    assert(get_or_throw<std::int64_t>(result[0]["total"]) == 1);
}

void test_async_queries(Database& db) {
    auto future_a = db.query_async("SELECT COUNT(*) AS total FROM mysqlwrapper_items");
    auto future_b = db.execute_async(
        "INSERT INTO mysqlwrapper_items (name, quantity, price, enabled, payload) VALUES (?, ?, ?, ?, ?)",
        "async",
        3,
        2.0,
        false,
        nullptr);

    auto async_count = future_a.get();
    assert(async_count);
    assert(async_count->row_count() == 1);
    assert(get_or_throw<std::int64_t>((*async_count)[0]["total"]) >= 1);

    auto async_insert = future_b.get();
    assert(async_insert);
    assert(async_insert->affected_rows == 1);
}

void test_escape(Database& db) {
    auto escaped = db.escape("quote ' slash \\");
    assert(escaped);
    assert(escaped->find("\\'") != std::string::npos);
}

} // namespace

int main() {
    if (env_or_null("MYSQLWRAPPER_RUN_INTEGRATION") == nullptr) {
        std::cout << "mysqlwrapper integration tests skipped; set MYSQLWRAPPER_RUN_INTEGRATION=1\n";
        return 0;
    }

    Database db(integration_config());
    reset_schema(db);
    test_prepared_insert_query_and_blob(db);
    test_transaction_commit_and_rollback(db);
    test_async_queries(db);
    test_escape(db);

    std::cout << "mysqlwrapper integration tests passed\n";
}
