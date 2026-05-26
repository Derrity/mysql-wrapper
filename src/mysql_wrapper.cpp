#include "mysqlwrapper/mysql_wrapper.hpp"

#include <mysql.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <thread>
#include <utility>

namespace mysqlw {
namespace {

constexpr auto mysql_success = 0;

std::string make_message(std::string_view prefix, const char* detail) {
    std::string message(prefix);
    if (detail != nullptr && detail[0] != '\0') {
        message += ": ";
        message += detail;
    }
    return message;
}

DbError make_error(ErrorCode code, Operation operation, std::string message) {
    return DbError{
        .code = code,
        .operation = operation,
        .message = std::move(message)
    };
}

DbError make_mysql_error(ErrorCode code, Operation operation, MYSQL* mysql, std::string_view prefix) {
    return DbError{
        .code = code,
        .operation = operation,
        .mysql_errno = mysql == nullptr ? 0U : mysql_errno(mysql),
        .sql_state = mysql == nullptr ? std::string{} : std::string(mysql_sqlstate(mysql)),
        .message = make_message(prefix, mysql == nullptr ? nullptr : mysql_error(mysql))
    };
}

DbError make_stmt_error(ErrorCode code, Operation operation, MYSQL_STMT* stmt, std::string_view prefix) {
    return DbError{
        .code = code,
        .operation = operation,
        .mysql_errno = stmt == nullptr ? 0U : mysql_stmt_errno(stmt),
        .sql_state = stmt == nullptr ? std::string{} : std::string(mysql_stmt_sqlstate(stmt)),
        .message = make_message(prefix, stmt == nullptr ? nullptr : mysql_stmt_error(stmt))
    };
}

std::uint32_t to_mysql_timeout(std::chrono::seconds timeout) {
    const auto count = timeout.count();
    if (count <= 0) {
        return 0;
    }
    if (static_cast<unsigned long long>(count) > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(count);
}

struct MysqlDeleter {
    void operator()(MYSQL* mysql) const noexcept {
        if (mysql != nullptr) {
            mysql_close(mysql);
        }
    }
};

struct StmtDeleter {
    void operator()(MYSQL_STMT* stmt) const noexcept {
        if (stmt != nullptr) {
            mysql_stmt_close(stmt);
        }
    }
};

struct ResultDeleter {
    void operator()(MYSQL_RES* result) const noexcept {
        if (result != nullptr) {
            mysql_free_result(result);
        }
    }
};

using MysqlHandle = std::unique_ptr<MYSQL, MysqlDeleter>;
using StmtHandle = std::unique_ptr<MYSQL_STMT, StmtDeleter>;
using MetadataHandle = std::unique_ptr<MYSQL_RES, ResultDeleter>;

ColumnType column_type_from_field(const MYSQL_FIELD& field) {
    switch (field.type) {
        case MYSQL_TYPE_TINY:
            return (field.flags & UNSIGNED_FLAG) != 0 ? ColumnType::unsigned_integer : ColumnType::signed_integer;
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_YEAR:
            return (field.flags & UNSIGNED_FLAG) != 0 ? ColumnType::unsigned_integer : ColumnType::signed_integer;
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            return ColumnType::floating;
        case MYSQL_TYPE_BIT:
            return ColumnType::blob;
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_GEOMETRY:
            return (field.flags & BINARY_FLAG) != 0 ? ColumnType::blob : ColumnType::text;
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_STRING:
            return (field.flags & BINARY_FLAG) != 0 ? ColumnType::blob : ColumnType::text;
        default:
            return ColumnType::text;
    }
}

enum class FieldDecode {
    signed_integer,
    unsigned_integer,
    floating,
    text,
    blob
};

FieldDecode decode_kind(const MYSQL_FIELD& field) {
    switch (field.type) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_YEAR:
            return (field.flags & UNSIGNED_FLAG) != 0 ? FieldDecode::unsigned_integer : FieldDecode::signed_integer;
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            return FieldDecode::floating;
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_GEOMETRY:
        case MYSQL_TYPE_BIT:
            return (field.flags & BINARY_FLAG) != 0 ? FieldDecode::blob : FieldDecode::text;
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_STRING:
            return (field.flags & BINARY_FLAG) != 0 ? FieldDecode::blob : FieldDecode::text;
        default:
            return FieldDecode::text;
    }
}

std::string field_name(const MYSQL_FIELD& field) {
    if (field.name == nullptr) {
        return {};
    }
    return std::string(field.name, field.name_length);
}

std::uint64_t mysql_affected_to_u64(my_ulonglong value) {
    if (value == static_cast<my_ulonglong>(-1)) {
        return 0;
    }
    return static_cast<std::uint64_t>(value);
}

class Connection;

class ConnectionLease {
public:
    ConnectionLease() noexcept = default;
    ConnectionLease(std::shared_ptr<Connection> connection, class ConnectionPoolImpl* pool) noexcept
        : connection_(std::move(connection)), pool_(pool) {}

    ~ConnectionLease();

    ConnectionLease(const ConnectionLease&) = delete;
    ConnectionLease& operator=(const ConnectionLease&) = delete;

    ConnectionLease(ConnectionLease&& other) noexcept
        : connection_(std::move(other.connection_)), pool_(std::exchange(other.pool_, nullptr)) {}

    ConnectionLease& operator=(ConnectionLease&& other) noexcept {
        if (this != &other) {
            reset();
            connection_ = std::move(other.connection_);
            pool_ = std::exchange(other.pool_, nullptr);
        }
        return *this;
    }

