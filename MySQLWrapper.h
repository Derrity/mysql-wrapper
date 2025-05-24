#ifndef MYSQL_WRAPPER_H
#define MYSQL_WRAPPER_H

#include <mysql/mysql.h>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <functional>
#include <unordered_map>
#include <variant>
#include <optional>
#include <future>

namespace MySQLWrapper {

// 数据类型定义
using Value = std::variant<std::nullptr_t, int, long long, double, std::string, std::vector<uint8_t>>;
using Row = std::unordered_map<std::string, Value>;
using ResultSet = std::vector<Row>;

// 连接配置
struct ConnectionConfig {
    std::string host = "localhost";
    int port = 3306;
    std::string user;
    std::string password;
    std::string database;
    std::string charset = "utf8mb4";
    int poolSize = 10;
    int maxPoolSize = 50;
    int connectionTimeout = 10;
    bool autoReconnect = true;
};

// 查询结果
class QueryResult {
public:
    QueryResult() = default;
    QueryResult(ResultSet&& data, uint64_t affected = 0, uint64_t insertId = 0)
        : data_(std::move(data)), affectedRows_(affected), lastInsertId_(insertId) {}

    const ResultSet& rows() const { return data_; }
    uint64_t affectedRows() const { return affectedRows_; }
    uint64_t lastInsertId() const { return lastInsertId_; }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    const Row& operator[](size_t index) const { return data_[index]; }

private:
    ResultSet data_;
    uint64_t affectedRows_ = 0;
    uint64_t lastInsertId_ = 0;
};

// 预处理语句包装器
class PreparedStatement {
public:
    PreparedStatement(MYSQL_STMT* stmt, const std::string& query);
    ~PreparedStatement();

    PreparedStatement(const PreparedStatement&) = delete;
    PreparedStatement& operator=(const PreparedStatement&) = delete;
    PreparedStatement(PreparedStatement&& other) noexcept;
    PreparedStatement& operator=(PreparedStatement&& other) noexcept;

    // 绑定参数
    PreparedStatement& bind(const Value& value);
    PreparedStatement& bind(int value);
    PreparedStatement& bind(long long value);
    PreparedStatement& bind(double value);
    PreparedStatement& bind(const std::string& value);
    PreparedStatement& bind(const char* value);
    PreparedStatement& bind(const std::vector<uint8_t>& value);
    PreparedStatement& bindNull();

    // 执行查询
    QueryResult execute();
    QueryResult executeQuery();
    uint64_t executeUpdate();

    // 重置语句
    void reset();

private:
    MYSQL_STMT* stmt_;
    std::string query_;
    std::vector<MYSQL_BIND> params_;
    std::vector<std::unique_ptr<char[]>> paramBuffers_;
    size_t currentParam_ = 0;

    void ensureCapacity();
    void bindParam(const Value& value);
};

// 数据库连接
class Connection {
public:
    Connection(const ConnectionConfig& config);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    bool connect();
    void disconnect();
    bool isConnected() const;
    bool ping();

    // 执行查询
    QueryResult query(const std::string& sql);
    uint64_t execute(const std::string& sql);

    // 预处理语句
    PreparedStatement prepare(const std::string& sql);

    // 事务
    void beginTransaction();
    void commit();
    void rollback();
    bool inTransaction() const { return inTransaction_; }

    // 转义字符串（防SQL注入）
    std::string escape(const std::string& str);

    // 获取错误信息
    std::string getError() const;
    int getErrorCode() const;

private:
    MYSQL* mysql_;
    ConnectionConfig config_;
    bool inTransaction_ = false;
    mutable std::mutex mutex_;

    bool reconnect();
};

// 连接池
class ConnectionPool {
public:
    ConnectionPool(const ConnectionConfig& config);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    std::shared_ptr<Connection> acquire();
    void release(std::shared_ptr<Connection> conn);

    size_t size() const;
    size_t available() const;

private:
    ConnectionConfig config_;
    std::queue<std::shared_ptr<Connection>> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;

