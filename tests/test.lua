#!/usr/local/bin/lua5.1
-- See Copyright Notice in license.html
-- $Id: test.lua,v 1.52 2008/06/30 10:43:03 blumf Exp $

TOTAL_FIELDS = 40
TOTAL_ROWS = 40 --unused

DEFINITION_STRING_TYPE_NAME = "text"
QUERYING_STRING_TYPE_NAME = "text"

CREATE_TABLE_RETURN_VALUE = 0
DROP_TABLE_RETURN_VALUE = 0

MSG_CURSOR_NOT_CLOSED = "cursor was not automatically closed by fetch"

CHECK_GETCOL_INFO_TABLES = true

---------------------------------------------------------------------
if not string.find(_VERSION, " 5.0") then
	table.getn = assert((loadstring or load)[[return function (t) return #t end]])()
end

---------------------------------------------------------------------
-- Creates a table that can handle differing capitalization of field
-- names
-- @return A table with altered metatable
---------------------------------------------------------------------
local mt = {
	__index = function(t, i)
		if type(i) == "string" then
			return rawget(t, string.upper(i)) or rawget(t, string.lower(i))
		end

		return rawget(t, i)
	end
}
function fetch_table ()
	return setmetatable({}, mt)
end

---------------------------------------------------------------------
-- Produces a SQL statement which completely erases a table.
-- @param table_name String with the name of the table.
-- @return String with SQL statement.
---------------------------------------------------------------------
function sql_erase_table (table_name)
	return string.format ("delete from %s", table_name)
end

---------------------------------------------------------------------
-- checks for a value and throw an error if it is invalid.
---------------------------------------------------------------------
function assert2 (expected, value, msg)
	if not msg then
		msg = ''
	else
		msg = msg..'\n'
	end
	return assert (value == expected,
		msg.."wrong value ("..tostring(value).." instead of "..
		tostring(expected)..")")
end

---------------------------------------------------------------------
-- Shallow compare of two tables
---------------------------------------------------------------------
function table_compare(t1, t2)
	if t1 == t2 then return true; end

	for i, v in pairs(t1) do
		if t2[i] ~= v then return false; end
	end

	for i, v in pairs(t2) do
		if t1[i] ~= v then return false; end
	end

	return true
end

---------------------------------------------------------------------
-- object test.
---------------------------------------------------------------------
function test_object (obj, objmethods)
	-- checking object type.
	assert2 (true, type(obj) == "userdata" or type(obj) == "table", "incorrect object type")

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

ENV_METHODS = { "close", "connect", }
ENV_OK = function (obj)
	return test_object (obj, ENV_METHODS)
end
CONN_METHODS = { "close", "commit", "execute", "rollback", "setautocommit", }
CONN_OK = function (obj)
	return test_object (obj, CONN_METHODS)
end
CUR_METHODS = { "close", "fetch", "getcolnames", "getcoltypes", }
CUR_OK = function (obj)
	return test_object (obj, CUR_METHODS)
end

function checkUnknownDatabase(ENV)
	assert2 (nil, ENV:connect ("/unknown-data-base"), "this should be an error")
end

---------------------------------------------------------------------
-- basic checking test.
---------------------------------------------------------------------
function basic_test ()
	-- Check environment object.
	ENV = ENV_OK (luasql[driver] ())
	assert2 (true, ENV:close(), "couldn't close environment")
	-- trying to connect with a closed environment.
	assert2 (false, pcall (ENV.connect, ENV, datasource, username, password),
		"error connecting with a closed environment")
	-- it is ok to close a closed object, but false is returned instead of true.
	assert2 (false, ENV:close())
	-- Reopen the environment.
	ENV = ENV_OK (luasql[driver] ())

	-- Check connection object.
	local conn, err = ENV:connect (datasource, username, password)
	assert (conn, (err or '').." ("..datasource..")")
	CONN_OK (conn)
	assert2 (true, conn:close(), "couldn't close connection")
	-- trying to execute a statement with a closed connection.
	assert2 (false, pcall (conn.execute, conn, "create table x (c char)"),
		"error while executing through a closed connection")
	-- it is ok to close a closed object, but false is returned instead of true.
	assert2 (false, conn:close())
	-- Check error situation.
	checkUnknownDatabase(ENV)	

	-- force garbage collection
	local a = {}
	setmetatable(a, {__mode="v"})
	a.ENV = ENV_OK (luasql[driver] ())
	a.CONN = a.ENV:connect (datasource, username, password)
	collectgarbage ()
	collectgarbage ()
	assert2(nil, a.ENV, "environment not collected")
	assert2(nil, a.CONN, "connection not collected")
end

---------------------------------------------------------------------
-- Build SQL command to create the test table.
---------------------------------------------------------------------
function define_table (n)
	local t = {}
	for i = 1, n do
		table.insert (t, "f"..i.." "..DEFINITION_STRING_TYPE_NAME)
	end
	return "create table t ("..table.concat (t, ',')..")"
end


---------------------------------------------------------------------
-- Create a table with TOTAL_FIELDS character fields.
---------------------------------------------------------------------
function create_table ()
	-- Check SQL statements.
	CONN = CONN_OK (ENV:connect (datasource, username, password))
	CONN:execute"drop table t"
	-- Create t.
	local cmd = define_table(TOTAL_FIELDS)
	assert2 (CREATE_TABLE_RETURN_VALUE, CONN:execute (cmd))
end

---------------------------------------------------------------------
-- Fetch 2 values.
---------------------------------------------------------------------
function fetch2 ()
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
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
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	assert2 (false, cur:close())
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
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	assert2 (false, cur:close())
	-- remove records.
	assert2 (2, CONN:execute ("delete from t where f1 in ('b', 'd')"))
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
end

---------------------------------------------------------------------
-- Test fetch with a new table, reusing a table and with different
-- indexing.
---------------------------------------------------------------------
function fetch_new_table ()
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
	-- insert elements.
	assert2 (1, CONN:execute ("insert into t (f1, f2, f3, f4) values ('a', 'b', 'c', 'd')"))
	assert2 (1, CONN:execute ("insert into t (f1, f2, f3, f4) values ('f', 'g', 'h', 'i')"))
	-- retrieve data using a new table.
	local cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
	local row, err = cur:fetch(fetch_table())
	assert2 (type(row), "table", err)
	assert2 ('a', row[1])
	assert2 ('b', row[2])
	assert2 ('c', row[3])
	assert2 ('d', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	row, err = cur:fetch(fetch_table())
	assert2 (type(row), "table", err)
	assert2 ('f', row[1])
	assert2 ('g', row[2])
	assert2 ('h', row[3])
	assert2 ('i', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	assert2 (nil, cur:fetch{})
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	assert2 (false, cur:close())

	-- retrieve data reusing the same table.
	io.write ("reusing a table...")
	cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
	local row, err = cur:fetch(fetch_table())
	assert2 (type(row), "table", err)
	assert2 ('a', row[1])
	assert2 ('b', row[2])
	assert2 ('c', row[3])
	assert2 ('d', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	row, err = cur:fetch (row)
	assert2 (type(row), "table", err)
	assert2 ('f', row[1])
	assert2 ('g', row[2])
	assert2 ('h', row[3])
	assert2 ('i', row[4])
	assert2 (nil, row.f1)
	assert2 (nil, row.f2)
	assert2 (nil, row.f3)
	assert2 (nil, row.f4)
	assert2 (nil, cur:fetch(fetch_table()))
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	assert2 (false, cur:close())

	-- retrieve data reusing the same table with alphabetic indexes.
	io.write ("with alpha keys...")
	cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
	local row, err = cur:fetch (fetch_table(), "a")
	assert2 (type(row), "table", err)
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
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	assert2 (false, cur:close())

	-- retrieve data reusing the same table with both indexes.
	io.write ("with both keys...")
	cur = CUR_OK (CONN:execute ("select f1, f2, f3, f4 from t order by f1"))
	local row, err = cur:fetch (fetch_table(), "an")
	assert2 (type(row), "table", err)
	assert2 ('a', row[1])
	assert2 ('b', row[2])
	assert2 ('c', row[3])
	assert2 ('d', row[4])
	assert2 ('a', row.f1)
	assert2 ('b', row.f2)
	assert2 ('c', row.f3)
	assert2 ('d', row.f4)
	row, err = cur:fetch (row, "an")
	assert2 (type(row), "table", err)
	assert2 ('f', row[1])
	assert2 ('g', row[2])
	assert2 ('h', row[3])
	assert2 ('i', row[4])
	assert2 ('f', row.f1)
	assert2 ('g', row.f2)
	assert2 ('h', row.f3)
	assert2 ('i', row.f4)
	assert2 (nil, cur:fetch(row, "an"))
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	assert2 (false, cur:close())
	-- clean the table.
	assert2 (2, CONN:execute ("delete from t where f1 in ('a', 'f')"))
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
end

---------------------------------------------------------------------
-- Fetch many values
---------------------------------------------------------------------
function fetch_many ()
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
	-- insert values.
	local fields, values = "f1", "'v1'"
	for i = 2, TOTAL_FIELDS do
		fields = string.format ("%s,f%d", fields, i)
		values = string.format ("%s,'v%d'", values, i)
	end
	local cmd = string.format ("insert into t (%s) values (%s)",
		fields, values)
	assert2 (1, CONN:execute (cmd))
	-- fetch values (without a table).
	local cur = CUR_OK (CONN:execute ("select * from t where f1 = 'v1'"))
	local row = { cur:fetch () }
	assert2 ("string", type(row[1]), "error while trying to fetch many values (without a table)")
	for i = 1, TOTAL_FIELDS do
		assert2 ('v'..i, row[i])
	end
	assert2 (nil, cur:fetch (row))
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	-- fetch values (with a table and default indexing).
	io.write ("with a table...")
	local cur = CUR_OK (CONN:execute ("select * from t where f1 = 'v1'"))
	local row = cur:fetch(fetch_table())
	assert2 ("string", type(row[1]), "error while trying to fetch many values (default indexing)")
	for i = 1, TOTAL_FIELDS do
		assert2 ('v'..i, row[i])
	end
	assert2 (nil, cur:fetch (row))
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	-- fetch values (with numbered indexes on a table).
	io.write ("with numbered keys...")
	local cur = CUR_OK (CONN:execute ("select * from t where f1 = 'v1'"))
	local row = cur:fetch (fetch_table(), "n")
	assert2 ("string", type(row[1]), "error while trying to fetch many values (numbered indexes)")
	for i = 1, TOTAL_FIELDS do
		assert2 ('v'..i, row[i])
	end
	assert2 (nil, cur:fetch (row))
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	-- fetch values (with alphanumeric indexes on a table).
	io.write ("with alpha keys...")
	local cur = CUR_OK (CONN:execute ("select * from t where f1 = 'v1'"))
	local row = cur:fetch (fetch_table(), "a")
	assert2 ("string", type(row.f1), "error while trying to fetch many values (alphanumeric indexes)")
	for i = 1, TOTAL_FIELDS do
		assert2 ('v'..i, row['f'..i])
	end
	assert2 (nil, cur:fetch (row))
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	-- fetch values (with both indexes on a table).
	io.write ("with both keys...")
	local cur = CUR_OK (CONN:execute ("select * from t where f1 = 'v1'"))
	local row = cur:fetch (fetch_table(), "na")
	assert2 ("string", type(row[1]), "error while trying to fetch many values (both indexes)")
	assert2 ("string", type(row.f1), "error while trying to fetch many values (both indexes)")
	for i = 1, TOTAL_FIELDS do
		assert2 ('v'..i, row[i])
		assert2 ('v'..i, row['f'..i])
	end
	assert2 (nil, cur:fetch (row))
	assert2 (false, cur:close(), MSG_CURSOR_NOT_CLOSED)
	-- clean the table.
	assert2 (1, CONN:execute ("delete from t where f1 = 'v1'"))
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function rollback ()
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
	-- begin transaction
	assert2 (true, CONN:setautocommit (false), "couldn't disable autocommit")
	-- insert a record and commit the operation.
	assert2 (1, CONN:execute ("insert into t (f1) values ('a')"))
	local cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber (cur:fetch ()), "Insert failed")
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
	assert2 (true, CONN:commit(), "couldn't commit transaction")
	-- insert a record and roll back the operation.
	assert2 (1, CONN:execute ("insert into t (f1) values ('b')"))
	local cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (2, tonumber (cur:fetch ()), "Insert failed")
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
	assert2 (true, CONN:rollback (), "couldn't roolback transaction")
	-- check resulting table with one record.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber(cur:fetch()), "Rollback failed")
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
	-- delete a record and roll back the operation.
	assert2 (1, CONN:execute ("delete from t where f1 = 'a'"))
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber(cur:fetch()))
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
	assert2 (true, CONN:rollback (), "couldn't roolback transaction")
	-- check resulting table with one record.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber(cur:fetch()), "Rollback failed")
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
--[[
	-- insert a second record and turn on the auto-commit mode.
	-- this will produce a rollback on PostgreSQL and a commit on ODBC.
	-- what to do?
	assert2 (1, CONN:execute ("insert into t (f1) values ('b')"))
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (2, tonumber (cur:fetch ()), "Insert failed")
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
	assert2 (true, CONN:setautocommit (true), "couldn't enable autocommit")
	-- check resulting table with one record.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (1, tonumber(cur:fetch()), "Rollback failed")
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
--]]
	-- clean the table.
	assert2 (1, CONN:execute (sql_erase_table"t"))
	assert2 (true, CONN:commit (), "couldn't commit transaction")
	assert2 (true, CONN:setautocommit (true), "couldn't enable autocommit")
	-- check resulting table with no records.
	cur = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber(cur:fetch()), "Rollback failed")
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
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
		assert2 ("f"..i, string.lower(names[i]), "incorrect column names table")
		local type_i = types[i]
		type_i = string.lower(type_i)
		assert2 (QUERYING_STRING_TYPE_NAME, type_i, "incorrect column types table")
	end
	-- check if the tables are being reused.
	local n2, t2 = cur:getcolnames(), cur:getcoltypes()
	if CHECK_GETCOL_INFO_TABLES then 
		assert2 (names, n2, "getcolnames is rebuilding the table")
		assert2 (types, t2, "getcoltypes is rebuilding the table")
	else
		assert2 (true, table_compare(names, n2), "getcolnames is inconsistent")
		assert2 (true, table_compare(types, t2), "getcoltypes is inconsistent")
	end
	assert2 (true, cur:close(), "couldn't close cursor")
	assert2 (false, cur:close())
	-- clean the table.
	assert2 (1, CONN:execute ("delete from t where f1 = 'a'"))
end

---------------------------------------------------------------------
-- Escaping strings
---------------------------------------------------------------------
function escape ()
	local escaped = CONN:escape"a'b'c'd"
	assert ("a\\'b\\'c\\'d" == escaped or "a''b''c''d" == escaped)

	local n = 5200
	local s1 = string.rep("'", n)
	local s2 = CONN:escape(s1)
	local s3 = s1:gsub ("'", "\\'")
	assert2 (n, s1:len(), "incorrect escaping of '"..s1.."': expected "..n.." bytes, but got "..s1:len())
	assert2 (2*n, s2:len(), "incerrect escaping of '"..s2.."': expected "..(2*n).." bytes, but got "..s2:len())
	assert (s2 == s1..s1 or s2 == s3)

	io.write (" escape")
end

---------------------------------------------------------------------
-- Check closing of various objects.
---------------------------------------------------------------------
function check_close()
	local cur0 = CUR_OK(CONN:execute"select count(*) from t")
	assert2 (0, tonumber(cur0:fetch()))
	cur0:close()
	-- an object with references to it can't be closed
	local cmd = "select * from t"
	local cur = CUR_OK(CONN:execute (cmd))
	assert2 (true, cur:close(), "couldn't close cursor")

	-- force garbage collection
	local a = {}
	setmetatable(a, {__mode="v"})
	a.CONN = ENV:connect (datasource, username, password)
	cur = CUR_OK(a.CONN:execute (cmd))

	collectgarbage ()
	collectgarbage ()
	CONN_OK (a.CONN)
	a.cur = cur
	a.cur:close()
	a.CONN:close()
	cur = nil
	collectgarbage ()
	assert2(nil, a.cur, "cursor not collected")
	collectgarbage ()
	assert2(nil, a.CONN, "connection not collected")

	-- check cursor integrity after trying to close its connection
	local conn = CONN_OK (ENV:connect (datasource, username, password))
	assert2 (1, conn:execute"insert into t (f1) values (1)", "could not insert a new record")
	local cur = CUR_OK (conn:execute (cmd))
	local ok, err, msg = pcall (conn.close, conn)
	assert2 (true, ok, "couldn´t try to close the connection")
	-- err could be true if the driver DOESN'T care about closing a connection
	--	that has open cursors
	-- err could be false if the driver DOES care about closing a connection
	--	that has open cursors
	CUR_OK (cur)
	assert (cur:fetch(), "corrupted cursor")
	cur:close ()
	conn:close ()
	-- The following check is failing in Firebird
    --local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
    --assert2 (1, tonumber (cur0:fetch()))
	--cur0:close()

	-- check connection integrity after trying to close an environment
	local conn = CONN_OK (ENV:connect (datasource, username, password))
	local closed = ENV:close()
	--assert2 (true, ENV:close(), "couldn't close the environment!")
	if closed then
		-- Some drivers can close the environment with open connections
		CONN_OK (conn)
		local cmd = "select * from t"
		local cur = CUR_OK(CONN:execute (cmd))
		assert2 ('1', cur:fetch(), "couldn't retrieve a row from `t`")
		assert2 (true, cur:close(), "couldn't close the cursor!")
    local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
    assert2 (1, tonumber (cur0:fetch()))
	cur0:close()

		assert2 (true, conn:close(), "couldn't close the connection!")
		ENV = ENV_OK (luasql[driver] ())
	else
		-- Some drivers cannot close the environment with open connections
		conn:close()
		ENV:close()
	end
end

---------------------------------------------------------------------
-- Check support for to-be-closed variables.
-- Since this feature depends on a syntax construction not available
-- in versions prior to 5.4, this check should be written in another
-- file (or in a string).
---------------------------------------------------------------------
function to_be_closed_support ()
	assert (loadfile"to_be_closed_support.lua")()
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function drop_table ()
	-- check number of lines
	-- The following check is failing with Firebird
	--local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	--assert2 (1, tonumber (cur0:fetch()))
	--assert2 (true, cur0:close(), "couldn't close the cursor")

	assert2 (true, CONN:setautocommit(true), "couldn't enable autocommit")
	-- Postgres retorns 0, ODBC retorns -1, sqlite returns 1
	assert2 (DROP_TABLE_RETURN_VALUE, CONN:execute ("drop table t"))
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function close_conn ()
	assert2 (true, CONN:close())
	assert2 (true, ENV:close())
end

---------------------------------------------------------------------
---------------------------------------------------------------------
function finalization ()
	-- nothing to do
end

---------------------------------------------------------------------
-- Testing Extensions
---------------------------------------------------------------------
EXTENSIONS = {
}
function extensions_test ()
	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))

	for i, f in ipairs (EXTENSIONS) do
		f ()
	end

	-- check number of lines
	local cur0 = CUR_OK (CONN:execute ("select count(*) from t"))
	assert2 (0, tonumber (cur0:fetch()))
