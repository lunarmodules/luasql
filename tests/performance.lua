TOTAL_ROWS = 8000

if os then clock = os.clock end

local env, err = sql:Open ("postgres")
assert (env, err)
conn, err = env:Connect ("luasql-test")
assert (conn, err)
conn:Execute ("drop table fetch_test")
-- Create test table
local n, err = conn:Execute ([[
	create table fetch_test (
		f1	varchar(30),
		f2	varchar(30),
		f3	varchar(30),
		f4	varchar(30),
		f5	varchar(30),
		f6	varchar(30),
		f7	varchar(30),
		f8	varchar(30)
	)]])
assert (n, err)
assert (type(n) == "number", "couldn't create fetch_test table")
-- Insert rows
for i = 1, TOTAL_ROWS do
	local n, err = conn:Execute (
		"insert into fetch_test values ('f1','f2','f3','f4','f5','f6','f7','f8')")
	assert (n, err)
	assert (type (n) == "number", "couldn't insert rows")
end
print ("table created; rows inserted")

-- default
local cur, err = conn:Execute ("select * from fetch_test")
assert (cur, err)
assert (cur:NumRows() == TOTAL_ROWS, "wrong number of rows")
local t1 = clock()
for i = 1, cur:NumRows() do
	local f1,f2,f3,f4,f5,f6,f7,f8 = cur:Fetch()
end
print ("default: ", clock() - t1)
assert (cur:Close () == 1, "couldn't close cursor object")

-- using the same table
local cur, err = conn:Execute ("select * from fetch_test")
assert (cur, err)
assert (cur:NumRows() == TOTAL_ROWS, "wrong number of rows")
t1 = clock()
local t = {}
for i = 1, cur:NumRows() do
	t = cur:Fetch (t)
end
print ("same table: ", clock() - t1)
assert (cur:Close () == 1, "couldn't close cursor object")

-- using the same table with alphanumeric keys
local cur, err = conn:Execute ("select * from fetch_test")
assert (cur, err)
assert (cur:NumRows() == TOTAL_ROWS, "wrong number of rows")
t1 = clock()
local t = {}
for i = 1, cur:NumRows() do
	t = cur:Fetch (t,"a")
end
print ("alpha keys: ", clock() - t1)
assert (cur:Close () == 1, "couldn't close cursor object")

-- using the same table with numeric and alphanumeric keys
local cur, err = conn:Execute ("select * from fetch_test")
assert (cur, err)
assert (cur:NumRows() == TOTAL_ROWS, "wrong number of rows")
t1 = clock()
local t = {}
for i = 1, cur:NumRows() do
	t = cur:Fetch (t,"an")
end
print ("all keys: ", clock() - t1)
assert (cur:Close () == 1, "couldn't close cursor object")

-- creating a table
local cur, err = conn:Execute ("select * from fetch_test")
assert (cur, err)
assert (cur:NumRows() == TOTAL_ROWS, "wrong number of rows")
t1 = clock()
for i = 1, cur:NumRows() do
	local t = cur:Fetch{}
end
print ("new table: ", clock() - t1)
assert (cur:Close () == 1, "couldn't close cursor object")

assert (conn:Close () == 1, "couldn't close connection object")
assert (env:Close () == 1, "couldn't close environment object")