    Connection& operator*() const noexcept { return *connection_; }
    Connection* operator->() const noexcept { return connection_.get(); }
    explicit operator bool() const noexcept { return static_cast<bool>(connection_); }

    void reset() noexcept;

private:
    std::shared_ptr<Connection> connection_;
    class ConnectionPoolImpl* pool_ = nullptr;
};

struct BoundParam {
    MYSQL_BIND bind{};
    Value value;
    unsigned long length = 0;
    bool is_null = false;
    bool error = false;

    explicit BoundParam(Value input) : value(std::move(input)) {
        rebuild_bind();
    }

    BoundParam(const BoundParam&) = delete;
    BoundParam& operator=(const BoundParam&) = delete;

    BoundParam(BoundParam&& other) noexcept
        : value(std::move(other.value)), length(other.length), is_null(other.is_null), error(other.error) {
        rebuild_bind();
    }

    BoundParam& operator=(BoundParam&& other) noexcept {
        if (this != &other) {
            value = std::move(other.value);
            length = other.length;
            is_null = other.is_null;
            error = other.error;
            rebuild_bind();
        }
        return *this;
    }

    void rebuild_bind() noexcept {
        std::memset(&bind, 0, sizeof(bind));
        bind.length = &length;
        bind.error = &error;

        std::visit([this](auto& stored) {
            using T = std::decay_t<decltype(stored)>;
            if constexpr (std::same_as<T, std::nullptr_t>) {
                is_null = true;
                bind.buffer_type = MYSQL_TYPE_NULL;
                bind.is_null = &is_null;
            } else if constexpr (std::same_as<T, std::int64_t>) {
                bind.buffer_type = MYSQL_TYPE_LONGLONG;
                bind.buffer = &stored;
                bind.is_unsigned = false;
                length = sizeof(stored);
            } else if constexpr (std::same_as<T, std::uint64_t>) {
                bind.buffer_type = MYSQL_TYPE_LONGLONG;
                bind.buffer = &stored;
                bind.is_unsigned = true;
                length = sizeof(stored);
            } else if constexpr (std::same_as<T, double>) {
                bind.buffer_type = MYSQL_TYPE_DOUBLE;
                bind.buffer = &stored;
                length = sizeof(stored);
            } else if constexpr (std::same_as<T, std::string>) {
                bind.buffer_type = MYSQL_TYPE_STRING;
                bind.buffer = stored.empty() ? nullptr : stored.data();
                length = static_cast<unsigned long>(stored.size());
                bind.buffer_length = length;
            } else if constexpr (std::same_as<T, Blob>) {
                bind.buffer_type = MYSQL_TYPE_BLOB;
                bind.buffer = stored.empty() ? nullptr : stored.data();
                length = static_cast<unsigned long>(stored.size());
                bind.buffer_length = length;
            } else if constexpr (std::same_as<T, bool>) {
                bind.buffer_type = MYSQL_TYPE_TINY;
                bind.buffer = &stored;
                length = sizeof(stored);
            }
        }, value);
    }
};

struct BoolSlot {
    bool value = false;
};

class Statement {
public:
    Statement(StmtHandle stmt, std::string sql) : stmt_(std::move(stmt)), sql_(std::move(sql)) {}

    [[nodiscard]] Expected<Result> query(std::vector<Value> values) {
        if (auto bound = bind(std::move(values)); !bound) {
            return std::unexpected(bound.error());
        }

        if (mysql_stmt_execute(stmt_.get()) != mysql_success) {
            return std::unexpected(make_stmt_error(ErrorCode::execute_failed, Operation::execute, stmt_.get(),
                                                  "failed to execute statement"));
        }

        return fetch_result();
    }

    [[nodiscard]] Expected<ExecuteResult> execute(std::vector<Value> values) {
        if (auto bound = bind(std::move(values)); !bound) {
            return std::unexpected(bound.error());
        }

        if (mysql_stmt_execute(stmt_.get()) != mysql_success) {
            return std::unexpected(make_stmt_error(ErrorCode::execute_failed, Operation::execute, stmt_.get(),
                                                  "failed to execute statement"));
        }

        return ExecuteResult{
            .affected_rows = mysql_affected_to_u64(mysql_stmt_affected_rows(stmt_.get())),
            .last_insert_id = static_cast<std::uint64_t>(mysql_stmt_insert_id(stmt_.get()))
        };
    }

private:
    StmtHandle stmt_;
    std::string sql_;
    std::vector<BoundParam> params_;
    std::vector<MYSQL_BIND> bind_params_;

    [[nodiscard]] Expected<void> bind(std::vector<Value> values) {
        const auto expected_count = mysql_stmt_param_count(stmt_.get());
        if (values.size() != expected_count) {
            std::ostringstream oss;
            oss << "statement expected " << expected_count << " parameters, got " << values.size();
            return std::unexpected(make_error(ErrorCode::invalid_argument, Operation::bind, oss.str()));
        }

        params_.clear();
        bind_params_.clear();
        params_.reserve(values.size());
        bind_params_.reserve(values.size());

        for (auto& value : values) {
            params_.emplace_back(std::move(value));
        }
        for (auto& param : params_) {
            bind_params_.push_back(param.bind);
        }

        if (!bind_params_.empty() && mysql_stmt_bind_param(stmt_.get(), bind_params_.data()) != mysql_success) {
            return std::unexpected(make_stmt_error(ErrorCode::bind_failed, Operation::bind, stmt_.get(),
                                                  "failed to bind statement parameters"));
        }

        return {};
    }

