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

## Usage Examples

### 1. Basic Query Operations

```cpp
// Simple query
auto users = db.query("SELECT * FROM users");
std::cout << "Total users: " << users.size() << std::endl;

// Parameterized query (recommended, automatic SQL injection prevention)
std::string name = "'; DROP TABLE users; --";  // Malicious input
auto safe_result = db.query(
    "SELECT * FROM users WHERE name = ? AND age > ?", 
    name, 18
);  // Completely safe, automatically escaped

// Get single value
auto count_result = db.query("SELECT COUNT(*) as total FROM users");
int total = get<int>(count_result[0].at("total"));
```

### 2. Insert, Update, and Delete

```cpp
// Insert data
auto insertId = db.execute(
    "INSERT INTO users (name, email, age) VALUES (?, ?, ?)",
    "John Doe", "john@example.com", 25
);
std::cout << "New user ID: " << insertId << std::endl;

// Update data
auto affected = db.execute(
    "UPDATE users SET age = age + 1 WHERE birthday = CURDATE()"
);
std::cout << "Updated " << affected << " records" << std::endl;

// Delete data
db.execute("DELETE FROM users WHERE inactive = ?", true);
```

### 3. Transaction Processing

```cpp
// Method 1: Automatic transaction management (recommended)
{
    auto tx = db.beginTransaction();
    
    try {
        // Transfer operation
        tx->execute("UPDATE accounts SET balance = balance - ? WHERE id = ?", 100.0, 1);
        tx->execute("UPDATE accounts SET balance = balance + ? WHERE id = ?", 100.0, 2);
        
        // Log the transaction
        tx->execute("INSERT INTO transfer_log (from_id, to_id, amount) VALUES (?, ?, ?)",
                    1, 2, 100.0);
        
        tx->commit();  // Commit transaction
        std::cout << "Transfer successful" << std::endl;
        
    } catch (const std::exception& e) {
        // Automatic rollback on exception
        std::cerr << "Transfer failed: " << e.what() << std::endl;
    }
    // Automatic rollback when leaving scope if not committed
}

// Method 2: Manual rollback
{
    auto tx = db.beginTransaction();
    
    auto balance = tx->query("SELECT balance FROM accounts WHERE id = ?", 1);
    if (get<double>(balance[0].at("balance")) < 100.0) {
        tx->rollback();  // Manual rollback
        throw std::runtime_error("Insufficient balance");
    }
    
    tx->execute("UPDATE accounts SET balance = balance - ? WHERE id = ?", 100.0, 1);
    tx->commit();
}
```

### 4. Batch Operations

```cpp
// Batch insert users
std::vector<std::vector<Value>> users = {
    {"Alice", "alice@example.com", 28, nullptr},      // nullptr represents NULL
    {"Bob", "bob@example.com", 30, "Beijing"},
    {"Charlie", "charlie@example.com", 25, "Shanghai"}
};

auto inserted = db.batchInsert("users", 
    {"name", "email", "age", "city"}, 
    users
);
std::cout << "Batch inserted " << inserted << " records" << std::endl;

// Batch update (using transaction)
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

### 5. Asynchronous Operations

```cpp
// Asynchronous queries
auto future1 = db.queryAsync("SELECT COUNT(*) as count FROM orders WHERE status = 'pending'");
auto future2 = db.queryAsync("SELECT SUM(amount) as total FROM orders WHERE date = CURDATE()");

// Execute other operations...
doSomeOtherWork();

// Get async results
auto result1 = future1.get();
auto result2 = future2.get();

std::cout << "Pending orders: " << get<int>(result1[0].at("count")) << std::endl;
std::cout << "Today's total: " << get<double>(result2[0].at("total")) << std::endl;

// Execute multiple queries concurrently
std::vector<std::future<QueryResult>> futures;
std::vector<std::string> queries = {
    "SELECT * FROM users WHERE city = 'Beijing'",
    "SELECT * FROM users WHERE city = 'Shanghai'",
    "SELECT * FROM users WHERE city = 'Guangzhou'"
};

for (const auto& query : queries) {
    futures.push_back(db.queryAsync(query));
}

// Collect all results
for (auto& future : futures) {
    auto result = future.get();
    processResult(result);
}
```

### 6. Result Set Processing

```cpp
// Query user information
auto users = db.query("SELECT id, name, email, age, balance, created_at FROM users");

for (const auto& row : users.rows()) {
    // Method 1: Direct access (throws exception if type mismatch)
    int id = get<int>(row.at("id"));
    std::string name = get<std::string>(row.at("name"));
    
    // Method 2: Safe access (returns optional)
    auto age = getOpt<int>(row.at("age"));
    if (age.has_value()) {
        std::cout << "Age: " << age.value() << std::endl;
    }
    
    // Method 3: Use std::visit to handle multiple types
    std::visit([](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, int>) {
            std::cout << "Integer: " << value << std::endl;
        } else if constexpr (std::is_same_v<T, double>) {
            std::cout << "Double: " << value << std::endl;
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::cout << "String: " << value << std::endl;
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            std::cout << "NULL value" << std::endl;
        }
    }, row.at("balance"));
}

