#!/usr/local/bin/lua
-- See Copyright Notice in license.html

TOTAL_FIELDS = 400
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
-- object test.
---------------------------------------------------------------------
function test_object (obj, objmethods)
	-- checking object type.
	assert2 ("userdata", type(obj), "incorrect object type")
	-- trying to get metatable.
	assert2 ("LuaSQL: you're not allowed to get this metatable",
		getmetatable(obj), "error permitting access to object's metatable")
	-- trying to set metatable.
	assert2 (false, pcall (setmetatable, ENV, {}))
	-- checking existence of object's methods.
	for i = 1, table.getn (objmethods) do
		local method = obj[objmethods[i]]
		assert2 ("function", type(method))
		assert2 (false, pcall (method), "no 'self' parameter accepted")
	end
	return obj
end

ENV_OK = function (obj)
	return test_object (obj, { "close", "connect", })
end
CONN_OK = function (obj)
	return test_object (obj, { "close", "commit", "execute", "rollback", "setautocommit", })
end
CUR_OK = function (obj)
	return test_object (obj, { "close", "fetch", "getcolnames", "getcoltypes", })
end

---------------------------------------------------------------------
-- basic checking test.
---------------------------------------------------------------------
function basic_test ()
	-- Check environment object.
	ENV = ENV_OK (luasql[driver] ())
	assert2 (1, ENV:close(), "couldn't close environment")
	-- trying to connect with a closed environment.
	assert2 (false, pcall (ENV.connect, ENV, datasource, username, password),
		"error connecting with a closed environment")
	-- it's ok to close a closed object, but nil is returned instead of 1.
	assert2 (nil, ENV:close())
	-- Reopen the environment.
	ENV = ENV_OK (luasql[driver] ())
	-- Check connection object.
	local conn, err = ENV:connect (datasource, username, password)
	assert (conn, (err or '').." ("..datasource..")")
	CONN_OK (conn)
	assert2 (1, conn:close(), "couldn't close connection")
	-- trying to execute a statement with a closed connection.
	assert2 (false, pcall (conn.execute, conn, "create table x (c char)"),
		"error connecting with a closed environment")
	-- it's ok to close a closed object, but nil is returned instead of 1.
	assert2 (nil, conn:close())
	-- Check error situation.
	assert2 (nil, ENV:connect ("unknown-data-base"), "this should be an error")
end

---------------------------------------------------------------------
-- Build SQL command to create the test table.
---------------------------------------------------------------------
function define_table (n)
    local s = "create table t ("
    for i = 1, n do
        s = s.."f"..i.." varchar(30), "
    end
	s = string.sub (s, 1, -3)
	if driver == "mysql" then 
	    return s..") TYPE = InnoDB;"
	else
		return s
	end
end


---------------------------------------------------------------------
-- Create a table with TOTAL_FIELDS character fields.
---------------------------------------------------------------------
function create_table ()
	-- Check SQL statements.
	CONN = CONN_OK (ENV:connect (datasource, username, password))
	-- Create t.
	local cmd = define_table(TOTAL_FIELDS)
	-- Postgres retorna 0, enquanto ODBC retorna -1.
	assert (CONN:execute (cmd))
end

---------------------------------------------------------------------
-- Fetch 2 values.
---------------------------------------------------------------------
function fetch2 ()
	-- insert a record.
	assert2 (1, CONN:execute ("insert into t (f1, f2) values ('b', 'c')"))
	-- retrieve data.
	local cur = CUR_OK (CONN:execute ("select f1, f2, f3 from t"))
	-- check data.
	local f1, f2, f3 = cur:fetch()
	assert2 ('b', f1)
	assert2 ('c', f2)
	assert2 (nil, f3)
	assert2 (nil, cur:fetch())
	assert2 (1, cur:close(), "couldn't close cursor")
	-- insert a second record.
	assert2 (1, CONN:execute ("insert into t (f1, f2) values ('d', 'e')"))
	cur = CUR_OK (CONN:execute ("select f1, f2, f3 from t order by f1"))
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
	-- remove records.
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
	local cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
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
	cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
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
	cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
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
	cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
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
	CONN:setautocommit (false) -- == begin transaction
	-- insert a record and commit the operation.
	assert2 (1, CONN:execute ("insert into t (f1) values ('a')"))
	local cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber (cur:fetch ()), "Insert failed")
	assert2 (1, cur:close(), "couldn't close cursor")
	CONN:commit ()
	-- insert a record and roll back the operation.
	assert2 (1, CONN:execute ("insert into t (f1) values ('b')"))
	local cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (2, tonumber (cur:fetch ()), "Insert failed")
	assert2 (1, cur:close(), "couldn't close cursor")
	CONN:rollback ()
	-- check resulting table with one record.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber(cur:fetch()), "Rollback failed")
	assert2 (1, cur:close(), "couldn't close cursor")
	-- delete a record and roll back the operation.
	assert2 (1, CONN:execute ("delete from t where f1 = 'a'"))
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber(cur:fetch()))
	assert2 (1, cur:close(), "couldn't close cursor")
	CONN:rollback ()
	-- check resulting table with one record.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber(cur:fetch()), "Rollback failed")
	assert2 (1, cur:close(), "couldn't close cursor")
--[[
	-- insert a second record and turn on the auto-commit mode.
	-- this will produce a rollback on PostgreSQL and a commit on ODBC.
	-- what to do?
	assert2 (1, CONN:execute ("insert into t (f1) values ('b')"))
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (2, tonumber (cur:fetch ()), "Insert failed")
	assert2 (1, cur:close(), "couldn't close cursor")
	CONN:setautocommit (true)
	-- check resulting table with one record.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber(cur:fetch()), "Rollback failed")
	assert2 (1, cur:close(), "couldn't close cursor")
--]]
	-- clean the table.
	assert2 (1, CONN:execute ("delete from t"))
	CONN:commit ()
	CONN:setautocommit (true)
	-- check resulting table with no records.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber(cur:fetch()), "Rollback failed")
	assert2 (1, cur:close(), "couldn't close cursor")
end

---------------------------------------------------------------------
-- Get column names and types.
---------------------------------------------------------------------
function column_info ()
	-- insert elements.
	assert2 (1, CONN:execute ("insert into t (f1, f2, f3, f4) values ('a', 'b', 'c', 'd')"))
	local cur = CUR_OK (CONN:execute ("select f1,f2,f3,f4 from t"))
	-- get column information.
	local names, types = cur:getcolnames(), cur:getcoltypes()
	assert2 ("table", type(names), "getcolnames failed")
	assert2 ("table", type(types), "getcoltypes failed")
	assert2 (4, table.getn(names), "incorrect column names table")
	assert2 (4, table.getn(types), "incorrect column types table")
	for i = 1, table.getn(names) do
		assert2 ("f"..i, names[i], "incorrect column names table")
		local type_i = types[i]
		assert (type_i == "varchar (30)" or type_i == "string" or type_i == "string(30)", "incorrect column types table")
	end
	-- check if the tables are being reused.
	local n2, t2 = cur:getcolnames(), cur:getcoltypes()
	assert2 (names, n2, "getcolnames is rebuilding the table")
	assert2 (types, t2, "getcoltypes is rebuilding the table")
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

require (driver)
assert (luasql, "no luasql table")

for i = 1, table.getn (tests) do
	local t = tests[i]
	io.write (t[1].." ...")
	t[2] ()
	io.write (" OK !\n")
end