    [[nodiscard]] Expected<Result> fetch_result() {
        MetadataHandle metadata(mysql_stmt_result_metadata(stmt_.get()));
        if (!metadata) {
            return Result{};
        }

        const auto update_max_length = true;
        (void)mysql_stmt_attr_set(stmt_.get(), STMT_ATTR_UPDATE_MAX_LENGTH, &update_max_length);

        if (mysql_stmt_store_result(stmt_.get()) != mysql_success) {
            return std::unexpected(make_stmt_error(ErrorCode::result_metadata_failed, Operation::fetch, stmt_.get(),
                                                  "failed to store statement result"));
        }

        const auto field_count = mysql_num_fields(metadata.get());
        MYSQL_FIELD* fields = mysql_fetch_fields(metadata.get());

        std::vector<Column> columns;
        columns.reserve(field_count);
        std::vector<FieldDecode> decode_kinds;
        decode_kinds.reserve(field_count);

        for (unsigned int index = 0; index < field_count; ++index) {
            columns.push_back(Column{
                .name = field_name(fields[index]),
                .type = column_type_from_field(fields[index]),
                .nullable = (fields[index].flags & NOT_NULL_FLAG) == 0,
                .unsigned_value = (fields[index].flags & UNSIGNED_FLAG) != 0
            });
            decode_kinds.push_back(decode_kind(fields[index]));
        }

        std::vector<MYSQL_BIND> result_binds(field_count);
        std::vector<std::vector<unsigned char>> buffers(field_count);
        std::vector<unsigned long> lengths(field_count);
        std::vector<BoolSlot> null_storage(field_count);
        std::vector<BoolSlot> error_storage(field_count);

        for (unsigned int index = 0; index < field_count; ++index) {
            std::memset(&result_binds[index], 0, sizeof(MYSQL_BIND));

            auto buffer_size = std::max<unsigned long>(fields[index].max_length, fields[index].length);
            buffer_size = std::max<unsigned long>(buffer_size, 1);
            if (decode_kinds[index] == FieldDecode::signed_integer || decode_kinds[index] == FieldDecode::unsigned_integer) {
                buffer_size = sizeof(std::uint64_t);
                result_binds[index].buffer_type = MYSQL_TYPE_LONGLONG;
                result_binds[index].is_unsigned = decode_kinds[index] == FieldDecode::unsigned_integer;
            } else if (decode_kinds[index] == FieldDecode::floating) {
                buffer_size = sizeof(double);
                result_binds[index].buffer_type = MYSQL_TYPE_DOUBLE;
            } else {
                result_binds[index].buffer_type = MYSQL_TYPE_STRING;
            }

            buffers[index].resize(buffer_size);
            result_binds[index].buffer = buffers[index].data();
            result_binds[index].buffer_length = static_cast<unsigned long>(buffers[index].size());
            result_binds[index].length = &lengths[index];
            result_binds[index].is_null = &null_storage[index].value;
            result_binds[index].error = &error_storage[index].value;
        }

        if (field_count > 0 && mysql_stmt_bind_result(stmt_.get(), result_binds.data()) != mysql_success) {
            return std::unexpected(make_stmt_error(ErrorCode::result_bind_failed, Operation::fetch, stmt_.get(),
                                                  "failed to bind result buffers"));
        }

        std::vector<Result::RowStorage> rows;
        while (true) {
            const auto fetch_status = mysql_stmt_fetch(stmt_.get());
            if (fetch_status == MYSQL_NO_DATA) {
                break;
            }
            if (fetch_status == 1) {
                return std::unexpected(make_stmt_error(ErrorCode::result_fetch_failed, Operation::fetch, stmt_.get(),
                                                      "failed to fetch result row"));
            }
            if (fetch_status == MYSQL_DATA_TRUNCATED) {
                for (unsigned int index = 0; index < field_count; ++index) {
                    if (error_storage[index].value && lengths[index] > buffers[index].size()) {
                        buffers[index].resize(lengths[index]);
                        result_binds[index].buffer = buffers[index].data();
                        result_binds[index].buffer_length = static_cast<unsigned long>(buffers[index].size());
                        if (mysql_stmt_fetch_column(stmt_.get(), &result_binds[index], index, 0) != mysql_success) {
                            return std::unexpected(make_stmt_error(ErrorCode::result_fetch_failed, Operation::fetch,
                                                                  stmt_.get(), "failed to fetch truncated column"));
                        }
                    }
                }
            }

            Result::RowStorage row;
            row.reserve(field_count);
            for (unsigned int index = 0; index < field_count; ++index) {
                if (null_storage[index].value) {
                    row.emplace_back(nullptr);
                    continue;
                }

                switch (decode_kinds[index]) {
                    case FieldDecode::signed_integer:
                        row.emplace_back(*reinterpret_cast<std::int64_t*>(buffers[index].data()));
                        break;
                    case FieldDecode::unsigned_integer:
                        row.emplace_back(*reinterpret_cast<std::uint64_t*>(buffers[index].data()));
                        break;
                    case FieldDecode::floating:
                        row.emplace_back(*reinterpret_cast<double*>(buffers[index].data()));
                        break;
                    case FieldDecode::blob: {
                        Blob blob;
                        blob.reserve(lengths[index]);
                        for (unsigned long byte_index = 0; byte_index < lengths[index]; ++byte_index) {
                            blob.push_back(static_cast<std::byte>(buffers[index][byte_index]));
                        }
                        row.emplace_back(std::move(blob));
                        break;
                    }
                    case FieldDecode::text:
                        row.emplace_back(std::string(reinterpret_cast<char*>(buffers[index].data()), lengths[index]));
                        break;
                }
            }
            rows.push_back(std::move(row));
        }

        mysql_stmt_free_result(stmt_.get());
        return Result(std::move(columns), std::move(rows));
    }
};

class Connection {
public:
    explicit Connection(ConnectionConfig config) : config_(std::move(config)) {}

