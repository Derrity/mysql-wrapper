# MySQLWrapper

MySQLWrapper is a C++23 MySQL client wrapper around the MySQL C API. Version 2
uses RAII handles, `std::expected` errors, move-only connection leases, a
contiguous result layout, and a `std::jthread` based async executor.

The primary namespace is `mysqlw`.

## Requirements

- C++23 compiler with `std::expected`
- CMake 3.28+ or xmake 2.9.9+
- `pkg-config`
- MySQL client development files that provide `mysqlclient.pc`

On macOS with Homebrew:

```sh
brew install mysql cmake pkg-config xmake ninja
```

## Build

### CMake

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The CMake build defaults to the traditional header interface because the Apple
Clang shipped with the tested Xcode toolchain does not compile named C++ module
interfaces. On a compiler/generator combination that supports C++ modules, opt
in with:

```sh
cmake -S . -B build -G Ninja -DMYSQLWRAPPER_BUILD_MODULES=ON
```

### xmake

```sh
xmake f -m release --tests=y
xmake
xmake test
```

C++ module support is available as an option, but disabled by default for
portable local builds:

```sh
xmake f --modules=y
xmake
```

## Integration Testing With Podman

The integration test is opt-in. Without `MYSQLWRAPPER_RUN_INTEGRATION=1` it
prints a skip message and exits successfully.

Start a disposable MySQL container:

```sh
podman run -d --name mysqlwrapper-mysql-test \
  -e MYSQL_ROOT_PASSWORD=mysqlwrapper \
  -e MYSQL_DATABASE=mysqlwrapper_test \
  -e MYSQL_ROOT_HOST=% \
  -p 127.0.0.1:3307:3306 \
  mysql:8.4

until podman exec mysqlwrapper-mysql-test \
  mysqladmin ping -uroot -pmysqlwrapper --silent; do
  sleep 1
done
```

Run the integration executable:

```sh
MYSQLWRAPPER_RUN_INTEGRATION=1 \
MYSQLWRAPPER_TEST_HOST=127.0.0.1 \
MYSQLWRAPPER_TEST_PORT=3307 \
MYSQLWRAPPER_TEST_USER=root \
MYSQLWRAPPER_TEST_PASSWORD=mysqlwrapper \
MYSQLWRAPPER_TEST_DATABASE=mysqlwrapper_test \
./build/mysqlwrapper_integration_tests
```

With xmake:

```sh
MYSQLWRAPPER_RUN_INTEGRATION=1 \
MYSQLWRAPPER_TEST_HOST=127.0.0.1 \
MYSQLWRAPPER_TEST_PORT=3307 \
MYSQLWRAPPER_TEST_USER=root \
MYSQLWRAPPER_TEST_PASSWORD=mysqlwrapper \
MYSQLWRAPPER_TEST_DATABASE=mysqlwrapper_test \
xmake run mysqlwrapper_integration_tests
```

Clean up:

```sh
podman rm -f mysqlwrapper-mysql-test
```

## Basic Use

```cpp
#include "mysqlwrapper/mysql_wrapper.hpp"

#include <iostream>

int main() {
    mysqlw::ConnectionConfig config;
    config.host = "127.0.0.1";
    config.user = "app";
    config.password = "secret";
    config.database = "app";
    config.initial_pool_size = 4;
    config.max_pool_size = 32;

    mysqlw::Database db(config);

    auto users = db.query(
        "SELECT id, name FROM users WHERE active = ? AND age >= ?",
        true,
        18);

    if (!users) {
        std::cerr << mysqlw::to_string(users.error().code)
                  << ": " << users.error().message << '\n';
        return 1;
    }

    for (std::size_t i = 0; i < users->row_count(); ++i) {
        auto row = (*users)[i];
        std::cout << mysqlw::get_or_throw<std::int64_t>(row["id"]) << ' '
                  << mysqlw::get_or_throw<std::string>(row["name"]) << '\n';
    }
}
```

When modules are enabled and supported by the active toolchain:

```cpp
import mysql.wrapper;
```

## API Shape

- `Database::query(sql, args...) -> std::expected<Result, DbError>`
- `Database::execute(sql, args...) -> std::expected<ExecuteResult, DbError>`
- `Database::query_async(...) -> std::future<std::expected<Result, DbError>>`
- `Database::execute_async(...) -> std::future<std::expected<ExecuteResult, DbError>>`
- `Database::begin_transaction() -> std::expected<Transaction, DbError>`
- `Database::transaction(fn)` commits when `fn` succeeds and rolls back when
  `fn` returns an unexpected result.

`Result` stores columns once and rows as contiguous `std::vector<Value>` values.
Column-name lookup is resolved through a result-level index map instead of a
per-row `unordered_map`.

`Value` is:

```cpp
std::variant<
    std::nullptr_t,
    std::int64_t,
    std::uint64_t,
    double,
    std::string,
    std::vector<std::byte>,
    bool>
```

## Error Handling

Database operations return `std::expected`. `DbError` contains:

- `ErrorCode code`
- `Operation operation`
- `unsigned mysql_errno`
- `std::string sql_state`
- `std::string message`

Throwing convenience wrappers are available:

```cpp
auto result = db.query_or_throw("SELECT 1");
auto update = db.execute_or_throw("UPDATE users SET active = ?", true);
```

These wrappers throw `mysqlw::DbException`.

## Transactions

```cpp
auto result = db.transaction([](mysqlw::Transaction& tx) -> mysqlw::Expected<void> {
    auto debit = tx.execute("UPDATE accounts SET balance = balance - ? WHERE id = ?", 100.0, 1);
    if (!debit) {
        return std::unexpected(debit.error());
    }

    auto credit = tx.execute("UPDATE accounts SET balance = balance + ? WHERE id = ?", 100.0, 2);
    if (!credit) {
        return std::unexpected(credit.error());
    }

    return {};
});

if (!result) {
    // transaction has been rolled back
}
```

## Notes

- Version 2 is intentionally API/ABI breaking and only supports the modern
  `mysqlw` API.
- Integration tests that require a live MySQL server are opt-in and can be run
  with Podman using the commands above.
