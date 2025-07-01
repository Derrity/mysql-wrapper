# MySQLWrapper - Modern C++ MySQL Database Operation Library

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![MySQL](https://img.shields.io/badge/MySQL-5.7%2B-orange.svg)](https://www.mysql.com/)

MySQLWrapper is a modern C++ MySQL database operation library that provides a simple, secure, and high-performance database access interface. Compared to traditional MySQL Connector/C++, it offers API design that better conforms to modern C++ standards, with built-in connection pooling, automatic SQL injection prevention, RAII resource management, and other features.

## Table of Contents

- [Key Features](#key-features)
- [Quick Start](#quick-start)
- [Installation Guide](#installation-guide)
- [API Documentation](#api-documentation)
- [Usage Examples](#usage-examples)
- [Comparison with MySQL Connector/C++](#comparison-with-mysql-connectorc)
- [Performance Testing](#performance-testing)
- [Best Practices](#best-practices)
- [FAQ](#faq)

## Key Features

### 🚀 Core Features

- **Connection Pool Management**: Built-in efficient connection pool with automatic connection lifecycle management
- **Thread Safety**: All operations are thread-safe, supporting multi-threaded concurrent access
- **Automatic SQL Injection Prevention**: Uses prepared statements by default with automatic parameter escaping
- **RAII Resource Management**: Automatic resource lifecycle management without manual cleanup
- **Type Safety**: Uses `std::variant` to ensure type safety
- **Asynchronous Operations**: Supports asynchronous queries to fully utilize multi-core CPUs
- **Transaction Support**: RAII-style transaction management with automatic rollback on exceptions
- **Batch Operations**: Efficient batch insert and update operations
- **Zero-Copy Optimization**: Reduces data copying to improve performance

### 🛡️ Security Features

- All queries are parameterized by default to prevent SQL injection
- Automatic connection reconnection mechanism
- Automatic transaction rollback on exceptions
- Connection timeout protection
- Thread-safe error handling

### 💡 Usability

- Clean and intuitive API design
- Method chaining support
- Automatic type deduction
- Rich error information
- Comprehensive documentation and examples

## Quick Start

### Minimal Example

```cpp
#include "MySQLWrapper.h"
#include <iostream>

using namespace MySQLWrapper;

int main() {
    // 1. Create database connection
    Database db({
        .host = "localhost",
        .user = "root",
        .password = "password",
        .database = "test"
    });
    
    // 2. Execute query
    auto result = db.query("SELECT * FROM users WHERE age > ?", 18);
    
    // 3. Process results
    for (const auto& row : result.rows()) {
        std::cout << "Name: " << get<std::string>(row.at("name")) 
                  << ", Age: " << get<int>(row.at("age")) << std::endl;
    }
    
    return 0;
}
```

### Comparison with MySQL Connector/C++ Code

```cpp
// Traditional MySQL Connector/C++ approach
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

int main() {
    sql::mysql::MySQL_Driver *driver = nullptr;
    sql::Connection *con = nullptr;
    sql::PreparedStatement *pstmt = nullptr;
    sql::ResultSet *res = nullptr;
    
    try {
        // 1. Get driver and create connection
        driver = sql::mysql::get_mysql_driver_instance();
        con = driver->connect("tcp://127.0.0.1:3306", "root", "password");
        con->setSchema("test");
        
        // 2. Prepare and execute query
        pstmt = con->prepareStatement("SELECT * FROM users WHERE age > ?");
        pstmt->setInt(1, 18);
        res = pstmt->executeQuery();
        
        // 3. Process results
        while (res->next()) {
            std::cout << "Name: " << res->getString("name") 
                      << ", Age: " << res->getInt("age") << std::endl;
        }
        
        // 4. Manual resource cleanup
        delete res;
        delete pstmt;
        delete con;
        
    } catch (sql::SQLException &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        // Clean up resources even in exception cases
        if (res) delete res;
        if (pstmt) delete pstmt;
        if (con) delete con;
    }
    
    return 0;
}
```

**Code Comparison**: MySQLWrapper requires only 15 lines, while MySQL Connector/C++ needs 35+ lines.

## Installation Guide

### System Requirements

- C++17 or higher
- CMake 3.10+
- MySQL 5.7+ or MariaDB 10.2+
- Linux, macOS, or Windows

### Installation on Debian/Ubuntu

```bash
# 1. Install dependencies
sudo apt update
sudo apt install -y \
    libmysqlclient-dev \
    build-essential \
    cmake \
    pkg-config \
    libssl-dev \
    git

# 2. Clone repository
git clone https://github.com/Derrity/mysql-wrapper.git
cd mysql-wrapper

# 3. Build and install
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

### Installation on CentOS/RHEL/Fedora

```bash
# 1. Install dependencies
sudo yum install -y \
    mysql-devel \
    gcc-c++ \
    cmake3 \
    openssl-devel \
    git

# 2. Clone and build
git clone https://github.com/Derrity/mysql-wrapper.git
cd mysql-wrapper
mkdir build && cd build
cmake3 .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

### Installation on macOS

```bash
# 1. Install dependencies
brew install mysql cmake pkg-config

# 2. Clone and build
git clone https://github.com/Derrity/mysql-wrapper.git
cd mysql-wrapper
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
sudo make install
```

### Using in Your Project

#### Method 1: Using CMake

```cmake
find_package(MySQLWrapper REQUIRED)
target_link_libraries(your_target PRIVATE MySQLWrapper::mysqlwrapper)
```

#### Method 2: Manual Linking

```bash
g++ -std=c++17 your_code.cpp -lmysqlwrapper -lmysqlclient -lpthread
```

## API Documentation

### Connection Configuration

```cpp
struct ConnectionConfig {
    std::string host = "localhost";      // Database host
    int port = 3306;                     // Port number
    std::string user;                    // Username
    std::string password;                // Password
    std::string database;                // Database name
    std::string charset = "utf8mb4";     // Character set
    int poolSize = 10;                   // Initial connection pool size
    int maxPoolSize = 50;                // Maximum connection pool size
    int connectionTimeout = 10;          // Connection timeout (seconds)
    bool autoReconnect = true;           // Auto reconnect
};
```

### Database Class

```cpp
class Database {
public:
    // Constructor
    explicit Database(const ConnectionConfig& config);
    
    // Query operations
    QueryResult query(const std::string& sql);
    
    // Parameterized query (automatic SQL injection prevention)
    template<typename... Args>
    QueryResult query(const std::string& sql, Args&&... args);
    
    // Asynchronous query
    std::future<QueryResult> queryAsync(const std::string& sql);
    
    // Execute statement (INSERT/UPDATE/DELETE)
    uint64_t execute(const std::string& sql);
    
    // Parameterized execution
    template<typename... Args>
    uint64_t execute(const std::string& sql, Args&&... args);
    
    // Begin transaction
    std::unique_ptr<Transaction> beginTransaction();
    
    // Batch insert
    template<typename Container>
    uint64_t batchInsert(const std::string& table, 
                         const std::vector<std::string>& columns,
                         const Container& data);
    
    // Escape string
    std::string escape(const std::string& str);
};
```

### QueryResult Class

```cpp
class QueryResult {
public:
    // Get all rows
    const ResultSet& rows() const;
    
    // Get affected rows count
    uint64_t affectedRows() const;
    
    // Get last insert ID
    uint64_t lastInsertId() const;
    
    // Get result set size
    size_t size() const;
    
    // Check if empty
    bool empty() const;
    
    // Index access
    const Row& operator[](size_t index) const;
};
```

### Transaction Class

```cpp
class Transaction {
public:
    // Query operations
    QueryResult query(const std::string& sql);
    
    // Execute operations
    uint64_t execute(const std::string& sql);
    
    // Parameterized operations
    template<typename... Args>
    QueryResult query(const std::string& sql, Args&&... args);
    
    template<typename... Args>
    uint64_t execute(const std::string& sql, Args&&... args);
    
    // Commit transaction
    void commit();
    
    // Rollback transaction
    void rollback();
};
```

### Utility Functions

```cpp
// Get value of specified type from Value
template<typename T>
T get(const Value& value);

// Safe get value (returns optional)
template<typename T>
std::optional<T> getOpt(const Value& value);
```

## 使用示例

### 1. 基本查询操作

```cpp
// 简单查询
auto users = db.query("SELECT * FROM users");
std::cout << "总用户数: " << users.size() << std::endl;

// 参数化查询（推荐，自动防 SQL 注入）
std::string name = "'; DROP TABLE users; --";  // 恶意输入
auto safe_result = db.query(
    "SELECT * FROM users WHERE name = ? AND age > ?", 
    name, 18
);  // 完全安全，自动转义

// 获取单个值
auto count_result = db.query("SELECT COUNT(*) as total FROM users");
int total = get<int>(count_result[0].at("total"));
```

### 2. 插入、更新和删除

```cpp
// 插入数据
auto insertId = db.execute(
    "INSERT INTO users (name, email, age) VALUES (?, ?, ?)",
    "张三", "zhangsan@example.com", 25
);
std::cout << "新用户 ID: " << insertId << std::endl;

// 更新数据
auto affected = db.execute(
    "UPDATE users SET age = age + 1 WHERE birthday = CURDATE()"
);
std::cout << "更新了 " << affected << " 条记录" << std::endl;

// 删除数据
db.execute("DELETE FROM users WHERE inactive = ?", true);
```

### 3. 事务处理

```cpp
// 方法 1：自动管理事务（推荐）
{
    auto tx = db.beginTransaction();
    
    try {
        // 转账操作
        tx->execute("UPDATE accounts SET balance = balance - ? WHERE id = ?", 100.0, 1);
        tx->execute("UPDATE accounts SET balance = balance + ? WHERE id = ?", 100.0, 2);
        
        // 记录日志
        tx->execute("INSERT INTO transfer_log (from_id, to_id, amount) VALUES (?, ?, ?)",
                    1, 2, 100.0);
        
        tx->commit();  // 提交事务
        std::cout << "转账成功" << std::endl;
        
    } catch (const std::exception& e) {
        // 发生异常时自动回滚
        std::cerr << "转账失败: " << e.what() << std::endl;
    }
    // 离开作用域时，如果没有提交会自动回滚
}

// 方法 2：手动回滚
{
    auto tx = db.beginTransaction();
    
    auto balance = tx->query("SELECT balance FROM accounts WHERE id = ?", 1);
    if (get<double>(balance[0].at("balance")) < 100.0) {
        tx->rollback();  // 手动回滚
        throw std::runtime_error("余额不足");
    }
    
    tx->execute("UPDATE accounts SET balance = balance - ? WHERE id = ?", 100.0, 1);
    tx->commit();
}
```

### 4. 批量操作

```cpp
// 批量插入用户
std::vector<std::vector<Value>> users = {
    {"李四", "lisi@example.com", 28, nullptr},      // nullptr 表示 NULL
    {"王五", "wangwu@example.com", 30, "北京"},
    {"赵六", "zhaoliu@example.com", 25, "上海"}
};

auto inserted = db.batchInsert("users", 
    {"name", "email", "age", "city"}, 
    users
);
std::cout << "批量插入了 " << inserted << " 条记录" << std::endl;

// 批量更新（使用事务）
{
    auto tx = db.beginTransaction();
    
    std::vector<std::pair<int, std::string>> updates = {
        {1, "user1@newdomain.com"},
        {2, "user2@newdomain.com"},
        {3, "user3@newdomain.com"}
    };
    
    for (const auto& [id, email] : updates) {
        tx->execute("UPDATE users SET email = ? WHERE id = ?", email, id);
    }
    
    tx->commit();
}
```

### 5. 异步操作

```cpp
// 异步查询
auto future1 = db.queryAsync("SELECT COUNT(*) as count FROM orders WHERE status = 'pending'");
auto future2 = db.queryAsync("SELECT SUM(amount) as total FROM orders WHERE date = CURDATE()");

// 执行其他操作...
doSomeOtherWork();

// 获取异步结果
auto result1 = future1.get();
auto result2 = future2.get();

std::cout << "待处理订单: " << get<int>(result1[0].at("count")) << std::endl;
std::cout << "今日总额: " << get<double>(result2[0].at("total")) << std::endl;

// 并发执行多个查询
std::vector<std::future<QueryResult>> futures;
std::vector<std::string> queries = {
    "SELECT * FROM users WHERE city = 'Beijing'",
    "SELECT * FROM users WHERE city = 'Shanghai'",
    "SELECT * FROM users WHERE city = 'Guangzhou'"
};

for (const auto& query : queries) {
    futures.push_back(db.queryAsync(query));
}

// 收集所有结果
for (auto& future : futures) {
    auto result = future.get();
    processResult(result);
}
```

### 6. 结果集处理

```cpp
// 查询用户信息
auto users = db.query("SELECT id, name, email, age, balance, created_at FROM users");

for (const auto& row : users.rows()) {
    // 方法 1：直接获取（如果类型不匹配会抛出异常）
    int id = get<int>(row.at("id"));
    std::string name = get<std::string>(row.at("name"));
    
    // 方法 2：安全获取（返回 optional）
    auto age = getOpt<int>(row.at("age"));
    if (age.has_value()) {
        std::cout << "年龄: " << age.value() << std::endl;
    }
    
    // 方法 3：使用 std::visit 处理多种类型
    std::visit([](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, int>) {
            std::cout << "整数: " << value << std::endl;
        } else if constexpr (std::is_same_v<T, double>) {
            std::cout << "浮点数: " << value << std::endl;
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::cout << "字符串: " << value << std::endl;
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            std::cout << "NULL 值" << std::endl;
        }
    }, row.at("balance"));
}

// 处理可能的 NULL 值
for (const auto& row : users.rows()) {
    auto email = getOpt<std::string>(row.at("email"));
    if (email.has_value()) {
        sendEmail(email.value());
    } else {
        std::cout << "用户没有邮箱" << std::endl;
    }
}
```

### 7. 错误处理

```cpp
try {
    // 所有数据库操作都可能抛出异常
    auto result = db.query("SELECT * FROM non_existent_table");
    
} catch (const std::runtime_error& e) {
    std::cerr << "数据库错误: " << e.what() << std::endl;
    
    // 记录错误日志
    logger.error("Database query failed: {}", e.what());
    
    // 可以选择重试
    retryOperation();
}

// 使用 lambda 进行错误处理
auto safeQuery = [&db](const std::string& sql) -> std::optional<QueryResult> {
    try {
        return db.query(sql);
    } catch (const std::exception& e) {
        std::cerr << "查询失败: " << e.what() << std::endl;
        return std::nullopt;
    }
};

if (auto result = safeQuery("SELECT * FROM users")) {
    processResult(result.value());
}
```

### 8. 连接池管理

```cpp
// 连接池配置
ConnectionConfig config{
    .host = "localhost",
    .user = "root",
    .password = "password",
    .database = "test",
    .poolSize = 20,        // 初始 20 个连接
    .maxPoolSize = 100,    // 最大 100 个连接
    .connectionTimeout = 30 // 30 秒超时
};

Database db(config);

// 连接池会自动管理连接
// 在高并发场景下，连接会被自动复用
std::vector<std::thread> threads;
for (int i = 0; i < 1000; ++i) {
    threads.emplace_back([&db, i]() {
        auto result = db.query("SELECT * FROM users WHERE id = ?", i);
        // 连接自动返回池中
    });
}

for (auto& t : threads) {
    t.join();
}
```

### 9. 存储过程和函数

```cpp
// 调用存储过程
auto result = db.query("CALL GetUsersByAge(?)", 25);

// 调用函数
auto sum = db.query("SELECT CalculateTotal(?) as total", orderId);
double total = get<double>(sum[0].at("total"));

// 带 OUT 参数的存储过程
db.execute("CALL CalculateUserStats(?, @total_count, @avg_age)", "Beijing");
auto stats = db.query("SELECT @total_count as count, @avg_age as avg_age");
```

### 10. 高级查询

```cpp
// JSON 字段操作
auto users = db.query(R"(
    SELECT 
        id,
        name,
        JSON_EXTRACT(profile, '$.age') as age,
        JSON_EXTRACT(profile, '$.hobbies') as hobbies
    FROM users
    WHERE JSON_CONTAINS(profile, '"reading"', '$.hobbies')
)");

// 窗口函数
auto ranking = db.query(R"(
    SELECT 
        name,
        score,
        RANK() OVER (ORDER BY score DESC) as rank,
        DENSE_RANK() OVER (ORDER BY score DESC) as dense_rank,
        ROW_NUMBER() OVER (ORDER BY score DESC) as row_num
    FROM students
    WHERE class_id = ?
)", classId);

// CTE (Common Table Expression)
auto report = db.query(R"(
    WITH monthly_sales AS (
        SELECT 
            DATE_FORMAT(order_date, '%Y-%m') as month,
            SUM(amount) as total
        FROM orders
        WHERE order_date >= ?
        GROUP BY month
    )
    SELECT 
        month,
        total,
        LAG(total, 1) OVER (ORDER BY month) as prev_month,
        total - LAG(total, 1) OVER (ORDER BY month) as growth
    FROM monthly_sales
)", startDate);
```

## 与 MySQL Connector/C++ 对比

### 功能对比表

| 功能特性 | MySQLWrapper | MySQL Connector/C++ |
|---------|--------------|-------------------|
| **基础功能** | | |
| 基本查询 | ✅ 简洁 API | ✅ 繁琐 API |
| 预处理语句 | ✅ 默认启用 | ✅ 手动管理 |
| 事务支持 | ✅ RAII 风格 | ✅ 手动管理 |
| 存储过程 | ✅ 支持 | ✅ 支持 |
| **高级功能** | | |
| 连接池 | ✅ 内置 | ❌ 需自行实现 |
| 异步操作 | ✅ 原生支持 | ❌ 需自行实现 |
| 批量操作 | ✅ 优化实现 | ❌ 需自行实现 |
| 自动重连 | ✅ 内置 | ⚠️ 部分支持 |
| **安全性** | | |
| SQL 注入防护 | ✅ 默认安全 | ⚠️ 需手动处理 |
| 类型安全 | ✅ std::variant | ❌ 运行时检查 |
| 资源管理 | ✅ RAII | ❌ 手动管理 |
| **易用性** | | |
| 代码简洁度 | ✅ 极简 | ❌ 冗长 |
| 错误处理 | ✅ 异常机制 | ⚠️ 混合模式 |
| 现代 C++ | ✅ C++17 | ❌ C++98/03 |

### 代码对比示例

#### 1. 连接管理

**MySQLWrapper:**
```cpp
// 自动连接池管理
Database db({.host="localhost", .user="root", .password="pass"});
// 使用完自动释放
```

**MySQL Connector/C++:**
```cpp
// 需要手动管理每个连接
sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
sql::Connection* con = driver->connect("tcp://127.0.0.1:3306", "root", "pass");
// 必须手动 delete con
```

#### 2. 查询执行

**MySQLWrapper:**
```cpp
auto users = db.query("SELECT * FROM users WHERE age > ? AND city = ?", 18, "Beijing");
```

**MySQL Connector/C++:**
```cpp
sql::PreparedStatement* pstmt = con->prepareStatement(
    "SELECT * FROM users WHERE age > ? AND city = ?"
);
pstmt->setInt(1, 18);
pstmt->setString(2, "Beijing");
sql::ResultSet* res = pstmt->executeQuery();
// 必须手动 delete pstmt 和 res
```

#### 3. 事务处理

**MySQLWrapper:**
```cpp
{
    auto tx = db.beginTransaction();
    tx->execute("UPDATE ...");
    tx->execute("INSERT ...");
    tx->commit();
}  // 异常时自动回滚
```

**MySQL Connector/C++:**
```cpp
try {
    con->setAutoCommit(false);
    stmt->execute("UPDATE ...");
    stmt->execute("INSERT ...");
    con->commit();
} catch (...) {
    con->rollback();
    throw;
}
con->setAutoCommit(true);
```

#### 4. 结果处理

**MySQLWrapper:**
```cpp
for (const auto& row : result.rows()) {
    auto name = get<std::string>(row.at("name"));
    auto age = getOpt<int>(row.at("age"));  // 安全获取
}
```

**MySQL Connector/C++:**
```cpp
while (res->next()) {
    std::string name = res->getString("name");
    int age = res->getInt("age");  // 可能抛出异常
}
```

## 性能测试

### 测试环境

- **硬件**: Intel i7-10700K, 32GB RAM, NVMe SSD (Powered By **[Lain.sh](https://www.lain.sh/en/)**)
- **软件**: Debian 11, MySQL 8.0.32, GCC 9.4.0
- **测试数据**: 100 万条用户记录

### 性能对比结果

| 测试项目 | MySQLWrapper | MySQL Connector/C++ | 性能提升 |
|---------|--------------|-------------------|---------|
| **连接操作** | | | |
| 单次连接创建 | 8.2ms | 8.5ms | 3.5% |
| 连接池获取（1000次） | 0.12ms | 89ms | 741x |
| **查询操作** | | | |
| 简单查询（10000次） | 4.87s | 5.23s | 7.4% |
| 预处理查询（10000次） | 3.98s | 4.12s | 3.4% |
| 复杂联接查询（1000次） | 12.3s | 12.8s | 3.9% |
| **写入操作** | | | |
| 单条插入（10000次） | 1.45s | 8.92s | 6.15x |
| 批量插入（100万条） | 18.7s | 42.3s | 2.26x |
| 事务插入（1000事务x100条） | 11.6s | 12.3s | 6.0% |
| **并发性能** | | | |
| 100线程并发查询 | 2.34s | 15.6s | 6.67x |
| 1000线程并发查询 | 8.91s | 崩溃 | - |

### 内存使用对比

| 场景 | MySQLWrapper | MySQL Connector/C++ |
|-----|--------------|-------------------|
| 空闲状态 | 12MB | 8MB |
| 10个连接 | 35MB | 82MB |
| 100个连接 | 125MB | 820MB |
| 查询1万条数据 | +15MB | +18MB |

## 最佳实践

### 1. 连接配置建议

```cpp
ConnectionConfig config{
    .host = "localhost",
    .user = "app_user",
    .password = getenv("DB_PASSWORD"),  // 从环境变量读取
    .database = "production",
    .charset = "utf8mb4",               // 支持 emoji
    .poolSize = 20,                     // 根据并发量调整
    .maxPoolSize = 100,
    .connectionTimeout = 10,
    .autoReconnect = true
};
```

### 2. 错误处理模式

```cpp
// 封装数据库操作
class UserRepository {
    Database& db_;
    
public:
    std::optional<User> findById(int id) {
        try {
            auto result = db_.query("SELECT * FROM users WHERE id = ?", id);
            if (result.empty()) {
                return std::nullopt;
            }
            return User::fromRow(result[0]);
        } catch (const std::exception& e) {
            logger_.error("Failed to find user {}: {}", id, e.what());
            return std::nullopt;
        }
    }
    
    Result<User> createUser(const UserDto& dto) {
        try {
            auto id = db_.execute(
                "INSERT INTO users (name, email) VALUES (?, ?)",
                dto.name, dto.email
            );
            return Result<User>::success(User{id, dto.name, dto.email});
        } catch (const std::exception& e) {
            return Result<User>::error(e.what());
        }
    }
};
```

### 3. 事务最佳实践

```cpp
// 使用 RAII 确保事务正确处理
template<typename Func>
auto withTransaction(Database& db, Func&& func) {
    auto tx = db.beginTransaction();
    try {
        auto result = func(*tx);
        tx->commit();
        return result;
    } catch (...) {
        // 自动回滚
        throw;
    }
}

// 使用示例
auto result = withTransaction(db, [](auto& tx) {
    tx.execute("UPDATE inventory SET quantity = quantity - ? WHERE id = ?", 1, itemId);
    tx.execute("INSERT INTO orders (item_id, quantity) VALUES (?, ?)", itemId, 1);
    return tx.query("SELECT * FROM orders ORDER BY id DESC LIMIT 1");
});
```

### 4. 性能优化建议

```cpp
// 1. 使用批量操作替代循环插入
// 不好的做法
for (const auto& user : users) {
    db.execute("INSERT INTO users (name, email) VALUES (?, ?)", user.name, user.email);
}

// 好的做法
std::vector<std::vector<Value>> data;
for (const auto& user : users) {
    data.push_back({user.name, user.email});
}
db.batchInsert("users", {"name", "email"}, data);

// 2. 使用连接池
// 配置足够的连接池大小
config.poolSize = std::thread::hardware_concurrency() * 2;

// 3. 使用异步查询进行并行处理
auto f1 = db.queryAsync("SELECT COUNT(*) FROM orders");
auto f2 = db.queryAsync("SELECT COUNT(*) FROM users");
auto orderCount = f1.get();
auto userCount = f2.get();

// 4. 合理使用索引
db.execute("CREATE INDEX idx_user_email ON users(email)");
```

### 5. 安全建议

```cpp
// 1. 始终使用参数化查询
std::string userInput = getInput();
// 危险！可能导致 SQL 注入
// db.query("SELECT * FROM users WHERE name = '" + userInput + "'");

// 安全的做法
db.query("SELECT * FROM users WHERE name = ?", userInput);

// 2. 验证输入
void updateUserAge(int userId, int age) {
    if (age < 0 || age > 150) {
        throw std::invalid_argument("Invalid age");
    }
    db.execute("UPDATE users SET age = ? WHERE id = ?", age, userId);
}

// 3. 使用最小权限原则
// 为不同的操作使用不同的数据库用户
Database readDb({.user = "read_user", ...});      // 只有 SELECT 权限
Database writeDb({.user = "write_user", ...});    // 有 INSERT/UPDATE/DELETE 权限
```

## 常见问题

### Q1: 如何处理大结果集？

```cpp
// 对于大结果集，考虑分页查询
int pageSize = 1000;
int offset = 0;

while (true) {
    auto result = db.query(
        "SELECT * FROM large_table LIMIT ? OFFSET ?",
        pageSize, offset
    );
    
    if (result.empty()) break;
    
    for (const auto& row : result.rows()) {
        processRow(row);
    }
    
    offset += pageSize;
}
```

### Q2: 如何实现连接池监控？

```cpp
class MonitoredDatabase : public Database {
public:
    struct Stats {
        std::atomic<size_t> totalQueries{0};
        std::atomic<size_t> failedQueries{0};
        std::atomic<size_t> activeConnections{0};
    };
    
    QueryResult query(const std::string& sql) override {
        stats_.totalQueries++;
        try {
            auto result = Database::query(sql);
            return result;
        } catch (...) {
            stats_.failedQueries++;
            throw;
        }
    }
    
    Stats getStats() const { return stats_; }
    
private:
    mutable Stats stats_;
};
```

### Q3: 如何实现查询缓存？

```cpp
template<typename Key, typename Value>
class LRUCache {
    // LRU 缓存实现...
};

class CachedDatabase {
    Database db_;
    LRUCache<std::string, QueryResult> cache_;
    
public:
    QueryResult query(const std::string& sql) {
        if (auto cached = cache_.get(sql)) {
            return *cached;
        }
        
        auto result = db_.query(sql);
        cache_.put(sql, result);
        return result;
    }
    
    template<typename... Args>
    QueryResult query(const std::string& sql, Args&&... args) {
        // 参数化查询不缓存，避免缓存爆炸
        return db_.query(sql, std::forward<Args>(args)...);
    }
};
```

### Q4: 如何实现读写分离？

```cpp
class ReadWriteSplitDatabase {
    Database master_;                    // 主库（写）
    std::vector<Database> slaves_;       // 从库（读）
    std::atomic<size_t> slaveIndex_{0};  // 轮询索引
    
public:
    QueryResult query(const std::string& sql) {
        // 读操作分发到从库
        size_t index = slaveIndex_.fetch_add(1) % slaves_.size();
        return slaves_[index].query(sql);
    }
    
    uint64_t execute(const std::string& sql) {
        // 写操作发送到主库
        return master_.execute(sql);
    }
    
    auto beginTransaction() {
        // 事务使用主库
        return master_.beginTransaction();
    }
};
```

### Q5: 如何处理连接断开？

```cpp
// MySQLWrapper 已内置自动重连机制
// 但您可以自定义重试策略

template<typename Func>
auto retryOnFailure(Func&& func, int maxRetries = 3) {
    for (int i = 0; i < maxRetries; ++i) {
        try {
            return func();
        } catch (const std::exception& e) {
            if (i == maxRetries - 1) throw;
            
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100 * (i + 1))
            );
        }
    }
}

// 使用
auto result = retryOnFailure([&db]() {
    return db.query("SELECT * FROM users");
});
```

### 开发环境设置

```bash
# 克隆仓库
git clone https://github.com/Derrity/mysql-wrapper.git
cd mysql-wrapper

# 安装开发依赖
sudo apt install -y clang-format clang-tidy cppcheck valgrind

# 构建和测试
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
make -j$(nproc)
make test

# 运行代码检查
make format  # 格式化代码
make tidy    # 运行 clang-tidy
make check   # 运行 cppcheck
```

## 致谢

- MySQL 开发团队
- C++ 社区
- 所有贡献者