    [[nodiscard]] Expected<void> connect() {
        MysqlHandle mysql(mysql_init(nullptr));
        if (!mysql) {
            return std::unexpected(make_error(ErrorCode::mysql_init_failed, Operation::connect,
                                             "failed to initialize MySQL handle"));
        }

        const auto connect_timeout = to_mysql_timeout(config_.connect_timeout);
        const auto read_timeout = to_mysql_timeout(config_.read_timeout);
        const auto write_timeout = to_mysql_timeout(config_.write_timeout);
        mysql_options(mysql.get(), MYSQL_SET_CHARSET_NAME, config_.charset.c_str());
        mysql_options(mysql.get(), MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
        mysql_options(mysql.get(), MYSQL_OPT_READ_TIMEOUT, &read_timeout);
        mysql_options(mysql.get(), MYSQL_OPT_WRITE_TIMEOUT, &write_timeout);

        if (mysql_real_connect(mysql.get(), config_.host.c_str(), config_.user.c_str(), config_.password.c_str(),
                               config_.database.c_str(), config_.port, nullptr, 0) == nullptr) {
            return std::unexpected(make_mysql_error(ErrorCode::connection_failed, Operation::connect, mysql.get(),
                                                   "failed to connect to MySQL"));
        }

        if (mysql_set_character_set(mysql.get(), config_.charset.c_str()) != mysql_success) {
            return std::unexpected(make_mysql_error(ErrorCode::connection_failed, Operation::connect, mysql.get(),
                                                   "failed to set MySQL charset"));
        }

        mysql_ = std::move(mysql);
        return {};
    }

    [[nodiscard]] Expected<void> ping() {
        std::lock_guard lock(mutex_);
        if (!mysql_) {
            return std::unexpected(make_error(ErrorCode::connection_lost, Operation::ping, "connection is not open"));
        }
        if (mysql_ping(mysql_.get()) != mysql_success) {
            return std::unexpected(make_mysql_error(ErrorCode::connection_lost, Operation::ping, mysql_.get(),
                                                   "MySQL ping failed"));
        }
        return {};
    }

    [[nodiscard]] Expected<Result> query(std::string_view sql) {
        std::lock_guard lock(mutex_);
        if (!mysql_) {
            return std::unexpected(make_error(ErrorCode::connection_lost, Operation::query, "connection is not open"));
        }
        if (mysql_query(mysql_.get(), std::string(sql).c_str()) != mysql_success) {
            return std::unexpected(make_mysql_error(ErrorCode::execute_failed, Operation::query, mysql_.get(),
                                                   "query failed"));
        }

        MetadataHandle result(mysql_store_result(mysql_.get()));
        if (!result) {
            if (mysql_field_count(mysql_.get()) == 0) {
                return Result{};
            }
            return std::unexpected(make_mysql_error(ErrorCode::result_metadata_failed, Operation::fetch, mysql_.get(),
                                                   "failed to store query result"));
        }

        const auto field_count = mysql_num_fields(result.get());
        MYSQL_FIELD* fields = mysql_fetch_fields(result.get());
        std::vector<Column> columns;
        columns.reserve(field_count);
        std::vector<FieldDecode> decode_kinds;
        decode_kinds.reserve(field_count);

        for (unsigned int index = 0; index < field_count; ++index) {
            columns.push_back(Column{
                .name = field_name(fields[index]),
                .type = column_type_from_field(fields[index]),
                .nullable = (fields[index].flags & NOT_NULL_FLAG) == 0,
                .unsigned_value = (fields[index].flags & UNSIGNED_FLAG) != 0
            });
            decode_kinds.push_back(decode_kind(fields[index]));
        }

        std::vector<Result::RowStorage> rows;
        MYSQL_ROW mysql_row = nullptr;
        while ((mysql_row = mysql_fetch_row(result.get())) != nullptr) {
            unsigned long* lengths = mysql_fetch_lengths(result.get());
            Result::RowStorage row;
            row.reserve(field_count);
            for (unsigned int index = 0; index < field_count; ++index) {
                if (mysql_row[index] == nullptr) {
                    row.emplace_back(nullptr);
                    continue;
                }

                const std::string_view cell(mysql_row[index], lengths[index]);
                try {
                    switch (decode_kinds[index]) {
                        case FieldDecode::signed_integer:
                            row.emplace_back(static_cast<std::int64_t>(std::stoll(std::string(cell))));
                            break;
                        case FieldDecode::unsigned_integer:
                            row.emplace_back(static_cast<std::uint64_t>(std::stoull(std::string(cell))));
                            break;
                        case FieldDecode::floating:
                            row.emplace_back(std::stod(std::string(cell)));
                            break;
                        case FieldDecode::blob: {
                            Blob blob;
                            blob.reserve(cell.size());
                            for (unsigned char byte : cell) {
                                blob.push_back(static_cast<std::byte>(byte));
                            }
                            row.emplace_back(std::move(blob));
                            break;
                        }
                        case FieldDecode::text:
                            row.emplace_back(std::string(cell));
                            break;
                    }
                } catch (const std::exception& ex) {
                    return std::unexpected(make_error(ErrorCode::result_fetch_failed, Operation::fetch, ex.what()));
                }
            }
            rows.push_back(std::move(row));
        }

        return Result(std::move(columns), std::move(rows));
    }

    [[nodiscard]] Expected<ExecuteResult> execute(std::string_view sql) {
        std::lock_guard lock(mutex_);
        if (!mysql_) {
            return std::unexpected(make_error(ErrorCode::connection_lost, Operation::execute, "connection is not open"));
        }
        if (mysql_query(mysql_.get(), std::string(sql).c_str()) != mysql_success) {
            return std::unexpected(make_mysql_error(ErrorCode::execute_failed, Operation::execute, mysql_.get(),
                                                   "execute failed"));
        }
        return ExecuteResult{
            .affected_rows = mysql_affected_to_u64(mysql_affected_rows(mysql_.get())),
            .last_insert_id = static_cast<std::uint64_t>(mysql_insert_id(mysql_.get()))
        };
    }

    [[nodiscard]] Expected<Statement> prepare(std::string_view sql) {
        std::lock_guard lock(mutex_);
        if (!mysql_) {
            return std::unexpected(make_error(ErrorCode::connection_lost, Operation::prepare, "connection is not open"));
        }

        StmtHandle stmt(mysql_stmt_init(mysql_.get()));
        if (!stmt) {
            return std::unexpected(make_mysql_error(ErrorCode::statement_init_failed, Operation::prepare, mysql_.get(),
                                                   "failed to create statement"));
        }

        const auto sql_text = std::string(sql);
        if (mysql_stmt_prepare(stmt.get(), sql_text.c_str(), static_cast<unsigned long>(sql_text.size())) != mysql_success) {
            return std::unexpected(make_stmt_error(ErrorCode::statement_prepare_failed, Operation::prepare, stmt.get(),
                                                  "failed to prepare statement"));
        }

        return Statement(std::move(stmt), sql_text);
    }

    [[nodiscard]] Expected<void> begin_transaction() {
        auto result = execute("START TRANSACTION");
        if (!result) {
            return std::unexpected(result.error());
        }
        in_transaction_.store(true, std::memory_order_release);
        return {};
    }

    [[nodiscard]] Expected<void> commit() {
        auto result = execute("COMMIT");
        if (!result) {
            return std::unexpected(result.error());
        }
        in_transaction_.store(false, std::memory_order_release);
        return {};
    }

    [[nodiscard]] Expected<void> rollback() {
        auto result = execute("ROLLBACK");
        if (!result) {
            return std::unexpected(result.error());
        }
        in_transaction_.store(false, std::memory_order_release);
        return {};
    }

    [[nodiscard]] bool in_transaction() const noexcept {
        return in_transaction_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Expected<std::string> escape(std::string_view value) {
        std::lock_guard lock(mutex_);
        if (!mysql_) {
            return std::unexpected(make_error(ErrorCode::connection_lost, Operation::query, "connection is not open"));
        }
        std::string output;
        output.resize(value.size() * 2 + 1);
        const auto escaped_length = mysql_real_escape_string(mysql_.get(), output.data(), value.data(),
                                                            static_cast<unsigned long>(value.size()));
        output.resize(escaped_length);
        return output;
    }

private:
    ConnectionConfig config_;
    MysqlHandle mysql_;
    mutable std::mutex mutex_;
    std::atomic_bool in_transaction_{false};
};

class ConnectionPoolImpl {
public:
    explicit ConnectionPoolImpl(ConnectionConfig config) : config_(std::move(config)) {
        if (config_.max_pool_size == 0) {
            config_.max_pool_size = 1;
        }
        if (config_.initial_pool_size > config_.max_pool_size) {
            config_.initial_pool_size = config_.max_pool_size;
        }
    }

    [[nodiscard]] Expected<void> initialize() {
        std::lock_guard lock(mutex_);
        for (std::size_t index = 0; index < config_.initial_pool_size; ++index) {
            auto created = create_connection_locked();
            if (!created) {
                return std::unexpected(created.error());
            }
            idle_.push(std::move(*created));
        }
        return {};
    }

    [[nodiscard]] Expected<ConnectionLease> acquire() {
        std::unique_lock lock(mutex_);
        const auto deadline = std::chrono::steady_clock::now() + config_.acquire_timeout;

        while (idle_.empty() && active_connections_ >= config_.max_pool_size && !stopped_) {
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return std::unexpected(make_error(ErrorCode::pool_timeout, Operation::query,
                                                 "timed out waiting for a MySQL connection"));
            }
        }

        if (stopped_) {
            return std::unexpected(make_error(ErrorCode::pool_stopped, Operation::query, "connection pool is stopped"));
        }

        std::shared_ptr<Connection> connection;
        if (!idle_.empty()) {
            connection = std::move(idle_.front());
            idle_.pop();
        } else {
            auto created = create_connection_locked();
            if (!created) {
                return std::unexpected(created.error());
            }
            connection = std::move(*created);
        }

        ++active_connections_;
        lock.unlock();

        if (auto ping = connection->ping(); !ping) {
            auto replacement = create_connection_unlocked();
            if (!replacement) {
                release(std::move(connection));
                return std::unexpected(replacement.error());
            }
            connection = std::move(*replacement);
        }

        return ConnectionLease(std::move(connection), this);
    }

    void release(std::shared_ptr<Connection> connection) noexcept {
        if (!connection) {
            return;
        }

        if (connection->in_transaction()) {
            (void)connection->rollback();
        }

        {
            std::lock_guard lock(mutex_);
            if (!stopped_ && idle_.size() < config_.max_pool_size) {
                idle_.push(std::move(connection));
            }
            if (active_connections_ > 0) {
                --active_connections_;
            }
        }
        cv_.notify_one();
    }

    void stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            std::queue<std::shared_ptr<Connection>> empty;
            idle_.swap(empty);
        }
        cv_.notify_all();
    }

