#include "MySQLWrapper.h"
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <chrono>

namespace MySQLWrapper {

// PreparedStatement 实现
PreparedStatement::PreparedStatement(MYSQL_STMT* stmt, const std::string& query)
    : stmt_(stmt), query_(query) {
    if (!stmt_) {
        throw std::runtime_error("Invalid statement handle");
    }
    
    size_t paramCount = mysql_stmt_param_count(stmt_);
    params_.reserve(paramCount);
    paramBuffers_.reserve(paramCount);
}

PreparedStatement::~PreparedStatement() {
    if (stmt_) {
        mysql_stmt_close(stmt_);
    }
}

PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept
    : stmt_(other.stmt_), query_(std::move(other.query_)),
      params_(std::move(other.params_)), paramBuffers_(std::move(other.paramBuffers_)),
      currentParam_(other.currentParam_) {
    other.stmt_ = nullptr;
}

PreparedStatement& PreparedStatement::operator=(PreparedStatement&& other) noexcept {
    if (this != &other) {
        if (stmt_) {
            mysql_stmt_close(stmt_);
        }
        stmt_ = other.stmt_;
        query_ = std::move(other.query_);
        params_ = std::move(other.params_);
        paramBuffers_ = std::move(other.paramBuffers_);
        currentParam_ = other.currentParam_;
        other.stmt_ = nullptr;
    }
    return *this;
}

void PreparedStatement::ensureCapacity() {
    if (currentParam_ >= params_.size()) {
        params_.emplace_back();
        memset(&params_.back(), 0, sizeof(MYSQL_BIND));
    }
}

PreparedStatement& PreparedStatement::bind(const Value& value) {
    bindParam(value);
    return *this;
}

PreparedStatement& PreparedStatement::bind(int value) {
    ensureCapacity();
    auto& param = params_[currentParam_];
    param.buffer_type = MYSQL_TYPE_LONG;
    param.buffer = new int(value);
    paramBuffers_.emplace_back(reinterpret_cast<char*>(param.buffer));
    param.is_null = 0;
    currentParam_++;
    return *this;
}

PreparedStatement& PreparedStatement::bind(long long value) {
    ensureCapacity();
    auto& param = params_[currentParam_];
    param.buffer_type = MYSQL_TYPE_LONGLONG;
    param.buffer = new long long(value);
    paramBuffers_.emplace_back(reinterpret_cast<char*>(param.buffer));
    param.is_null = 0;
    currentParam_++;
    return *this;
}

PreparedStatement& PreparedStatement::bind(double value) {
    ensureCapacity();
    auto& param = params_[currentParam_];
    param.buffer_type = MYSQL_TYPE_DOUBLE;
    param.buffer = new double(value);
    paramBuffers_.emplace_back(reinterpret_cast<char*>(param.buffer));
    param.is_null = 0;
    currentParam_++;
    return *this;
}

PreparedStatement& PreparedStatement::bind(const std::string& value) {
    ensureCapacity();
    auto& param = params_[currentParam_];
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = const_cast<char*>(value.c_str());
    param.buffer_length = value.length();
    param.length = new unsigned long(value.length());
    paramBuffers_.emplace_back(reinterpret_cast<char*>(param.length));
    param.is_null = 0;
    currentParam_++;
    return *this;
}

PreparedStatement& PreparedStatement::bind(const char* value) {
    return bind(std::string(value));
}

PreparedStatement& PreparedStatement::bind(const std::vector<uint8_t>& value) {
    ensureCapacity();
    auto& param = params_[currentParam_];
    param.buffer_type = MYSQL_TYPE_BLOB;
    param.buffer = const_cast<uint8_t*>(value.data());
    param.buffer_length = value.size();
    param.length = new unsigned long(value.size());
    paramBuffers_.emplace_back(reinterpret_cast<char*>(param.length));
    param.is_null = 0;
    currentParam_++;
    return *this;
}

PreparedStatement& PreparedStatement::bindNull() {
    ensureCapacity();
    auto& param = params_[currentParam_];
    param.buffer_type = MYSQL_TYPE_NULL;
    param.is_null = new my_bool(1);
    paramBuffers_.emplace_back(reinterpret_cast<char*>(param.is_null));
    currentParam_++;
    return *this;
}

void PreparedStatement::bindParam(const Value& value) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            bindNull();
        } else if constexpr (std::is_same_v<T, int>) {
            bind(arg);
        } else if constexpr (std::is_same_v<T, long long>) {
            bind(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            bind(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            bind(arg);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            bind(arg);
        }
    }, value);
}