end

---------------------------------------------------------------------
-- Testing numrows method.
-- This is not a default test, it must be added to the extensions
-- table to be executed.
---------------------------------------------------------------------
function numrows()
    local cur = CUR_OK(CONN:execute"select * from t")
    assert2(0,cur:numrows())
    cur:close()

    -- Inserts one row.
    assert2 (1, CONN:execute"insert into t (f1) values ('a')", "could not insert a new record")
    cur = CUR_OK(CONN:execute"select * from t")
    assert2(1,cur:numrows())
    cur:close()

    -- Inserts three more rows (total = 4).
    assert2 (1, CONN:execute"insert into t (f1) values ('b')", "could not insert a new record")
    assert2 (1, CONN:execute"insert into t (f1) values ('c')", "could not insert a new record")
    assert2 (1, CONN:execute"insert into t (f1) values ('d')", "could not insert a new record")
    cur = CUR_OK(CONN:execute"select * from t")
    assert2(4,cur:numrows())
	cur:close()

    -- Deletes one row
    assert2(1, CONN:execute"delete from t where f1 = 'a'", "could not delete the specified row")
    cur = CUR_OK(CONN:execute"select * from t")
    assert2(3,cur:numrows())
    cur:close()

    -- Deletes all rows
    assert2 (3, CONN:execute (sql_erase_table"t"))
    cur = CUR_OK(CONN:execute"select * from t")
    assert2(0,cur:numrows())
    cur:close()

	io.write (" numrows")