    [[nodiscard]] PoolStats stats(std::size_t queued_tasks) const {
        std::lock_guard lock(mutex_);
        return PoolStats{
            .idle_connections = idle_.size(),
            .active_connections = active_connections_,
            .created_connections = created_connections_.load(std::memory_order_relaxed),
            .failed_connections = failed_connections_.load(std::memory_order_relaxed),
            .queued_tasks = queued_tasks
        };
    }

private:
    ConnectionConfig config_;
    std::queue<std::shared_ptr<Connection>> idle_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
    std::size_t active_connections_ = 0;
    std::atomic_size_t created_connections_{0};
    std::atomic_size_t failed_connections_{0};

    [[nodiscard]] Expected<std::shared_ptr<Connection>> create_connection_locked() {
        auto connection = std::make_shared<Connection>(config_);
        if (auto connected = connection->connect(); !connected) {
            failed_connections_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(connected.error());
        }
        created_connections_.fetch_add(1, std::memory_order_relaxed);
        return connection;
    }

    [[nodiscard]] Expected<std::shared_ptr<Connection>> create_connection_unlocked() {
        auto connection = std::make_shared<Connection>(config_);
        if (auto connected = connection->connect(); !connected) {
            failed_connections_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(connected.error());
        }
        created_connections_.fetch_add(1, std::memory_order_relaxed);
        return connection;
    }
};

ConnectionLease::~ConnectionLease() {
    reset();
}

void ConnectionLease::reset() noexcept {
    if (pool_ != nullptr && connection_) {
        pool_->release(std::move(connection_));
    }
    pool_ = nullptr;
}

struct Task {
    std::function<void(std::stop_token)> run;
    std::function<void(DbError)> cancel;
};

DbError cancelled_error() {
    return DbError{
        .code = ErrorCode::async_cancelled,
        .operation = Operation::async_cancelled,
        .message = "async task cancelled"
    };
}

} // namespace

