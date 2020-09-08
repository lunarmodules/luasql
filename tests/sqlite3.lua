---------------------------------------------------------------------
-- SQLite specific tests and configurations.
---------------------------------------------------------------------

DROP_TABLE_RETURN_VALUE = 1

---------------------------------------------------------------------
-- Produces a SQL statement which completely erases a table.
-- @param table_name String with the name of the table.
-- @return String with SQL statement.
---------------------------------------------------------------------
function sql_erase_table (table_name)
	return string.format ("delete from %s where 1", table_name)
end

function checkUnknownDatabase(ENV)
	-- skip this test
end

function finalization ()
	os.execute ("rm -rf "..datasource)
end

table.insert (CONN_METHODS, "escape")
table.insert (EXTENSIONS, escape)