QueryResult PreparedStatement::execute() {
    if (!params_.empty()) {
        if (mysql_stmt_bind_param(stmt_, params_.data()) != 0) {
            throw std::runtime_error("Failed to bind parameters: " + 
                std::string(mysql_stmt_error(stmt_)));
        }
    }
    
    if (mysql_stmt_execute(stmt_) != 0) {
        throw std::runtime_error("Failed to execute statement: " + 
            std::string(mysql_stmt_error(stmt_)));
    }
    
    // 检查是否是SELECT查询
    MYSQL_RES* metaResult = mysql_stmt_result_metadata(stmt_);
    if (metaResult) {
        // 是SELECT查询，获取结果
        mysql_free_result(metaResult);
        return executeQuery();
    } else {
        // 是INSERT/UPDATE/DELETE
        uint64_t affected = mysql_stmt_affected_rows(stmt_);
        uint64_t insertId = mysql_stmt_insert_id(stmt_);
        return QueryResult(ResultSet{}, affected, insertId);
    }
}

QueryResult PreparedStatement::executeQuery() {
    if (mysql_stmt_store_result(stmt_) != 0) {
        throw std::runtime_error("Failed to store result: " + 
            std::string(mysql_stmt_error(stmt_)));
    }
    
    MYSQL_RES* metaResult = mysql_stmt_result_metadata(stmt_);
    if (!metaResult) {
        return QueryResult();
    }
    
    unsigned int numFields = mysql_num_fields(metaResult);
    MYSQL_FIELD* fields = mysql_fetch_fields(metaResult);
    
    // 准备结果绑定
    std::vector<MYSQL_BIND> resultBinds(numFields);
    std::vector<std::unique_ptr<char[]>> buffers(numFields);
    std::vector<unsigned long> lengths(numFields);
    std::vector<my_bool> isNulls(numFields);
    
    for (unsigned int i = 0; i < numFields; ++i) {
        memset(&resultBinds[i], 0, sizeof(MYSQL_BIND));
        buffers[i] = std::make_unique<char[]>(fields[i].max_length + 1);
        
        resultBinds[i].buffer_type = fields[i].type;
        resultBinds[i].buffer = buffers[i].get();
        resultBinds[i].buffer_length = fields[i].max_length + 1;
        resultBinds[i].length = &lengths[i];
        resultBinds[i].is_null = &isNulls[i];
    }
    
    if (mysql_stmt_bind_result(stmt_, resultBinds.data()) != 0) {
        mysql_free_result(metaResult);
        throw std::runtime_error("Failed to bind result: " + 
            std::string(mysql_stmt_error(stmt_)));
    }
    
    ResultSet results;
    while (mysql_stmt_fetch(stmt_) == 0) {
        Row row;
        for (unsigned int i = 0; i < numFields; ++i) {
            if (isNulls[i]) {
                row[fields[i].name] = nullptr;
            } else {
                switch (fields[i].type) {
                    case MYSQL_TYPE_LONG:
                        row[fields[i].name] = *reinterpret_cast<int*>(buffers[i].get());
                        break;
                    case MYSQL_TYPE_LONGLONG:
                        row[fields[i].name] = *reinterpret_cast<long long*>(buffers[i].get());
                        break;
                    case MYSQL_TYPE_FLOAT:
                    case MYSQL_TYPE_DOUBLE:
                        row[fields[i].name] = *reinterpret_cast<double*>(buffers[i].get());
                        break;
                    default:
                        row[fields[i].name] = std::string(buffers[i].get(), lengths[i]);
                        break;
                }
            }
        }
        results.push_back(std::move(row));
    }
    
    mysql_free_result(metaResult);
    return QueryResult(std::move(results));
}

uint64_t PreparedStatement::executeUpdate() {
    if (!params_.empty()) {
        if (mysql_stmt_bind_param(stmt_, params_.data()) != 0) {
            throw std::runtime_error("Failed to bind parameters: " + 
                std::string(mysql_stmt_error(stmt_)));
        }
    }
    
    if (mysql_stmt_execute(stmt_) != 0) {
        throw std::runtime_error("Failed to execute statement: " + 
            std::string(mysql_stmt_error(stmt_)));
    }
    
    return mysql_stmt_affected_rows(stmt_);
}

void PreparedStatement::reset() {
    mysql_stmt_reset(stmt_);
    params_.clear();
    paramBuffers_.clear();
    currentParam_ = 0;
}

// Connection 实现
Connection::Connection(const ConnectionConfig& config) : config_(config) {
    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
        throw std::runtime_error("Failed to initialize MySQL connection");
    }
    
    // 设置连接选项
    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, config_.charset.c_str());
    mysql_options(mysql_, MYSQL_OPT_RECONNECT, &config_.autoReconnect);
    
    unsigned int timeout = config_.connectionTimeout;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
}

