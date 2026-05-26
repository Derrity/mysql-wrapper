#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <future>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace mysqlw {

using Blob = std::vector<std::byte>;
using Value = std::variant<std::nullptr_t, std::int64_t, std::uint64_t, double, std::string, Blob, bool>;

enum class ErrorCode {
    ok = 0,
    mysql_init_failed,
    connection_failed,
    connection_lost,
    statement_init_failed,
    statement_prepare_failed,
    bind_failed,
    execute_failed,
    result_metadata_failed,
    result_bind_failed,
    result_fetch_failed,
    result_truncated,
    pool_stopped,
    pool_timeout,
    async_stopped,
    async_cancelled,
    invalid_argument,
    type_mismatch,
    transaction_failed
};

enum class Operation {
    unknown,
    connect,
    ping,
    query,
    execute,
    prepare,
    bind,
    fetch,
    begin_transaction,
    commit,
    rollback,
    async_submit,
    async_cancelled
};

struct DbError {
    ErrorCode code = ErrorCode::ok;
    Operation operation = Operation::unknown;
    unsigned mysql_errno = 0;
    std::string sql_state;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept { return code != ErrorCode::ok; }
};

class DbException final : public std::runtime_error {
public:
    explicit DbException(DbError error);

    [[nodiscard]] const DbError& error() const noexcept;

private:
    DbError error_;
};

template <typename T>
using Expected = std::expected<T, DbError>;

struct ConnectionConfig {
    std::string host = "localhost";
    std::uint16_t port = 3306;
    std::string user;
    std::string password;
    std::string database;
    std::string charset = "utf8mb4";
    std::size_t initial_pool_size = 4;
    std::size_t max_pool_size = 32;
    std::size_t worker_count = 0;
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds read_timeout{30};
    std::chrono::seconds write_timeout{30};
    std::chrono::milliseconds acquire_timeout{30000};
};

enum class ColumnType {
    null,
    signed_integer,
    unsigned_integer,
    floating,
    text,
    blob,
    boolean
};

struct Column {
    std::string name;
    ColumnType type = ColumnType::text;
    bool nullable = true;
    bool unsigned_value = false;
};

class RowView;

class Result {
public:
    using RowStorage = std::vector<Value>;

    Result() = default;
    Result(std::vector<Column> columns, std::vector<RowStorage> rows);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t row_count() const noexcept;
    [[nodiscard]] std::size_t column_count() const noexcept;
    [[nodiscard]] std::span<const Column> columns() const noexcept;
    [[nodiscard]] RowView row(std::size_t index) const;
    [[nodiscard]] RowView operator[](std::size_t index) const;
    [[nodiscard]] std::size_t column_index(std::string_view name) const;

private:
    friend class RowView;

    std::vector<Column> columns_;
    std::vector<RowStorage> rows_;
    std::unordered_map<std::string, std::size_t> column_index_;

    void rebuild_index();
};

class RowView {
public:
    RowView(const Result* result, std::size_t row_index) noexcept;

    [[nodiscard]] const Value& at(std::size_t column_index) const;
    [[nodiscard]] const Value& at(std::string_view column_name) const;
    [[nodiscard]] const Value& operator[](std::string_view column_name) const;
    [[nodiscard]] std::span<const Value> values() const;

private:
    const Result* result_ = nullptr;
    std::size_t row_index_ = 0;
};

struct ExecuteResult {
    std::uint64_t affected_rows = 0;
    std::uint64_t last_insert_id = 0;
};

struct PoolStats {
    std::size_t idle_connections = 0;
    std::size_t active_connections = 0;
    std::size_t created_connections = 0;
    std::size_t failed_connections = 0;
    std::size_t queued_tasks = 0;
};

class PreparedStatement;
class Transaction;

class ConnectionPool;

class Database;

Expected<Result> query_with_values(Database& database, std::string_view sql, std::vector<Value> values);
Expected<ExecuteResult> execute_with_values(Database& database, std::string_view sql, std::vector<Value> values);
std::future<Expected<Result>> submit_query_with_values(Database& database, std::string sql, std::vector<Value> values);
std::future<Expected<ExecuteResult>> submit_execute_with_values(
    Database& database,
    std::string sql,
    std::vector<Value> values);
Expected<Result> transaction_query_with_values(Transaction& tx, std::string_view sql, std::vector<Value> values);
Expected<ExecuteResult> transaction_execute_with_values(Transaction& tx, std::string_view sql, std::vector<Value> values);

namespace detail {

template <typename T>
struct expected_traits {
    static constexpr bool is_expected = false;
    using value_type = T;
};

template <typename T, typename E>
struct expected_traits<std::expected<T, E>> {
    static constexpr bool is_expected = true;
    using value_type = T;
};

} // namespace detail

class Database {
public:
    explicit Database(ConnectionConfig config);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    [[nodiscard]] Expected<Result> query(std::string_view sql);

