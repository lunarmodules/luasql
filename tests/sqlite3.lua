---------------------------------------------------------------------
-- SQLite specific tests and configurations.
-- $Id: sqlite3.lua,v 1.1 2007/04/06 23:46:04 mascarenhas Exp $
---------------------------------------------------------------------

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