Connection::~Connection() {
    disconnect();
}

Connection::Connection(Connection&& other) noexcept
    : mysql_(other.mysql_), config_(std::move(other.config_)),
      inTransaction_(other.inTransaction_) {
    other.mysql_ = nullptr;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        disconnect();
        mysql_ = other.mysql_;
        config_ = std::move(other.config_);
        inTransaction_ = other.inTransaction_;
        other.mysql_ = nullptr;
    }
    return *this;
}

bool Connection::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!mysql_real_connect(mysql_, config_.host.c_str(), config_.user.c_str(),
                           config_.password.c_str(), config_.database.c_str(),
                           config_.port, nullptr, 0)) {
        return false;
    }
    
    // 设置字符集
    mysql_set_character_set(mysql_, config_.charset.c_str());
    
    return true;
}

void Connection::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mysql_) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

bool Connection::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mysql_ && mysql_ping(mysql_) == 0;
}

bool Connection::ping() {
    std::lock_guard<std::mutex> lock(mutex_);
    return mysql_ && mysql_ping(mysql_) == 0;
}

QueryResult Connection::query(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (mysql_query(mysql_, sql.c_str()) != 0) {
        throw std::runtime_error("Query failed: " + getError());
    }
    
    MYSQL_RES* result = mysql_store_result(mysql_);
    if (!result) {
        if (mysql_field_count(mysql_) == 0) {
            // 不是SELECT查询
            uint64_t affected = mysql_affected_rows(mysql_);
            uint64_t insertId = mysql_insert_id(mysql_);
            return QueryResult(ResultSet{}, affected, insertId);
        } else {
            throw std::runtime_error("Failed to store result: " + getError());
        }
    }
    
    ResultSet rows;
    unsigned int numFields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    
    MYSQL_ROW mysqlRow;
    while ((mysqlRow = mysql_fetch_row(result))) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        Row row;
        
        for (unsigned int i = 0; i < numFields; ++i) {
            if (mysqlRow[i] == nullptr) {
                row[fields[i].name] = nullptr;
            } else {
                switch (fields[i].type) {
                    case MYSQL_TYPE_TINY:
                    case MYSQL_TYPE_SHORT:
                    case MYSQL_TYPE_LONG:
                    case MYSQL_TYPE_INT24:
                        row[fields[i].name] = std::stoi(mysqlRow[i]);
                        break;
                    case MYSQL_TYPE_LONGLONG:
                        row[fields[i].name] = std::stoll(mysqlRow[i]);
                        break;
                    case MYSQL_TYPE_FLOAT:
                    case MYSQL_TYPE_DOUBLE:
                    case MYSQL_TYPE_DECIMAL:
                        row[fields[i].name] = std::stod(mysqlRow[i]);
                        break;
                    default:
                        row[fields[i].name] = std::string(mysqlRow[i], lengths[i]);
                        break;
                }
            }
        }
        rows.push_back(std::move(row));
    }
    
    mysql_free_result(result);
    return QueryResult(std::move(rows));
}

uint64_t Connection::execute(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (mysql_query(mysql_, sql.c_str()) != 0) {
        throw std::runtime_error("Execute failed: " + getError());
    }
    
    return mysql_affected_rows(mysql_);
}

PreparedStatement Connection::prepare(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    MYSQL_STMT* stmt = mysql_stmt_init(mysql_);
    if (!stmt) {
        throw std::runtime_error("Failed to initialize statement");
    }
    
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        std::string error = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        throw std::runtime_error("Failed to prepare statement: " + error);
    }
    
    return PreparedStatement(stmt, sql);
}

void Connection::beginTransaction() {
    execute("START TRANSACTION");
    inTransaction_ = true;
}

void Connection::commit() {
    execute("COMMIT");
    inTransaction_ = false;
}

void Connection::rollback() {
    execute("ROLLBACK");
    inTransaction_ = false;
}

std::string Connection::escape(const std::string& str) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<char> buffer(str.length() * 2 + 1);
    unsigned long len = mysql_real_escape_string(mysql_, buffer.data(), 
                                                 str.c_str(), str.length());
    return std::string(buffer.data(), len);
}

std::string Connection::getError() const {
    return mysql_ ? mysql_error(mysql_) : "No connection";
}

int Connection::getErrorCode() const {
    return mysql_ ? mysql_errno(mysql_) : 0;
}

bool Connection::reconnect() {
    disconnect();
    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
        return false;
    }
    
    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, config_.charset.c_str());
    mysql_options(mysql_, MYSQL_OPT_RECONNECT, &config_.autoReconnect);
    
    unsigned int timeout = config_.connectionTimeout;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    
    return connect();
}

