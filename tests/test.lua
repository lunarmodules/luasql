#!/usr/local/bin/lua.5.0b

TOTAL_FIELDS = 40
TOTAL_ROWS = 40

-- 4.0 and 5.0 compatibility
if not math then
	gettype = tag
	off = "false"
else
	mod = math.mod
	floor = math.floor
	strchar = string.char
	strsub = string.sub
	strbyte = string.byte
	gettype = getmetatable
	off = false
end

-- Create a string based on a number.
function toword (n)
	local s = ""
	repeat
		local c = mod (n, 26)
		s = strchar (strbyte('a')+c)..s
		n = floor (n / 26)
	until n == 0
	return s
end

-- Build SQL command to create the test table
function define_table (n)
	local s = "create table test_table ( id serial"
	for i = 1, n do
		s = s..", field"..i.." varchar(30)"
	end
	return s..")"
end

-- Build SQL command to insert a record at the table.
function insert_table (n, offset)
	local s = "insert into test_table ("
	for i = 1, n do
		s = s.."field"..i..", "
	end
	s = strsub(s,1,-3)..") values ("
	for i = 1, n do
		s = s.."'"..toword(offset + i).."', "
	end
	return strsub(s,1,-3)..")"
end

function T (...)
	if arg[1] then
		return arg
	else
		return arg[1], arg[2]
	end
end

-- Execute a select-statement and read all resulting rows.
function fetch_test (conn, tab, mode)
	local cur, err = conn:Execute ("select * from test_table order by id")
	assert (cur, err)
	if mode then
		linha, err = cur:Fetch (tab, mode)
	else
		linha, err = T (cur:Fetch ())
	end
	assert (linha, err)
	while linha do
		assert (type(linha) == "table", "not a table")
		if mode then
			linha, err = cur:Fetch (tab, mode)
		else
			linha, err = T (cur:Fetch ())
		end
	end
	assert (cur:Close ())
end

-- Main --

if not sql and loadlib then
	loadlib ("libluasqlpostgres.5.0.dylib","luasql_libopen_postgres") ()
end
assert (sql, "no sql table")
--assert (type(sql.Open)=="function", "no Open method")

-- Check environment object.
--local env, err = sql:Open("postgres")
local env, err = sql.postgres ()
assert (env, err)
local env_type = gettype(env)
assert (env:Close() == 1, "couldn't close environment")

env, err = sql.postgres () 
assert (env, err)

-- Check connection object.
local conn, err = env:Connect ("luasql-test") 
assert (conn, err)
local conn_type = gettype(conn)
assert (conn:Close() == 1, "couldn't close connection")

conn, err = env:Connect ("unknown-data-base")
assert (conn == nil)

-- Check SQL statements.
conn, err = env:Connect ("luasql-test")
assert (conn, err)

-- Create test_table.
conn:Execute ("drop table test_table")
conn:Execute ("drop sequence test_table_id_seq")
cur, err = conn:Execute (define_table(TOTAL_FIELDS))
assert (type(cur) == "number", err)

-- Test transaction.
conn:SetAutoCommit (off)		-- begin transaction
local n, err = conn:Execute ("insert into test_table (field1) values ('a')")
assert (n == 1, err)
conn:Rollback ()
cur, err = conn:Execute ("select count(*) from test_table")
assert (cur, err)
n = tonumber (cur:Fetch ())
assert (n == 0, "Error on Rollback")
---!!!!!!!!!!!!!!!!!!!!!!!!!!!

-- Test 1 value.
cur, err = conn:Execute ("insert into test_table (field1) values ('a')")
assert (cur == 1, err)
cur, err = conn:Execute ("select field1 from test_table")
assert (cur, err)
local cur_type = gettype(cur)
assert (cur:Fetch() == 'a', "wrong value")
assert (cur:Fetch() == nil, "wrong number of rows")
assert (cur:Close () == 1, "couldn't close cursor")
cur, err = conn:Execute ("select field1 from test_table where field1 != 'a'")
assert (cur, err)
assert (cur:Fetch() == nil)
cur:Close ()
cur, err = conn:Execute ("delete from test_table where field1 = 'a'")
assert (cur == 1, err)
print ("insert value ok")

-- Test 2 values.
cur, err = conn:Execute (
	"insert into test_table (field1, field2) values ('b', 'c')")
assert (cur == 1, err)
cur, err = conn:Execute ("select field1, field2, field3 from test_table")
assert (cur, err)
local f1, f2, f3 = cur:Fetch()
assert (f1 == 'b')
assert (f2 == 'c')
assert (f3 == nil)
assert (cur:Fetch() == nil)
cur:Close()
cur, err = conn:Execute (
	"insert into test_table (field1, field2) values ('d', 'e')")
assert (cur == 1, err)
cur, err = conn:Execute (
	"select field1, field2, field3 from test_table order by id")
assert (cur, err)
local f1, f2, f3 = cur:Fetch()
assert (f1 == 'b')
assert (f2 == 'c')
assert (f3 == nil)
f1, f2, f3 = cur:Fetch()
assert (f1 == 'd')
assert (f2 == 'e')
assert (f3 == nil)
assert (cur:Fetch() == nil)
cur:Close()
cur, err = conn:Execute ("delete from test_table where field1 = 'b' or field1 = 'd'")
assert (cur == 2, err)
print ("insert 2 values ok")

-- Insert rows.
for i = 1, TOTAL_ROWS do
	local cmd = insert_table (TOTAL_FIELDS, i)
	cur, err = conn:Execute (cmd)
	--assert (type(cur) == "number", err)
	if cur ~= 1 then
		print (format ("sql: %s\n", cmd))
		error (err)
	end
end
print ("insert "..TOTAL_ROWS.." rows with "..TOTAL_FIELDS.." fields each OK")

fetch_test (conn)
print("fetch default OK")
fetch_test (conn, {})
print("fetch {} OK")
fetch_test (conn, {}, "n")
print("fetch {} n OK")
fetch_test (conn, {}, "s")
print("fetch {} s OK")

cur, err = conn:Execute ("delete from test_table")
assert (cur == TOTAL_ROWS, err)
print ("delete "..TOTAL_ROWS.." OK")

-- Column Information.
cur, err = conn:Execute ("select count(*) as c from test_table where field1 like '%b%'")
assert (gettype(cur) == cur_type, err)
local num_rows = tonumber (cur:Fetch())
assert (cur:Close() == 1, "couldn't close cursor object")
cur, err = conn:Execute ("select field1, field2, field3 from test_table where field1 like '%b%'")
assert (gettype(cur) == cur_type, err)
assert ((cur:NumRows() == num_rows), "wrong number of total rows"..num_rows..cur:NumRows())
local info, err = cur:ColInfo ()
assert (type(info) == "table", err)
for i,v in info do print (i,v) end
