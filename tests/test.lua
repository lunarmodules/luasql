#!/usr/local/bin/lua

TOTAL_FIELDS = 40
TOTAL_ROWS = 40

-- Create a string based on a number.
function toword (n)
	local s = ""
	repeat
		local c = math.mod (n, 26)
		s = string.char (string.byte('a')+c)..s
		n = math.floor (n / 26)
	until n == 0
	return s
end

-- Build SQL command to create the test table
function define_table (n)
	local s = "create table test_table ("
	for i = 1, n do
		s = s.."field"..i.." varchar(30), "
	end
	return string.sub (s, 1, -3)..")"
end

-- Build SQL command to insert a record at the table.
function insert_table (n, offset)
	local s = "insert into test_table ("
	for i = 1, n do
		s = s.."field"..i..", "
	end
	s = string.sub(s,1,-3)..") values ("
	for i = 1, n do
		s = s.."'"..toword(offset + i).."', "
	end
	return string.sub(s,1,-3)..")"
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
	local cur, err = conn:execute ("select * from test_table")
	assert (cur, err)
	if mode then
		linha, err = cur:fetch (tab, mode)
	else
		linha, err = T (cur:fetch ())
	end
	assert (linha, err)
	while linha do
		assert (type(linha) == "table", "not a table")
		if mode then
			linha, err = cur:fetch (tab, mode)
		else
			linha, err = T (cur:fetch ())
		end
	end
	assert (cur:close ())
end

-- Main --

if type(arg[1]) ~= "string" then
	print (string.format ("Usage %s <driver> [<data source> [, <user> [, <password>]]]", arg[0]))
	os.exit()
end

local driver = arg[1]
local datasouce = arg[2] or "luasql-test"
local username = arg[3] or nil
local password = arg[4] or nil

require (arg[1])
assert (luasql, "no luasql table")

-- Check environment object.
local env, err = luasql[driver] ()
assert (env, err)
local env_type = getmetatable(env)
assert (env:close() == 1, "couldn't close environment")

env, err = luasql[driver] () 
assert (env, err)

-- Check connection object.
local conn, err = env:connect (datasource, username, password)
assert (conn, err)
local conn_type = getmetatable(conn)
assert (conn:close() == 1, "couldn't close connection")

conn, err = env:connect ("unknown-data-base")
assert (conn == nil)

-- Check SQL statements.
conn, err = env:connect (datasource, username, password)
assert (conn, err)

-- Create test_table.
conn:execute ("drop table test_table")
local cmd = define_table(TOTAL_FIELDS)
cur, err = conn:execute (cmd)
assert (type(cur) == "number", err)

-- Test transaction.
conn:setautocommit (false)		-- begin transaction
local n, err = conn:execute ("insert into test_table (field1) values ('a')")
assert (n == 1, err)
conn:rollback ()
cur, err = conn:execute ("select count(*) from test_table")
assert (cur, err)
n = tonumber (cur:fetch ())
assert (n == 0, "Error on Rollback")
---!!!!!!!!!!!!!!!!!!!!!!!!!!!

-- Test 1 value.
cur, err = conn:execute ("insert into test_table (field1) values ('a')")
assert (cur == 1, err)
cur, err = conn:execute ("select field1 from test_table")
assert (cur, err)
local cur_type = getmetatable(cur)
assert (cur:fetch() == 'a', "wrong value")
assert (cur:fetch() == nil, "wrong number of rows")
assert (cur:close () == 1, "couldn't close cursor")
cur, err = conn:execute ("select field1 from test_table where field1 <> 'a'")
assert (cur, err)
assert (cur:fetch() == nil)
cur:close ()
cur, err = conn:execute ("delete from test_table where field1 = 'a'")
assert (cur == 1, err)
print ("insert value ok")

-- Test 2 values.
cur, err = conn:execute (
	"insert into test_table (field1, field2) values ('b', 'c')")
assert (cur == 1, err)
cur, err = conn:execute ("select field1, field2, field3 from test_table")
assert (cur, err)
local f1, f2, f3 = cur:fetch()
assert (f1 == 'b')
assert (f2 == 'c')
assert (f3 == nil)
assert (cur:fetch() == nil)
cur:close()
cur, err = conn:execute (
	"insert into test_table (field1, field2) values ('d', 'e')")
assert (cur == 1, err)
cur, err = conn:execute (
	"select field1, field2, field3 from test_table order by field1")
assert (cur, err)
local f1, f2, f3 = cur:fetch()
assert (f1 == 'b')
assert (f2 == 'c')
assert (f3 == nil)
f1, f2, f3 = cur:fetch()
assert (f1 == 'd')
assert (f2 == 'e')
assert (f3 == nil)
assert (cur:fetch() == nil)
cur:close()
cur, err = conn:execute ("delete from test_table where field1 = 'b' or field1 = 'd'")
assert (cur == 2, err)
print ("insert 2 values ok")

-- Insert rows.
for i = 1, TOTAL_ROWS do
	local cmd = insert_table (TOTAL_FIELDS, i)
	cur, err = conn:execute (cmd)
	if cur ~= 1 then
		print (format ("luasql: %s\n", cmd))
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

cur, err = conn:execute ("delete from test_table")
assert (cur == TOTAL_ROWS, err)
print ("delete "..TOTAL_ROWS.." OK")

-- Column Information.
cur, err = conn:execute ("select count(*) as c from test_table where field1 like '%b%'")
assert (getmetatable(cur) == cur_type, err)
local num_rows = tonumber (cur:fetch())
assert (cur:close() == 1, "couldn't close cursor object")
cur, err = conn:execute ("select field1, field2, field3 from test_table where field1 like '%b%'")
assert (getmetatable(cur) == cur_type, err)
local info, err = cur:colinfo ()
assert (type(info) == "table", err)
for i,v in info do print (i,v) end