end


---------------------------------------------------------------------
-- Main
---------------------------------------------------------------------

if type(arg[1]) ~= "string" then
	print (string.format ("Usage %s <driver> [<data source> [, <user> [, <password>]]]", arg[0]))
	os.exit()
end

driver = arg[1]

-- Loading driver specific functions
if arg[0] then
	local path = string.gsub (arg[0], "^(.*%/)[^/]*$", "%1")
	if path == "test.lua" then
		path = ""
	end
	local file = path..driver..".lua"
	local f, err = loadfile (file)
	if not f then
		print ("LuaSQL test: couldn't find driver-specific test file ("..
			file..").\nProceeding with general test")
	else
		print ("Loading driver-specific test file ("..file..").")
		f ()
	end
end

datasource = arg[2] or DEFAULT_TEST_DATABASE or "luasql-test"
username = arg[3] or DEFAULT_USERNAME or nil
password = arg[4] or DEFAULT_PASSWORD or nil

-- Complete set of tests
tests = {
	{ "basic checking", basic_test },
	{ "create table", create_table },
	{ "fetch two values", fetch2 },
	{ "fetch new table", fetch_new_table },
	{ "fetch many", fetch_many },
	{ "rollback", rollback },
	{ "get column information", column_info },
	{ "extensions", extensions_test },
	{ "close objects", check_close },
-- to-be-closed variables could be inserted here
	{ "drop table", drop_table },
	{ "close connection", close_conn },
	{ "finalization", finalization },
}