    void initialize();
    std::shared_ptr<Connection> createConnection();
};

// 主数据库类
class Database {
public:
    explicit Database(const ConnectionConfig& config);
    ~Database();

    // 查询操作
    QueryResult query(const std::string& sql);
    std::future<QueryResult> queryAsync(const std::string& sql);

    // 预处理查询
    template<typename... Args>
    QueryResult query(const std::string& sql, Args&&... args) {
        auto conn = pool_->acquire();
        auto stmt = conn->prepare(sql);
        bindArgs(stmt, std::forward<Args>(args)...);
        auto result = stmt.execute();
        pool_->release(conn);
        return result;
    }

    // 执行语句（INSERT/UPDATE/DELETE）
    uint64_t execute(const std::string& sql);

    template<typename... Args>
    uint64_t execute(const std::string& sql, Args&&... args) {
        auto conn = pool_->acquire();
        auto stmt = conn->prepare(sql);
        bindArgs(stmt, std::forward<Args>(args)...);
        auto result = stmt.executeUpdate();
        pool_->release(conn);
        return result;
    }

    // 事务
    class Transaction {
    public:
        Transaction(Database* db, std::shared_ptr<Connection> conn);
        ~Transaction();

        QueryResult query(const std::string& sql);
        uint64_t execute(const std::string& sql);

        template<typename... Args>
        QueryResult query(const std::string& sql, Args&&... args) {
            auto stmt = conn_->prepare(sql);
            db_->bindArgs(stmt, std::forward<Args>(args)...);
            return stmt.execute();
        }

        template<typename... Args>
        uint64_t execute(const std::string& sql, Args&&... args) {
            auto stmt = conn_->prepare(sql);
            db_->bindArgs(stmt, std::forward<Args>(args)...);
            return stmt.executeUpdate();
        }

        void commit();
        void rollback();

    private:
        Database* db_;
        std::shared_ptr<Connection> conn_;
        bool committed_ = false;
    };

    std::unique_ptr<Transaction> beginTransaction();

    // 工具方法
    std::string escape(const std::string& str);

    // 批量操作
    template<typename Container>
    uint64_t batchInsert(const std::string& table, const std::vector<std::string>& columns,
                         const Container& data) {
        if (data.empty()) return 0;

        std::string sql = "INSERT INTO " + table + " (";
        for (size_t i = 0; i < columns.size(); ++i) {
            sql += columns[i];
            if (i < columns.size() - 1) sql += ", ";
        }
        sql += ") VALUES ";

        std::vector<std::string> placeholders;
        for (size_t i = 0; i < data.size(); ++i) {
            std::string placeholder = "(";
            for (size_t j = 0; j < columns.size(); ++j) {
                placeholder += "?";
                if (j < columns.size() - 1) placeholder += ", ";
            }
            placeholder += ")";
            placeholders.push_back(placeholder);
        }

        sql += join(placeholders, ", ");

        auto conn = pool_->acquire();
        auto stmt = conn->prepare(sql);

        for (const auto& row : data) {
            for (const auto& value : row) {
                stmt.bind(value);
            }
        }

        auto result = stmt.executeUpdate();
        pool_->release(conn);
        return result;
    }

private:
    std::unique_ptr<ConnectionPool> pool_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex taskMutex_;
    std::condition_variable taskCv_;
    bool stopped_ = false;

    void workerThread();

    template<typename... Args>
    void bindArgs(PreparedStatement& stmt, Args&&... args) {
        (stmt.bind(std::forward<Args>(args)), ...);
    }

    std::string join(const std::vector<std::string>& strings, const std::string& delimiter);
};

// 便捷函数
template<typename T>
T get(const Value& value) {
    if (auto* v = std::get_if<T>(&value)) {
        return *v;
    }
    throw std::runtime_error("Type mismatch in get()");
}

template<typename T>
std::optional<T> getOpt(const Value& value) {
    if (auto* v = std::get_if<T>(&value)) {
        return *v;
    }
    return std::nullopt;
}

} // namespace MySQLWrapper

#endif // MYSQL_WRAPPER_H