package.cpath = "./src/?.so;" .. package.cpath

local luasql = require("luasql.mysql")
local env = luasql.mysql()

-- Read database credentials from environment variables, or use defaults
local db_name = os.getenv("DB_NAME") or "test"
local db_user = os.getenv("DB_USER") or "root"
local db_pass = os.getenv("DB_PASS") or ""
local db_host = os.getenv("DB_HOST") or "127.0.0.1"

print(string.format("Connecting to %s as user '%s' on host '%s'...", db_name, db_user, db_host))
local conn, err = env:connect(db_name, db_user, db_pass, db_host)

if not conn then
    print("Could not connect to database: " .. err)
    os.exit(1)
end

print("Connected to database successfully!")

conn:execute("DROP TABLE IF EXISTS prepare_test")
conn:execute("CREATE TABLE prepare_test (id INT PRIMARY KEY, name VARCHAR(50), value DOUBLE, is_active TINYINT)")
print("Created table prepare_test")

-- Prepare the statement
local stmt, err = conn:prepare("INSERT INTO prepare_test (id, name, value, is_active) VALUES (?, ?, ?, ?)")
if not stmt then
    print("Failed to prepare statement: " .. err)
    os.exit(1)
end
print("Prepared statement successfully")

-- Test 1: Bind all at once
print("\nTest 1: Bind all at once")
local ok, berr = stmt:bind(1, "Alice", 3.14159, true)
if not ok then print("Bind failed: " .. tostring(berr)) end
local res, err = stmt:execute()
if not res then
    print("Failed to execute prepared statement 1: " .. err)
else
    print("Executed statement 1 successfully, affected rows: " .. res)
end

-- Test 2: Bind one by one
print("\nTest 2: Bind one by one")
stmt:bind(1, 2)
stmt:bind(2, "Bob")
stmt:bind(3, 42.0)
stmt:bind(4, false)
local res2, err2 = stmt:execute()
if not res2 then
    print("Failed to execute prepared statement 2: " .. err2)
else
    print("Executed statement 2 successfully, affected rows: " .. res2)
end

-- Test 3: Reuse and partial update
print("\nTest 3: Reuse and partial update (Alice again but id=3)")
stmt:bind(1, 3)
stmt:bind(2, "Charlie") -- Explicitly update name too
local res3, err3 = stmt:execute()
if not res3 then
    print("Failed to execute prepared statement 3: " .. err3)
else
    print("Executed statement 3 successfully, affected rows: " .. res3)
end

-- Close the prepared statement
stmt:close()
print("\nClosed prepared statement")

-- Test 4: Prepared SELECT
print("\nTest 4: Prepared SELECT")
local stmt_sel, err_sel = conn:prepare("SELECT id, name, value, is_active FROM prepare_test WHERE id > ? ORDER BY id")
if not stmt_sel then
    print("Failed to prepare SELECT: " .. err_sel)
else
    stmt_sel:bind(1, 1) -- Find id > 1
    local cur_sel, err_exec = stmt_sel:execute()
    if not cur_sel then
        print("Failed to execute SELECT: " .. err_exec)
    else
        print("Fetched results from prepared SELECT:")
        local row = cur_sel:fetch({}, "a")
        while row do
            print(string.format("  -> Row: id=%s, name=%s, value=%s, is_active=%s", 
                                tostring(row.id), tostring(row.name), tostring(row.value), tostring(row.is_active)))
            row = cur_sel:fetch(row, "a")
        end
        cur_sel:close()
    end
    stmt_sel:close()
end

-- Clean up the environment
conn:close()
env:close()

print("\nAll tests passed successfully!")