    template <typename... Args>
    [[nodiscard]] Expected<Result> query(std::string_view sql, Args&&... args);

    [[nodiscard]] Expected<ExecuteResult> execute(std::string_view sql);

    template <typename... Args>
    [[nodiscard]] Expected<ExecuteResult> execute(std::string_view sql, Args&&... args);

    [[nodiscard]] std::future<Expected<Result>> query_async(std::string sql);

    template <typename... Args>
    [[nodiscard]] std::future<Expected<Result>> query_async(std::string sql, Args&&... args);

    [[nodiscard]] std::future<Expected<ExecuteResult>> execute_async(std::string sql);

    template <typename... Args>
    [[nodiscard]] std::future<Expected<ExecuteResult>> execute_async(std::string sql, Args&&... args);

    [[nodiscard]] Expected<Transaction> begin_transaction();

    template <typename Fn>
    [[nodiscard]] auto transaction(Fn&& fn);

    [[nodiscard]] Expected<std::string> escape(std::string_view value);
    [[nodiscard]] PoolStats stats() const;

    [[nodiscard]] Result query_or_throw(std::string_view sql);

    template <typename... Args>
    [[nodiscard]] Result query_or_throw(std::string_view sql, Args&&... args);

    [[nodiscard]] ExecuteResult execute_or_throw(std::string_view sql);