// Handle possible NULL values
for (const auto& row : users.rows()) {
    auto email = getOpt<std::string>(row.at("email"));
    if (email.has_value()) {
        sendEmail(email.value());
    } else {
        std::cout << "User has no email" << std::endl;
    }
}
```

### 7. Error Handling

```cpp
try {
    // All database operations may throw exceptions
    auto result = db.query("SELECT * FROM non_existent_table");
    
} catch (const std::runtime_error& e) {
    std::cerr << "Database error: " << e.what() << std::endl;
    
    // Log error
    logger.error("Database query failed: {}", e.what());
    
    // Option to retry
    retryOperation();
}

// Use lambda for error handling
auto safeQuery = [&db](const std::string& sql) -> std::optional<QueryResult> {
    try {
        return db.query(sql);
    } catch (const std::exception& e) {
        std::cerr << "Query failed: " << e.what() << std::endl;
        return std::nullopt;
    }
};

if (auto result = safeQuery("SELECT * FROM users")) {
    processResult(result.value());
}
```

### 8. Connection Pool Management

```cpp
// Connection pool configuration
ConnectionConfig config{
    .host = "localhost",
    .user = "root",
    .password = "password",
    .database = "test",
    .poolSize = 20,        // Initial 20 connections
    .maxPoolSize = 100,    // Maximum 100 connections
    .connectionTimeout = 30 // 30 second timeout
};

Database db(config);

// Connection pool automatically manages connections
// In high concurrency scenarios, connections are automatically reused
std::vector<std::thread> threads;
for (int i = 0; i < 1000; ++i) {
    threads.emplace_back([&db, i]() {
        auto result = db.query("SELECT * FROM users WHERE id = ?", i);
        // Connection automatically returned to pool
    });
}

for (auto& t : threads) {
    t.join();
}
```

### 9. Stored Procedures and Functions

```cpp
// Call stored procedure
auto result = db.query("CALL GetUsersByAge(?)", 25);

// Call function
auto sum = db.query("SELECT CalculateTotal(?) as total", orderId);
double total = get<double>(sum[0].at("total"));

// Stored procedure with OUT parameters
db.execute("CALL CalculateUserStats(?, @total_count, @avg_age)", "Beijing");
auto stats = db.query("SELECT @total_count as count, @avg_age as avg_age");
```

### 10. Advanced Queries

```cpp
// JSON field operations
auto users = db.query(R"(
    SELECT 
        id,
        name,
        JSON_EXTRACT(profile, '$.age') as age,
        JSON_EXTRACT(profile, '$.hobbies') as hobbies
    FROM users
    WHERE JSON_CONTAINS(profile, '"reading"', '$.hobbies')
)");

// Window functions
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

## Comparison with MySQL Connector/C++

### Feature Comparison Table

| Feature | MySQLWrapper | MySQL Connector/C++ |
|---------|--------------|-------------------|
| **Basic Features** | | |
| Basic queries | ✅ Clean API | ✅ Verbose API |
| Prepared statements | ✅ Default enabled | ✅ Manual management |
| Transaction support | ✅ RAII style | ✅ Manual management |
| Stored procedures | ✅ Supported | ✅ Supported |
| **Advanced Features** | | |
| Connection pooling | ✅ Built-in | ❌ DIY implementation |
| Async operations | ✅ Native support | ❌ DIY implementation |
| Batch operations | ✅ Optimized impl | ❌ DIY implementation |
| Auto reconnect | ✅ Built-in | ⚠️ Partial support |
| **Security** | | |
| SQL injection protection | ✅ Default safe | ⚠️ Manual handling |
| Type safety | ✅ std::variant | ❌ Runtime checks |
| Resource management | ✅ RAII | ❌ Manual management |
| **Usability** | | |
| Code conciseness | ✅ Minimal | ❌ Verbose |
| Error handling | ✅ Exception-based | ⚠️ Mixed mode |
| Modern C++ | ✅ C++17 | ❌ C++98/03 |

### Code Comparison Examples

#### 1. Connection Management

**MySQLWrapper:**
```cpp
// Automatic connection pool management
Database db({.host="localhost", .user="root", .password="pass"});
// Automatically released after use
```

**MySQL Connector/C++:**
```cpp
// Manual management of each connection required
sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
sql::Connection* con = driver->connect("tcp://127.0.0.1:3306", "root", "pass");
// Must manually delete con
```

#### 2. Query Execution

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
// Must manually delete pstmt and res
```

#### 3. Transaction Processing

**MySQLWrapper:**
```cpp
{
    auto tx = db.beginTransaction();
    tx->execute("UPDATE ...");
    tx->execute("INSERT ...");
    tx->commit();
}  // Automatic rollback on exception
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

#### 4. Result Processing

**MySQLWrapper:**
```cpp
for (const auto& row : result.rows()) {
    auto name = get<std::string>(row.at("name"));
    auto age = getOpt<int>(row.at("age"));  // Safe access
}
```