if string.find(_VERSION, " 5.0") then
	local init_so, err = loadlib("./"..driver..".so", "luaopen_luasql_"..driver)
	if init_so then
		luasql = init_so()
	else
		luasql = assert(loadlib("/usr/local/lib/lua/5.0/luasql/"..driver..".so", "luaopen_luasql_"..driver))()
	end
else
	luasql = require ("luasql."..driver)
	if string.find(_VERSION, " 5.4")
		or string.find(_VERSION, " 5.5") then
		table.insert (tests, 10, { "to-be-closed support", to_be_closed_support })
	end
end
assert (luasql, "Could not load driver: no luasql table.")
io.write (luasql._VERSION.." "..driver)
if luasql._CLIENTVERSION then
	io.write (" ("..luasql._CLIENTVERSION..")")
end
io.write (" driver test.  "..luasql._COPYRIGHT.."\n")

for i = 1, table.getn (tests) do
	local t = tests[i]
	io.write (t[1].." ...")
	local ok, err = xpcall (t[2], debug.traceback)
	if not ok then
		io.write ("\n"..err)
		io.write"\n... trying to drop test table ..."
		local ok, err = pcall (drop_table)
		if not ok then
			io.write (" failed: "..err)
		else
			io.write" OK !\n... and to close the connection ..."
			local ok, err = pcall (close_conn)
			if not ok then
				io.write (" failed: "..err)
			else
				io.write" OK !"
			end
		end
		io.write"\nThe test failed!\n"
		return
	end
	io.write (" OK !\n")
end
io.write ("The test passed!\n")