    template <typename... Args>
    [[nodiscard]] ExecuteResult execute_or_throw(std::string_view sql, Args&&... args);

private:
    friend Expected<Result> query_with_values(Database& database, std::string_view sql, std::vector<Value> values);
    friend Expected<ExecuteResult> execute_with_values(Database& database, std::string_view sql, std::vector<Value> values);
    friend std::future<Expected<Result>> submit_query_with_values(
        Database& database,
        std::string sql,
        std::vector<Value> values);
    friend std::future<Expected<ExecuteResult>> submit_execute_with_values(
        Database& database,
        std::string sql,
        std::vector<Value> values);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Transaction {
public:
    Transaction() noexcept;
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;

    [[nodiscard]] Expected<Result> query(std::string_view sql);

    template <typename... Args>
    [[nodiscard]] Expected<Result> query(std::string_view sql, Args&&... args);

    [[nodiscard]] Expected<ExecuteResult> execute(std::string_view sql);

    template <typename... Args>
    [[nodiscard]] Expected<ExecuteResult> execute(std::string_view sql, Args&&... args);

    [[nodiscard]] Expected<void> commit();
    [[nodiscard]] Expected<void> rollback();
    [[nodiscard]] bool active() const noexcept;

private:
    friend class Database;
    friend Expected<Result> transaction_query_with_values(Transaction& tx, std::string_view sql, std::vector<Value> values);
    friend Expected<ExecuteResult> transaction_execute_with_values(
        Transaction& tx,
        std::string_view sql,
        std::vector<Value> values);

    class Impl;
    explicit Transaction(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

template <typename T>
[[nodiscard]] Expected<T> get_as(const Value& value);

template <typename T>
[[nodiscard]] T get_or_throw(const Value& value);

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(Operation operation) noexcept;

namespace detail {

template <typename T>
Value make_value(T&& value);

template <>
inline Value make_value<std::nullptr_t>(std::nullptr_t&&) {
    return nullptr;
}

template <>
inline Value make_value<const std::nullptr_t&>(const std::nullptr_t&) {
    return nullptr;
}

template <typename T>
    requires(std::same_as<std::remove_cvref_t<T>, Value>)
Value make_value(T&& value) {
    return std::forward<T>(value);
}

template <typename T>
    requires(std::same_as<std::remove_cvref_t<T>, std::string>)
Value make_value(T&& value) {
    return std::string(std::forward<T>(value));
}

template <typename T>
    requires(std::same_as<std::remove_cvref_t<T>, std::string_view>)
Value make_value(T&& value) {
    return std::string(std::forward<T>(value));
}

inline Value make_value(const char* value) {
    return value == nullptr ? Value{nullptr} : Value{std::string(value)};
}

inline Value make_value(char* value) {
    return make_value(static_cast<const char*>(value));
}

template <std::size_t N>
Value make_value(const char (&value)[N]) {
    return std::string(value);
}

template <typename T>
    requires(std::same_as<std::remove_cvref_t<T>, Blob>)
Value make_value(T&& value) {
    return Blob(std::forward<T>(value));
}

inline Value make_value(std::span<const std::byte> value) {
    return Blob(value.begin(), value.end());
}

inline Value make_value(std::span<const std::uint8_t> value) {
    Blob blob;
    blob.reserve(value.size());
    for (auto byte : value) {
        blob.push_back(static_cast<std::byte>(byte));
    }
    return blob;
}

template <typename T>
    requires(std::same_as<std::remove_cvref_t<T>, bool>)
Value make_value(T&& value) {
    return static_cast<bool>(value);
}

template <typename T>
    requires(std::integral<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> &&
             std::signed_integral<std::remove_cvref_t<T>>)
Value make_value(T&& value) {
    return static_cast<std::int64_t>(value);
}

template <typename T>
    requires(std::integral<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> &&
             std::unsigned_integral<std::remove_cvref_t<T>>)
Value make_value(T&& value) {
    return static_cast<std::uint64_t>(value);
}

template <typename T>
    requires(std::floating_point<std::remove_cvref_t<T>>)
Value make_value(T&& value) {
    return static_cast<double>(value);
}

template <typename... Args>
std::vector<Value> make_values(Args&&... args) {
    std::vector<Value> values;
    values.reserve(sizeof...(Args));
    (values.emplace_back(make_value(std::forward<Args>(args))), ...);
    return values;
}

} // namespace detail

template <typename... Args>
Expected<Result> Database::query(std::string_view sql, Args&&... args) {
    return query_with_values(*this, sql, detail::make_values(std::forward<Args>(args)...));
}

template <typename... Args>
Expected<ExecuteResult> Database::execute(std::string_view sql, Args&&... args) {
    return execute_with_values(*this, sql, detail::make_values(std::forward<Args>(args)...));
}

template <typename... Args>
std::future<Expected<Result>> Database::query_async(std::string sql, Args&&... args) {
    auto values = detail::make_values(std::forward<Args>(args)...);
    return submit_query_with_values(*this, std::move(sql), std::move(values));
}

template <typename... Args>
std::future<Expected<ExecuteResult>> Database::execute_async(std::string sql, Args&&... args) {
    auto values = detail::make_values(std::forward<Args>(args)...);
    return submit_execute_with_values(*this, std::move(sql), std::move(values));
}

template <typename Fn>
auto Database::transaction(Fn&& fn) {
    using RawResult = std::invoke_result_t<Fn, Transaction&>;
    using Traits = detail::expected_traits<RawResult>;
    using ResultValue = typename Traits::value_type;

    auto tx = begin_transaction();
    if (!tx) {
        return std::expected<ResultValue, DbError>(std::unexpected(tx.error()));
    }

    if constexpr (Traits::is_expected) {
        auto result = std::forward<Fn>(fn)(*tx);
        if (!result) {
            auto rollback_result = tx->rollback();
            (void)rollback_result;
            return std::expected<ResultValue, DbError>(std::unexpected(result.error()));
        }

        auto commit_result = tx->commit();
        if (!commit_result) {
            return std::expected<ResultValue, DbError>(std::unexpected(commit_result.error()));
        }

        if constexpr (std::is_void_v<ResultValue>) {
            return std::expected<void, DbError>{};
        } else {
            return std::expected<ResultValue, DbError>(std::move(*result));
        }
    } else {
        if constexpr (std::is_void_v<ResultValue>) {
            std::forward<Fn>(fn)(*tx);
            auto commit_result = tx->commit();
            if (!commit_result) {
                return std::expected<void, DbError>(std::unexpected(commit_result.error()));
            }
            return std::expected<void, DbError>{};
        } else {
            auto result = std::forward<Fn>(fn)(*tx);
            auto commit_result = tx->commit();
            if (!commit_result) {
                return std::expected<ResultValue, DbError>(std::unexpected(commit_result.error()));
            }
            return std::expected<ResultValue, DbError>(std::move(result));
        }
    }
}

template <typename... Args>
Result Database::query_or_throw(std::string_view sql, Args&&... args) {
    auto result = query(sql, std::forward<Args>(args)...);
    if (!result) {
        throw DbException(std::move(result.error()));
    }
    return std::move(*result);
}

template <typename... Args>
ExecuteResult Database::execute_or_throw(std::string_view sql, Args&&... args) {
    auto result = execute(sql, std::forward<Args>(args)...);
    if (!result) {
        throw DbException(std::move(result.error()));
    }
    return std::move(*result);
}

template <typename... Args>
Expected<Result> Transaction::query(std::string_view sql, Args&&... args) {
    return transaction_query_with_values(*this, sql, detail::make_values(std::forward<Args>(args)...));
}

template <typename... Args>
Expected<ExecuteResult> Transaction::execute(std::string_view sql, Args&&... args) {
    return transaction_execute_with_values(*this, sql, detail::make_values(std::forward<Args>(args)...));
}

template <typename T>
Expected<T> get_as(const Value& value) {
    if (const auto* found = std::get_if<T>(&value)) {
        return *found;
    }
    return std::unexpected(DbError{
        .code = ErrorCode::type_mismatch,
        .operation = Operation::unknown,
        .message = "value type mismatch"
    });
}

template <typename T>
T get_or_throw(const Value& value) {
    auto result = get_as<T>(value);
    if (!result) {
        throw DbException(std::move(result.error()));
    }
    return std::move(*result);
}

} // namespace mysqlw