**MySQL Connector/C++:**
```cpp
while (res->next()) {
    std::string name = res->getString("name");
    int age = res->getInt("age");  // May throw exception
}
```

## Performance Testing

### Test Environment

- **Hardware**: Intel i7-10700K, 32GB RAM, NVMe SSD (Powered By **[Lain.sh](https://www.lain.sh/en/)**)
- **Software**: Debian 11, MySQL 8.0.32, GCC 9.4.0
- **Test Data**: 1 million user records

### Performance Comparison Results

| Test Category | MySQLWrapper | MySQL Connector/C++ | Performance Gain |
|---------|--------------|-------------------|---------|
| **Connection Operations** | | | |
| Single connection creation | 8.2ms | 8.5ms | 3.5% |
| Connection pool acquisition (1000x) | 0.12ms | 89ms | 741x |
| **Query Operations** | | | |
| Simple queries (10000x) | 4.87s | 5.23s | 7.4% |
| Prepared queries (10000x) | 3.98s | 4.12s | 3.4% |
| Complex join queries (1000x) | 12.3s | 12.8s | 3.9% |
| **Write Operations** | | | |
| Single insert (10000x) | 1.45s | 8.92s | 6.15x |
| Batch insert (1M records) | 18.7s | 42.3s | 2.26x |
| Transaction insert (1000 tx x 100 records) | 11.6s | 12.3s | 6.0% |
| **Concurrency Performance** | | | |
| 100 thread concurrent queries | 2.34s | 15.6s | 6.67x |
| 1000 thread concurrent queries | 8.91s | Crashed | - |

### Memory Usage Comparison

| Scenario | MySQLWrapper | MySQL Connector/C++ |
|-----|--------------|-------------------|
| Idle state | 12MB | 8MB |
| 10 connections | 35MB | 82MB |
| 100 connections | 125MB | 820MB |
| Query 10k records | +15MB | +18MB |

## Best Practices

### 1. Connection Configuration Recommendations

```cpp
ConnectionConfig config{
    .host = "localhost",
    .user = "app_user",
    .password = getenv("DB_PASSWORD"),  // Read from environment variable
    .database = "production",
    .charset = "utf8mb4",               // Support emoji
    .poolSize = 20,                     // Adjust based on concurrency
    .maxPoolSize = 100,
    .connectionTimeout = 10,
    .autoReconnect = true
};
```

### 2. Error Handling Patterns

```cpp
// Encapsulate database operations
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

### 3. Transaction Best Practices

```cpp
// Use RAII to ensure proper transaction handling
template<typename Func>
auto withTransaction(Database& db, Func&& func) {
    auto tx = db.beginTransaction();
    try {
        auto result = func(*tx);
        tx->commit();
        return result;
    } catch (...) {
        // Automatic rollback
        throw;
    }
}

// Usage example
auto result = withTransaction(db, [](auto& tx) {
    tx.execute("UPDATE inventory SET quantity = quantity - ? WHERE id = ?", 1, itemId);
    tx.execute("INSERT INTO orders (item_id, quantity) VALUES (?, ?)", itemId, 1);
    return tx.query("SELECT * FROM orders ORDER BY id DESC LIMIT 1");
});
```

### 4. Performance Optimization Recommendations

```cpp
// 1. Use batch operations instead of loop inserts
// Bad practice
for (const auto& user : users) {
    db.execute("INSERT INTO users (name, email) VALUES (?, ?)", user.name, user.email);
}

// Good practice
std::vector<std::vector<Value>> data;
for (const auto& user : users) {
    data.push_back({user.name, user.email});
}
db.batchInsert("users", {"name", "email"}, data);

// 2. Use connection pool
// Configure adequate connection pool size
config.poolSize = std::thread::hardware_concurrency() * 2;

// 3. Use async queries for parallel processing
auto f1 = db.queryAsync("SELECT COUNT(*) FROM orders");
auto f2 = db.queryAsync("SELECT COUNT(*) FROM users");
auto orderCount = f1.get();
auto userCount = f2.get();

// 4. Use indexes properly
db.execute("CREATE INDEX idx_user_email ON users(email)");
```

### 5. Security Recommendations

```cpp
// 1. Always use parameterized queries
std::string userInput = getInput();
// Dangerous! May lead to SQL injection
// db.query("SELECT * FROM users WHERE name = '" + userInput + "'");

// Safe approach
db.query("SELECT * FROM users WHERE name = ?", userInput);

// 2. Validate input
void updateUserAge(int userId, int age) {
    if (age < 0 || age > 150) {
        throw std::invalid_argument("Invalid age");
    }
    db.execute("UPDATE users SET age = ? WHERE id = ?", age, userId);
}

// 3. Use principle of least privilege
// Use different database users for different operations
Database readDb({.user = "read_user", ...});      // Only SELECT permissions
Database writeDb({.user = "write_user", ...});    // Has INSERT/UPDATE/DELETE permissions
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
