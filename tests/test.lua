#!/usr/local/bin/lua

TOTAL_FIELDS = 40
TOTAL_ROWS = 40

---------------------------------------------------------------------
-- checks for a value and throw an error if it's not the expected.
---------------------------------------------------------------------
function assert2 (expected, value, msg)
	if not msg then
		msg = ''
	else
		msg = msg..'\n'
	end
	return assert (value == expected,
		msg.."wrong value (["..tostring(value).."] instead of "..
		tostring(expected)..")")
end

---------------------------------------------------------------------
-- basic checking test.
---------------------------------------------------------------------
function basic_test ()
	-- Check environment object.
	ENV = assert (luasql[driver] ())
	assert2 (1, ENV:close(), "couldn't close environment")
	-- Reopen the environment.
	ENV = assert (luasql[driver] ())
	-- Check connection object.
	local conn, err = ENV:connect (datasource, username, password)
	assert (conn, (err or '').." ("..datasource..")")
	assert2 (1, conn:close(), "couldn't close connection")
	-- Check error situation.
	assert2 (nil, ENV:connect ("unknown-data-base"), "this should be an error")
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
	-- Check SQL statements.
	CONN = assert (ENV:connect (datasource, username, password))
	-- Drop table (if it already exists).
	--CONN:execute("drop table t")
	-- Create t.
	local cmd = define_table(TOTAL_FIELDS)
	-- Postgres retorna 0, enquanto ODBC retorna -1.
	--assert (cur == 0, err)
	assert (CONN:execute (cmd))
end

---------------------------------------------------------------------
-- Fetch 2 values.
---------------------------------------------------------------------
function fetch2 ()
	-- insert a record.
	assert2 (1, CONN:execute ("insert into t (f1, f2) values ('b', 'c')"))
	-- retrieve data.
	local cur = assert (CONN:execute ("select f1, f2, f3 from t"))
	assert2 ("userdata", type(cur), "not a cursor")
	-- get cursor metatable.
	CUR_TYPE = getmetatable (cur)
	-- check data.
	local f1, f2, f3 = cur:fetch()
	assert2 ('b', f1)
	assert2 ('c', f2)
	assert2 (nil, f3)
	assert2 (nil, cur:fetch())
	assert2 (1, cur:close(), "couldn't close cursor")
	cur, err = CONN:execute ("insert into t (f1, f2) values ('d', 'e')")
	assert2 (1, cur, err)
	cur = assert (CONN:execute ("select f1, f2, f3 from t order by f1"))
	local f1, f2, f3 = cur:fetch()
	assert2 ('b', f1, f2)	-- f2 can be an error message
	assert2 ('c', f2)
	assert2 (nil, f3)
	f1, f2, f3 = cur:fetch()
	assert2 ('d', f1, f2)	-- f2 can be an error message
	assert2 ('e', f2)
	assert2 (nil, f3)
	assert2 (nil, cur:fetch())
	assert2 (1, cur:close(), "couldn't close cursor")
	assert2 (2, CONN:execute ("delete from t where f1 in ('b', 'd')"))
end

