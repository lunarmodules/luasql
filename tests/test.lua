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

-- Build SQL command to insert a record at the table.
function insert_table (n, offset)
	local s = "insert into t ("
	for i = 1, n do
		s = s.."f"..i..", "
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
	local cur, err = conn:execute ("select * from t")
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

---------------------------------------------------------------------
---------------------------------------------------------------------

---------------------------------------------------------------------
-- basic checking test.
---------------------------------------------------------------------
function basic_test ()
	local err

	-- Check environment object.
	ENV, err = luasql[driver] ()
	assert (ENV, err)
	assert (ENV:close() == 1, "couldn't close environment")

	ENV, err = luasql[driver] () 
	assert (ENV, err)

	-- Check connection object.
	local conn, err = ENV:connect (datasource, username, password)
	assert (conn, (err or '').." ("..datasource..")")
	assert (conn:close() == 1, "couldn't close connection")

	conn, err = ENV:connect ("unknown-data-base")
	assert (conn == nil)
end

---------------------------------------------------------------------
-- Build SQL command to create the test table
---------------------------------------------------------------------
function define_table (n)
	local s = "create table t ("
	for i = 1, n do
		s = s.."f"..i.." varchar(30), "
	end
	return string.sub (s, 1, -3)..")"
end

---------------------------------------------------------------------
-- Create a table with TOTAL_FIELDS character fields.
---------------------------------------------------------------------
function create_table ()
	local err

	-- Check SQL statements.
	CONN, err = ENV:connect (datasource, username, password)
	assert (CONN, err)

	-- Create t.
	local cmd = define_table(TOTAL_FIELDS)
	local cur, err = CONN:execute (cmd)
	assert (cur == 0, err)
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function drop_table ()
	local cur, err = CONN:execute ("drop table t")
	assert (cur == 0, err)
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function rollback ()
	-- erase table
	local n, err = CONN:execute ("delete from t")
	assert (n == 0, err)
	CONN:setautocommit (false) -- begin transaction
	local n, err = CONN:execute ("insert into t (f1) values ('a')")
	assert (n == 1, err)
	CONN:rollback ()
	cur, err = CONN:execute ("select count(*) from t")
	assert (cur, err)
	n = tonumber (cur:fetch ())
	assert (n == 0, "Rollback fails")
	CONN:setautocommit (true)
end

---------------------------------------------------------------------
function assert2 (value, expected, msg)
	if not msg then
		msg = ''
	else
		msg = msg..'\n'
	end
	assert (value == expected,
		msg.."wrong value (["..tostring(value).."] instead of "..
		tostring(expected)..")")
end

---------------------------------------------------------------------
-- Fetch 2 values.
---------------------------------------------------------------------
function fetch2 ()
	local cur, err = CONN:execute ("insert into t (f1, f2) values ('b', 'c')")
	assert2 (cur, 1, err)
	cur, err = CONN:execute ("select f1, f2, f3 from t")
	assert (cur, err)
	CUR_TYPE = getmetatable (cur)

	local f1, f2, f3 = cur:fetch()
	assert2 (f1, 'b')
	assert2 (f2, 'c')
	assert2 (f3, nil)
	assert2 (cur:fetch(), nil)
	assert2 (cur:close(), 1, "couldn't close cursor")
	cur, err = CONN:execute ("insert into t (f1, f2) values ('d', 'e')")
	assert2 (cur, 1, err)
	cur, err = CONN:execute ("select f1, f2, f3 from t order by f1")
	assert (cur, err)
	local f1, f2, f3 = cur:fetch()
	assert2 (f1, 'b')
	assert2 (f2, 'c')
	assert2 (f3, nil)
	f1, f2, f3 = cur:fetch()
	assert2 (f1, 'd')
	assert2 (f2, 'e')
	assert2 (f3, nil)
	assert2 (cur:fetch(), nil)
	assert2 (cur:close(), 1, "couldn't close cursor")
	cur, err = CONN:execute ("delete from t where f1 in ('b', 'd')")
	assert2 (cur, 2, err)
end

