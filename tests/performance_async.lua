---------------------------------------------------------------------
-- LuaSQL Performance and Async Test Suite
---------------------------------------------------------------------
-- This script benchmarks LuaSQL drivers for throughput and latency.
-- It supports Sync and Async (PostgreSQL and MySQL) operations.
--
-- Environment Variables for Configuration:
--   DB_HOST: Database host (default: localhost)
--   DB_NAME: Database name or schema (default: luasql_test)
--   DB_USER: Database username (default: luasql)
--   DB_PASS: Database password (default: luasql)
--
-- How to run for different databases:
--
-- 1. PostgreSQL (with Async support):
--    DB_HOST=localhost DB_NAME=postgres DB_USER=postgres DB_PASS=123456 \
--    lua tests/performance_async.lua postgres
--
-- 2. MySQL / MariaDB (with Async support):
--    DB_HOST=localhost DB_NAME=luasql_test DB_USER=luasql DB_PASS=luasql \
--    lua tests/performance_async.lua mysql
--
-- 3. SQLite3 (In-memory, no variables needed):
--    lua tests/performance_async.lua sqlite3
---------------------------------------------------------------------

local driver_name = arg[1] or "sqlite3"
local luasql = require("luasql."..driver_name)
local env = luasql[driver_name]()

-- Configuration
local ITERATIONS = 1000
local ASYNC_BATCH = 50

-- Helper to establish connection based on driver and environment variables
local function get_connection()
    local host = os.getenv("DB_HOST") or "localhost"
    local dbname = os.getenv("DB_NAME") or "luasql_test"
    local user = os.getenv("DB_USER") or "luasql"
    local pass = os.getenv("DB_PASS") or "luasql"
    local port = os.getenv("DB_PORT")

    if driver_name == "sqlite3" or driver_name == "sqlite" or driver_name == "duckdb" then
        return env:connect(":memory:")
    elseif driver_name == "postgres" then
        local conn_str = string.format("host=%s dbname=%s user=%s password=%s", host, dbname, user, pass)
        if port then conn_str = conn_str .. " port=" .. port end
        return env:connect(conn_str)
    elseif driver_name == "mysql" then
        return env:connect(dbname, user, pass, host, port)
    elseif driver_name == "firebird" then
        return env:connect(host..":"..dbname..".fdb", user, pass)
    elseif driver_name == "oci8" then
        return env:connect(string.format("//%s:%s/FREEPDB1", host, port or "1521"), user, pass)
    elseif driver_name == "odbc" then
        return env:connect(dbname, user, pass)
    else
        error("Unsupported driver: " .. driver_name)
    end
end

local conn = assert(get_connection())

-- Safe drop table across all databases
pcall(function() conn:execute("DROP TABLE perf_test") end)

if driver_name == "mysql" then
    assert(conn:execute("CREATE TABLE perf_test (id INTEGER, val VARCHAR(255)) ENGINE=InnoDB"))
else
    assert(conn:execute("CREATE TABLE perf_test (id INTEGER, val VARCHAR(255))"))
end

print(string.format("Starting performance test for %s (%d iterations)...", driver_name, ITERATIONS))

-- 1. Sync Performance (Baseline)
print("  Running Sync INSERT test...")
local start_time = os.clock()
for i = 1, ITERATIONS do
    conn:execute(string.format("INSERT INTO perf_test VALUES (%d, 'sync_value_%d')", i, i))
end
local end_time = os.clock()
local sync_duration = end_time - start_time
print(string.format("  Sync INSERT: %.4f seconds (%.2f op/s)", sync_duration, ITERATIONS / sync_duration))

-- 2. Async/Batch Performance
if conn.send_query and conn.get_result then
    if driver_name == "postgres" then
        print(string.format("  Running %s Async INSERT test (batch size: %d)...", driver_name, ASYNC_BATCH))
        conn:execute("DELETE FROM perf_test")
        
        start_time = os.clock()
        for i = 1, ITERATIONS, ASYNC_BATCH do
            for j = 0, ASYNC_BATCH - 1 do
                if (i+j) <= ITERATIONS then
                    conn:send_query(string.format("INSERT INTO perf_test VALUES (%d, 'async_value_%d')", i+j, i+j))
                end
            end
            for j = 0, ASYNC_BATCH - 1 do
                if (i+j) <= ITERATIONS then
                    local res = conn:get_result()
                    while res do
                        if type(res) == "userdata" then res:close() end
                        res = conn:get_result()
                    end
                end
            end
        end
        end_time = os.clock()
        local async_duration = end_time - start_time
        print(string.format("  Async INSERT: %.4f seconds (%.2f op/s)", async_duration, ITERATIONS / async_duration))
        print(string.format("  Async is %.2fx faster than Sync", sync_duration / async_duration))
    else
        print(string.format("  Running %s Async INSERT test (batch size: %d)...", driver_name, ASYNC_BATCH))
        conn:execute("DELETE FROM perf_test")
        
        start_time = os.clock()
        for i = 1, ITERATIONS, ASYNC_BATCH do
            local batch_handles = {}
            for j = 0, ASYNC_BATCH - 1 do
                if (i+j) <= ITERATIONS then
                    local status, ret = conn:send_query(string.format("INSERT INTO perf_test VALUES (%d, 'async_value_%d')", i+j, i+j))
                    if status ~= 0 then
                        table.insert(batch_handles, status)
                    end
                end
            end
            
            for _, status in ipairs(batch_handles) do
                local busy, new_status = conn:poll(status)
                while busy do
                    busy, new_status = conn:poll(new_status)
                end
            end
            
            for j = 0, ASYNC_BATCH - 1 do
                if (i+j) <= ITERATIONS then
                    local res = conn:get_result()
                    if type(res) == "userdata" then res:close() end
                end
            end
        end
        end_time = os.clock()
        local async_duration = end_time - start_time
        print(string.format("  Async INSERT: %.4f seconds (%.2f op/s)", async_duration, ITERATIONS / async_duration))
        print(string.format("  Async is %.2fx faster than Sync", sync_duration / async_duration))
    end
else
    print("  Native Async (send_query/get_result) not available for this driver.")
end

-- 3. Read Performance
print("  Running SELECT test...")
start_time = os.clock()
local cur, err = conn:execute("SELECT * FROM perf_test")
if not cur then error("SELECT failed: " .. tostring(err)) end
local row = cur:fetch({}, "a")
local count = 0
while row do
    count = count + 1
    row = cur:fetch(row, "a")
end
cur:close()
end_time = os.clock()
print(string.format("  Sync SELECT (%d rows): %.4f seconds", count, end_time - start_time))

conn:close()
env:close()
print("Done.\n")