DbException::DbException(DbError error) : std::runtime_error(error.message), error_(std::move(error)) {}

const DbError& DbException::error() const noexcept {
    return error_;
}

Result::Result(std::vector<Column> columns, std::vector<RowStorage> rows)
    : columns_(std::move(columns)), rows_(std::move(rows)) {
    rebuild_index();
}

bool Result::empty() const noexcept {
    return rows_.empty();
}

std::size_t Result::row_count() const noexcept {
    return rows_.size();
}

std::size_t Result::column_count() const noexcept {
    return columns_.size();
}

std::span<const Column> Result::columns() const noexcept {
    return columns_;
}

RowView Result::row(std::size_t index) const {
    if (index >= rows_.size()) {
        throw std::out_of_range("row index out of range");
    }
    return RowView(this, index);
}

RowView Result::operator[](std::size_t index) const {
    return row(index);
}

std::size_t Result::column_index(std::string_view name) const {
    const auto found = column_index_.find(std::string(name));
    if (found == column_index_.end()) {
        throw std::out_of_range("column not found");
    }
    return found->second;
}

void Result::rebuild_index() {
    column_index_.clear();
    column_index_.reserve(columns_.size());
    for (std::size_t index = 0; index < columns_.size(); ++index) {
        column_index_.emplace(columns_[index].name, index);
    }
}

RowView::RowView(const Result* result, std::size_t row_index) noexcept : result_(result), row_index_(row_index) {}

const Value& RowView::at(std::size_t column_index) const {
    if (result_ == nullptr || row_index_ >= result_->rows_.size()) {
        throw std::out_of_range("row view is invalid");
    }
    const auto& row = result_->rows_[row_index_];
    if (column_index >= row.size()) {
        throw std::out_of_range("column index out of range");
    }
    return row[column_index];
}

const Value& RowView::at(std::string_view column_name) const {
    return at(result_->column_index(column_name));
}

const Value& RowView::operator[](std::string_view column_name) const {
    return at(column_name);
}

std::span<const Value> RowView::values() const {
    if (result_ == nullptr || row_index_ >= result_->rows_.size()) {
        throw std::out_of_range("row view is invalid");
    }
    return result_->rows_[row_index_];
}

class Database::Impl {
public:
    explicit Impl(ConnectionConfig config) : config_(std::move(config)) {
        if (config_.worker_count == 0) {
            config_.worker_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        }
        pool_ = std::make_unique<ConnectionPoolImpl>(config_);
        if (auto initialized = pool_->initialize(); !initialized) {
            init_error_ = initialized.error();
        }
        start_workers();
    }

    ~Impl() {
        stop();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] Expected<Result> query(std::string_view sql, std::vector<Value> values) {
        if (init_error_) {
            return std::unexpected(*init_error_);
        }
        auto lease = pool_->acquire();
        if (!lease) {
            return std::unexpected(lease.error());
        }

        if (values.empty()) {
            return (*lease)->query(sql);
        }

        auto statement = (*lease)->prepare(sql);
        if (!statement) {
            return std::unexpected(statement.error());
        }
        return statement->query(std::move(values));
    }

    [[nodiscard]] Expected<ExecuteResult> execute(std::string_view sql, std::vector<Value> values) {
        if (init_error_) {
            return std::unexpected(*init_error_);
        }
        auto lease = pool_->acquire();
        if (!lease) {
            return std::unexpected(lease.error());
        }

        if (values.empty()) {
            return (*lease)->execute(sql);
        }

        auto statement = (*lease)->prepare(sql);
        if (!statement) {
            return std::unexpected(statement.error());
        }
        return statement->execute(std::move(values));
    }