// ConnectionPool 实现
ConnectionPool::ConnectionPool(const ConnectionConfig& config) : config_(config) {
    initialize();
}

ConnectionPool::~ConnectionPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}

void ConnectionPool::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (int i = 0; i < config_.poolSize; ++i) {
        auto conn = createConnection();
        if (conn && conn->connect()) {
            pool_.push(conn);
        }
    }
}

std::shared_ptr<Connection> ConnectionPool::createConnection() {
    return std::make_shared<Connection>(config_);
}

std::shared_ptr<Connection> ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    cv_.wait(lock, [this] { return !pool_.empty() || stopped_; });
    
    if (stopped_) {
        throw std::runtime_error("Connection pool is stopped");
    }
    
    auto conn = pool_.front();
    pool_.pop();
    
    // 检查连接是否有效
    if (!conn->ping()) {
        conn = createConnection();
        if (!conn->connect()) {
            throw std::runtime_error("Failed to create new connection");
        }
    }
    
    return conn;
}

void ConnectionPool::release(std::shared_ptr<Connection> conn) {
    if (!conn) return;
    
    // 如果连接在事务中，先回滚
    if (conn->inTransaction()) {
        try {
            conn->rollback();
        } catch (...) {}
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (pool_.size() < static_cast<size_t>(config_.maxPoolSize)) {
        pool_.push(conn);
        cv_.notify_one();
    }
}

size_t ConnectionPool::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size();
}

size_t ConnectionPool::available() const {
    return size();
}

// Database 实现
Database::Database(const ConnectionConfig& config) 
    : pool_(std::make_unique<ConnectionPool>(config)) {
    
    // 启动工作线程
    int numWorkers = std::thread::hardware_concurrency();
    for (int i = 0; i < numWorkers; ++i) {
        workers_.emplace_back(&Database::workerThread, this);
    }
}

Database::~Database() {
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        stopped_ = true;
    }
    taskCv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void Database::workerThread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(taskMutex_);
            taskCv_.wait(lock, [this] { return stopped_ || !tasks_.empty(); });
            
            if (stopped_ && tasks_.empty()) {
                break;
            }
            
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        
        if (task) {
            task();
        }
    }
}

QueryResult Database::query(const std::string& sql) {
    auto conn = pool_->acquire();
    try {
        auto result = conn->query(sql);
        pool_->release(conn);
        return result;
    } catch (...) {
        pool_->release(conn);
        throw;
    }
}

std::future<QueryResult> Database::queryAsync(const std::string& sql) {
    auto promise = std::make_shared<std::promise<QueryResult>>();
    auto future = promise->get_future();
    
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        tasks_.push([this, sql, promise]() {
            try {
                promise->set_value(query(sql));
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }
    taskCv_.notify_one();
    
    return future;
}

uint64_t Database::execute(const std::string& sql) {
    auto conn = pool_->acquire();
    try {
        auto result = conn->execute(sql);
        pool_->release(conn);
        return result;
    } catch (...) {
        pool_->release(conn);
        throw;
    }
}

std::string Database::escape(const std::string& str) {
    auto conn = pool_->acquire();
    try {
        auto result = conn->escape(str);
        pool_->release(conn);
        return result;
    } catch (...) {
        pool_->release(conn);
        throw;
    }
}

std::unique_ptr<Database::Transaction> Database::beginTransaction() {
    auto conn = pool_->acquire();
    conn->beginTransaction();
    return std::make_unique<Transaction>(this, conn);
}

std::string Database::join(const std::vector<std::string>& strings, const std::string& delimiter) {
    if (strings.empty()) return "";
    
    std::ostringstream oss;
    for (size_t i = 0; i < strings.size(); ++i) {
        oss << strings[i];
        if (i < strings.size() - 1) {
            oss << delimiter;
        }
    }
    return oss.str();
}

// Transaction 实现
Database::Transaction::Transaction(Database* db, std::shared_ptr<Connection> conn)
    : db_(db), conn_(conn) {}

Database::Transaction::~Transaction() {
    if (!committed_) {
        try {
            conn_->rollback();
        } catch (...) {}
    }
    db_->pool_->release(conn_);
}

QueryResult Database::Transaction::query(const std::string& sql) {
    return conn_->query(sql);
}

uint64_t Database::Transaction::execute(const std::string& sql) {
    return conn_->execute(sql);
}

void Database::Transaction::commit() {
    conn_->commit();
    committed_ = true;
}

void Database::Transaction::rollback() {
    conn_->rollback();
    committed_ = true;
}

} // namespace MySQLWrapper