---------------------------------------------------------------------
-- Test fetch with a new table, reusing a table and with different
-- indexing.
---------------------------------------------------------------------
function fetch_new_table ()
	-- insert elements.
	assert2 (1, CONN:execute ("insert into t (f1, f2, f3, f4) values ('a', 'b', 'c', 'd')"))
	assert2 (1, CONN:execute ("insert into t (f1, f2, f3, f4) values ('f', 'g', 'h', 'i')"))
	-- retrieve data using a new table.
	local cur = assert (CONN:execute ("select f1, f2, f3, f4 from t"))
	assert2 (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch{}
	assert2 (type(row), "table", err)
	assert2 ('a', row[1])
	assert2 ('b', row[2])
	assert2 ('c', row[3])
	assert2 ('d', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	row, err = cur:fetch{}
	assert (type(row), "table", err)
	assert2 ('f', row[1])
	assert2 ('g', row[2])
	assert2 ('h', row[3])
	assert2 ('i', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	assert2 (nil, cur:fetch())
	assert2 (1, cur:close(), "couldn't close cursor")

	-- retrieve data reusing the same table.
	io.write ("reusing a table...")
	cur = assert (CONN:execute ("select f1, f2, f3, f4 from t"))
	assert2 (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch{}
	assert (type(row), "table", err)
	assert2 ('a', row[1])
	assert2 ('b', row[2])
	assert2 ('c', row[3])
	assert2 ('d', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	row, err = cur:fetch (row)
	assert (type(row), "table", err)
	assert2 ('f', row[1])
	assert2 ('g', row[2])
	assert2 ('h', row[3])
	assert2 ('i', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	assert2 (nil, cur:fetch{})
	assert2 (1, cur:close(), "couldn't close cursor")

	-- retrieve data reusing the same table with alphabetic indexes.
	io.write ("with alpha keys...")
	cur = assert (CONN:execute ("select f1, f2, f3, f4 from t"))
	assert2 (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch ({}, "a")
	assert (type(row), "table", err)
	assert2 (nil, row[1])
	assert2 (nil, row[2])
	assert2 (nil, row[3])
	assert2 (nil, row[4])
	assert2 ('a', row.f1)
	assert2 ('b', row.f2)
	assert2 ('c', row.f3)
	assert2 ('d', row.f4)
	row, err = cur:fetch (row, "a")
	assert2 (type(row), "table", err)
	assert2 (nil, row[1])
	assert2 (nil, row[2])
	assert2 (nil, row[3])
	assert2 (nil, row[4])
	assert2 ('f', row.f1)
	assert2 ('g', row.f2)
	assert2 ('h', row.f3)
	assert2 ('i', row.f4)
	assert2 (nil, cur:fetch(row, "a"))
	assert2 (1, cur:close(), "couldn't close cursor")

	-- retrieve data reusing the same table with both indexes.
	io.write ("with both keys...")
	cur = assert (CONN:execute ("select f1, f2, f3, f4 from t"))
	assert (getmetatable (cur), CUR_TYPE, "incorrect metatable")
	local row, err = cur:fetch ({}, "an")
	assert (type(row), "table", err)
	assert2 ('a', row[1])
	assert2 ('b', row[2])
	assert2 ('c', row[3])
	assert2 ('d', row[4])
	assert2 ('a', row.f1)
	assert2 ('b', row.f2)
	assert2 ('c', row.f3)
	assert2 ('d', row.f4)
	row, err = cur:fetch (row, "an")
	assert (type(row), "table", err)
	assert2 ('f', row[1])
	assert2 ('g', row[2])
	assert2 ('h', row[3])
	assert2 ('i', row[4])
	assert2 ('f', row.f1)
	assert2 ('g', row.f2)
	assert2 ('h', row.f3)
	assert2 ('i', row.f4)
	assert2 (nil, cur:fetch(row, "an"))
	assert2 (1, cur:close(), "couldn't close cursor")
	-- clean the table.
	assert2 (2, CONN:execute ("delete from t where f1 in ('a', 'f')"))
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function rollback ()
	CONN:setautocommit (false) -- begin transaction
	assert2 (1, CONN:execute ("insert into t (f1) values ('a')"))
	local cur = assert (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber (cur:fetch ()), "Insert failed")
	assert2 (1, cur:close(), "couldn't close cursor")
	CONN:rollback ()
	cur = assert (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber(cur:fetch()), "Rollback failed")
	assert2 (1, cur:close(), "couldn't close cursor")
	CONN:setautocommit (true)
end

---------------------------------------------------------------------
-- Get column names and types.
---------------------------------------------------------------------
function column_info ()
	-- insert elements.
	assert2 (1, CONN:execute ("insert into t (f1, f2, f3, f4) values ('a', 'b', 'c', 'd')"))
	local cur = assert (CONN:execute ("select f1,f2,f3,f4 from t"))
	assert2 (CUR_TYPE, getmetatable(cur), "incorrect metatable")
	local names, types = cur:getcolnames(), cur:getcoltypes()
	assert2 ("table", type(names), "getcolnames failed")
	assert2 ("table", type(types), "getcoltypes failed")
	assert2 (4, table.getn(names), "incorrect column names table")
	assert2 (4, table.getn(types), "incorrect column types table")
	for i = 1, table.getn(names) do
		assert2 ("f"..i, names[i], "incorrect column names table")
		--assert2 ("string", types[i], "incorrect column types table")
		local type_i = types[i]
		assert (type_i == "varchar (30)" or type_i == "string", "incorrect column types table")
	end
	assert2 (1, cur:close(), "couldn't close cursor")
	-- clean the table.
	assert2 (1, CONN:execute ("delete from t where f1 = 'a'"))
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function drop_table ()
	-- Postgres retorna 0, enquanto ODBC retorna -1.
	assert (CONN:execute ("drop table t"))
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function close_conn ()
	assert (1, CONN:close())
	assert (1, ENV:close())
end


---------------------------------------------------------------------
tests = {
	{ "basic checking", basic_test },
	{ "create table", create_table },
	{ "fetch two values", fetch2 },
	{ "fetch new table", fetch_new_table },
	{ "rollback", rollback },
	{ "get column information", column_info },
	{ "drop table", drop_table },
	{ "close connection", close_conn },
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