    [[nodiscard]] Expected<Transaction> begin_transaction();

    [[nodiscard]] Expected<std::string> escape(std::string_view value) {
        if (init_error_) {
            return std::unexpected(*init_error_);
        }
        auto lease = pool_->acquire();
        if (!lease) {
            return std::unexpected(lease.error());
        }
        return (*lease)->escape(value);
    }

    [[nodiscard]] PoolStats stats() const {
        return pool_->stats(queued_tasks_.load(std::memory_order_relaxed));
    }

    template <typename T>
    std::future<Expected<T>> submit(std::function<Expected<T>()> work) {
        auto promise = std::make_shared<std::promise<Expected<T>>>();
        auto future = promise->get_future();

        {
            std::lock_guard lock(task_mutex_);
            if (stopped_) {
                promise->set_value(std::unexpected(make_error(ErrorCode::async_stopped, Operation::async_submit,
                                                              "database async executor is stopped")));
                return future;
            }
            queued_tasks_.fetch_add(1, std::memory_order_relaxed);
            tasks_.push_back(Task{
        .run = [promise, work = std::move(work)](std::stop_token stop_token) mutable {
                    if (stop_token.stop_requested()) {
                        promise->set_value(std::unexpected(cancelled_error()));
                        return;
                    }
                    promise->set_value(work());
                },
                .cancel = [promise](DbError error) mutable {
                    promise->set_value(std::unexpected(std::move(error)));
                }
            });
        }
        task_cv_.notify_one();
        return future;
    }

    void stop() noexcept {
        {
            std::lock_guard lock(task_mutex_);
            if (stopped_) {
                return;
            }
            stopped_ = true;
            while (!tasks_.empty()) {
                auto task = std::move(tasks_.front());
                tasks_.pop_front();
                queued_tasks_.fetch_sub(1, std::memory_order_relaxed);
                if (task.cancel) {
                    task.cancel(cancelled_error());
                }
            }
        }
        task_cv_.notify_all();
        workers_.clear();
        if (pool_) {
            pool_->stop();
        }
    }

private:
    ConnectionConfig config_;
    std::unique_ptr<ConnectionPoolImpl> pool_;
    std::optional<DbError> init_error_;
    std::vector<std::jthread> workers_;
    std::deque<Task> tasks_;
    mutable std::mutex task_mutex_;
    std::condition_variable_any task_cv_;
    bool stopped_ = false;
    std::atomic_size_t queued_tasks_{0};

    void start_workers() {
        workers_.reserve(config_.worker_count);
        for (std::size_t index = 0; index < config_.worker_count; ++index) {
            workers_.emplace_back([this](std::stop_token stop_token) {
                while (!stop_token.stop_requested()) {
                    Task task;
                    {
                        std::unique_lock lock(task_mutex_);
                        task_cv_.wait(lock, stop_token, [this] { return stopped_ || !tasks_.empty(); });
                        if ((stopped_ && tasks_.empty()) || stop_token.stop_requested()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop_front();
                        queued_tasks_.fetch_sub(1, std::memory_order_relaxed);
                    }
                    if (task.run) {
                        task.run(stop_token);
                    }
                }
            });
        }
    }

    friend class Transaction::Impl;
};

class Transaction::Impl {
public:
    explicit Impl(ConnectionLease lease) noexcept : lease_(std::move(lease)) {}

    ~Impl() {
        if (active_) {
            (void)rollback();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] Expected<Result> query(std::string_view sql, std::vector<Value> values) {
        if (!active_ || !lease_) {
            return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::query,
                                             "transaction is not active"));
        }

        if (values.empty()) {
            return lease_->query(sql);
        }
        auto statement = lease_->prepare(sql);
        if (!statement) {
            return std::unexpected(statement.error());
        }
        return statement->query(std::move(values));
    }

    [[nodiscard]] Expected<ExecuteResult> execute(std::string_view sql, std::vector<Value> values) {
        if (!active_ || !lease_) {
            return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::execute,
                                             "transaction is not active"));
        }

        if (values.empty()) {
            return lease_->execute(sql);
        }
        auto statement = lease_->prepare(sql);
        if (!statement) {
            return std::unexpected(statement.error());
        }
        return statement->execute(std::move(values));
    }

    [[nodiscard]] Expected<void> commit() {
        if (!active_ || !lease_) {
            return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::commit,
                                             "transaction is not active"));
        }
        auto result = lease_->commit();
        if (!result) {
            return std::unexpected(result.error());
        }
        active_ = false;
        lease_.reset();
        return {};
    }

    [[nodiscard]] Expected<void> rollback() {
        if (!active_ || !lease_) {
            return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::rollback,
                                             "transaction is not active"));
        }
        auto result = lease_->rollback();
        active_ = false;
        lease_.reset();
        if (!result) {
            return std::unexpected(result.error());
        }
        return {};
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

private:
    ConnectionLease lease_;
    bool active_ = true;
};

Expected<Transaction> Database::Impl::begin_transaction() {
    if (init_error_) {
        return std::unexpected(*init_error_);
    }
    auto lease = pool_->acquire();
    if (!lease) {
        return std::unexpected(lease.error());
    }
    auto begun = (*lease)->begin_transaction();
    if (!begun) {
        return std::unexpected(begun.error());
    }
    return Transaction(std::make_unique<Transaction::Impl>(std::move(*lease)));
}