---------------------------------------------------------------------
-- Test fetch with a new table, reusing a table and with different
-- indexing.
---------------------------------------------------------------------
function fetch_new_table ()
	-- insert elements.
	local cur, err = CONN:execute ("insert into t (f1, f2, f3, f4) values ('a', 'b', 'c', 'd')")
	assert2 (cur, 1, err)
	cur, err = CONN:execute ("insert into t (f1, f2, f3, f4) values ('f', 'g', 'h', 'i')")
	assert2 (cur, 1, err)
	-- retrieve data using a new table.
	cur, err = CONN:execute ("select f1, f2, f3, f4 from t")
	assert (cur, err)
	assert (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch{}
	assert (type(row), "table", err)
	assert2 (row[1], 'a')
	assert2 (row[2], 'b')
	assert2 (row[3], 'c')
	assert2 (row[4], 'd')
	assert2 (row.f1, nil)
	assert2 (row.f2, nil)
	assert2 (row.f3, nil)
	assert2 (row.f4, nil)
	row, err = cur:fetch{}
	assert (type(row), "table", err)
	assert2 (row[1], 'f')
	assert2 (row[2], 'g')
	assert2 (row[3], 'h')
	assert2 (row[4], 'i')
	assert2 (row.f1, nil)
	assert2 (row.f2, nil)
	assert2 (row.f3, nil)
	assert2 (row.f4, nil)
	assert2 (cur:fetch(), nil)
	assert2 (cur:close(), 1, "couldn't close cursor")

	-- retrieve data reusing the same table.
	io.write ("reusing a table...")
	cur, err = CONN:execute ("select f1, f2, f3, f4 from t")
	assert (cur, err)
	assert (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch{}
	assert (type(row), "table", err)
	assert2 (row[1], 'a')
	assert2 (row[2], 'b')
	assert2 (row[3], 'c')
	assert2 (row[4], 'd')
	assert2 (row.f1, nil)
	assert2 (row.f2, nil)
	assert2 (row.f3, nil)
	assert2 (row.f4, nil)
	row, err = cur:fetch (row)
	assert (type(row), "table", err)
	assert2 (row[1], 'f')
	assert2 (row[2], 'g')
	assert2 (row[3], 'h')
	assert2 (row[4], 'i')
	assert2 (row.f1, nil)
	assert2 (row.f2, nil)
	assert2 (row.f3, nil)
	assert2 (row.f4, nil)
	assert2 (cur:fetch{}, nil)
	assert2 (cur:close(), 1, "couldn't close cursor")

	-- retrieve data reusing the same table with alphabetic indexes.
	io.write ("with alpha keys...")
	cur, err = CONN:execute ("select f1, f2, f3, f4 from t")
	assert (cur, err)
	assert (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch ({}, "a")
	assert (type(row), "table", err)
	assert2 (row[1], nil)
	assert2 (row[2], nil)
	assert2 (row[3], nil)
	assert2 (row[4], nil)
	assert2 (row.f1, 'a')
	assert2 (row.f2, 'b')
	assert2 (row.f3, 'c')
	assert2 (row.f4, 'd')
	row, err = cur:fetch (row, "a")
	assert (type(row), "table", err)
	assert2 (row[1], nil)
	assert2 (row[2], nil)
	assert2 (row[3], nil)
	assert2 (row[4], nil)
	assert2 (row.f1, 'f')
	assert2 (row.f2, 'g')
	assert2 (row.f3, 'h')
	assert2 (row.f4, 'i')
	assert2 (cur:fetch(row, "a"), nil)
	assert2 (cur:close(), 1, "couldn't close cursor")

	-- retrieve data reusing the same table with both indexes.
	io.write ("with both keys...")
	cur, err = CONN:execute ("select f1, f2, f3, f4 from t")
	assert (cur, err)
	assert (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch ({}, "an")
	assert (type(row), "table", err)
	assert2 (row[1], 'a')
	assert2 (row[2], 'b')
	assert2 (row[3], 'c')
	assert2 (row[4], 'd')
	assert2 (row.f1, 'a')
	assert2 (row.f2, 'b')
	assert2 (row.f3, 'c')
	assert2 (row.f4, 'd')
	row, err = cur:fetch (row, "an")
	assert (type(row), "table", err)
	assert2 (row[1], 'f')
	assert2 (row[2], 'g')
	assert2 (row[3], 'h')
	assert2 (row[4], 'i')
	assert2 (row.f1, 'f')
	assert2 (row.f2, 'g')
	assert2 (row.f3, 'h')
	assert2 (row.f4, 'i')
	assert2 (cur:fetch(row, "an"), nil)
	assert2 (cur:close(), 1, "couldn't close cursor")
	-- clean the table.
	cur, err = CONN:execute ("delete from t where f1 in ('a', 'f')")
	assert2 (cur, 2, err)
end

---------------------------------------------------------------------
-- Get column names and types.
---------------------------------------------------------------------
function column_info ()
	-- insert elements.
	local cur, err = CONN:execute ("insert into t (f1, f2, f3, f4) values ('a', 'b', 'c', 'd')")
	assert2 (cur, 1, err)
	cur, err = CONN:execute ("select * from t")
	assert (cur, err)
	assert2 (getmetatable(cur), CUR_TYPE, "incorrect metatable")
	local names, types = cur:getcolnames(), cur:getcoltypes()

	assert2 (cur:close(), 1, "couldn't close cursor")
	-- clean the table.
	cur, err = CONN:execute ("delete from t where f1 = 'a'")
	assert2 (cur, 2, err)
end

--[[
-- Column Information.
cur, err = CONN:execute ("select count(*) as c from t where f1 like '%b%'")
assert (getmetatable(cur) == CUR_TYPE, err)
local num_rows = tonumber (cur:fetch())
assert (cur:close() == 1, "couldn't close cursor object")
cur, err = CONN:execute ("select f1, f2, f3 from t where f1 like '%b%'")
assert (getmetatable(cur) == CUR_TYPE, err)
local info, err = cur:colinfo ()
assert (type(info) == "table", err)
for i,v in info do print (i,v) end
cur:close ()
--]]

---------------------------------------------------------------------
tests = {
	{ "basic checking", basic_test },
	{ "create table", create_table },
	{ "fetch two values", fetch2 },
	{ "fetch new table", fetch_new_table },
	{ "rollback", rollback },
	{ "get column information", column_info },
	{ "drop table", drop_table },
}

---------------------------------------------------------------------
-- Main
---------------------------------------------------------------------

if type(arg[1]) ~= "string" then
	print (string.format ("Usage %s <driver> [<data source> [, <user> [, <password>]]]", arg[0]))
	os.exit()
end

driver = arg[1]
datasource = arg[2] or "luasql-test"
username = arg[3] or nil
password = arg[4] or nil

require (arg[1])
assert (luasql, "no luasql table")

for i = 1, table.getn (tests) do
	local t = tests[i]
	io.write (t[1].." ...")
	t[2] ()
	io.write (" OK !\n")
end