Database::Database(ConnectionConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

Database::~Database() = default;

Database::Database(Database&&) noexcept = default;

Database& Database::operator=(Database&&) noexcept = default;

Expected<Result> Database::query(std::string_view sql) {
    return impl_->query(sql, {});
}

Expected<ExecuteResult> Database::execute(std::string_view sql) {
    return impl_->execute(sql, {});
}

std::future<Expected<Result>> Database::query_async(std::string sql) {
    return submit_query_with_values(*this, std::move(sql), {});
}

std::future<Expected<ExecuteResult>> Database::execute_async(std::string sql) {
    return submit_execute_with_values(*this, std::move(sql), {});
}

Expected<Transaction> Database::begin_transaction() {
    return impl_->begin_transaction();
}

Expected<std::string> Database::escape(std::string_view value) {
    return impl_->escape(value);
}

PoolStats Database::stats() const {
    return impl_->stats();
}

Result Database::query_or_throw(std::string_view sql) {
    auto result = query(sql);
    if (!result) {
        throw DbException(std::move(result.error()));
    }
    return std::move(*result);
}

ExecuteResult Database::execute_or_throw(std::string_view sql) {
    auto result = execute(sql);
    if (!result) {
        throw DbException(std::move(result.error()));
    }
    return std::move(*result);
}

Transaction::Transaction() noexcept = default;

Transaction::~Transaction() = default;

Transaction::Transaction(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Transaction::Transaction(Transaction&&) noexcept = default;

Transaction& Transaction::operator=(Transaction&&) noexcept = default;

Expected<Result> Transaction::query(std::string_view sql) {
    if (!impl_) {
        return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::query,
                                         "transaction is not initialized"));
    }
    return impl_->query(sql, {});
}

Expected<ExecuteResult> Transaction::execute(std::string_view sql) {
    if (!impl_) {
        return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::execute,
                                         "transaction is not initialized"));
    }
    return impl_->execute(sql, {});
}

Expected<void> Transaction::commit() {
    if (!impl_) {
        return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::commit,
                                         "transaction is not initialized"));
    }
    return impl_->commit();
}

Expected<void> Transaction::rollback() {
    if (!impl_) {
        return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::rollback,
                                         "transaction is not initialized"));
    }
    return impl_->rollback();
}

bool Transaction::active() const noexcept {
    return impl_ != nullptr && impl_->active();
}

Expected<Result> query_with_values(Database& database, std::string_view sql, std::vector<Value> values) {
    return database.impl_->query(sql, std::move(values));
}

Expected<ExecuteResult> execute_with_values(Database& database, std::string_view sql, std::vector<Value> values) {
    return database.impl_->execute(sql, std::move(values));
}

std::future<Expected<Result>> submit_query_with_values(Database& database, std::string sql, std::vector<Value> values) {
    return database.impl_->submit<Result>([&database, sql = std::move(sql), values = std::move(values)]() mutable {
        return database.impl_->query(sql, std::move(values));
    });
}

std::future<Expected<ExecuteResult>> submit_execute_with_values(
    Database& database,
    std::string sql,
    std::vector<Value> values) {
    return database.impl_->submit<ExecuteResult>(
        [&database, sql = std::move(sql), values = std::move(values)]() mutable {
            return database.impl_->execute(sql, std::move(values));
        });
}

Expected<Result> transaction_query_with_values(Transaction& tx, std::string_view sql, std::vector<Value> values) {
    if (!tx.impl_) {
        return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::query,
                                         "transaction is not initialized"));
    }
    return tx.impl_->query(sql, std::move(values));
}

Expected<ExecuteResult> transaction_execute_with_values(Transaction& tx, std::string_view sql, std::vector<Value> values) {
    if (!tx.impl_) {
        return std::unexpected(make_error(ErrorCode::transaction_failed, Operation::execute,
                                         "transaction is not initialized"));
    }
    return tx.impl_->execute(sql, std::move(values));
}

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::ok: return "ok";
        case ErrorCode::mysql_init_failed: return "mysql_init_failed";
        case ErrorCode::connection_failed: return "connection_failed";
        case ErrorCode::connection_lost: return "connection_lost";
        case ErrorCode::statement_init_failed: return "statement_init_failed";
        case ErrorCode::statement_prepare_failed: return "statement_prepare_failed";
        case ErrorCode::bind_failed: return "bind_failed";
        case ErrorCode::execute_failed: return "execute_failed";
        case ErrorCode::result_metadata_failed: return "result_metadata_failed";
        case ErrorCode::result_bind_failed: return "result_bind_failed";
        case ErrorCode::result_fetch_failed: return "result_fetch_failed";
        case ErrorCode::result_truncated: return "result_truncated";
        case ErrorCode::pool_stopped: return "pool_stopped";
        case ErrorCode::pool_timeout: return "pool_timeout";
        case ErrorCode::async_stopped: return "async_stopped";
        case ErrorCode::async_cancelled: return "async_cancelled";
        case ErrorCode::invalid_argument: return "invalid_argument";
        case ErrorCode::type_mismatch: return "type_mismatch";
        case ErrorCode::transaction_failed: return "transaction_failed";
    }
    return "unknown";
}

std::string_view to_string(Operation operation) noexcept {
    switch (operation) {
        case Operation::unknown: return "unknown";
        case Operation::connect: return "connect";
        case Operation::ping: return "ping";
        case Operation::query: return "query";
        case Operation::execute: return "execute";
        case Operation::prepare: return "prepare";
        case Operation::bind: return "bind";
        case Operation::fetch: return "fetch";
        case Operation::begin_transaction: return "begin_transaction";
        case Operation::commit: return "commit";
        case Operation::rollback: return "rollback";
        case Operation::async_submit: return "async_submit";
        case Operation::async_cancelled: return "async_cancelled";
    }
    return "unknown";
}

} // namespace mysqlw
