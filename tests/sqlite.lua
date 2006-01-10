---------------------------------------------------------------------
-- SQLite specific tests and configurations.
-- $Id: sqlite.lua,v 1.1 2006/01/10 18:22:46 tomas Exp $
---------------------------------------------------------------------

---------------------------------------------------------------------
-- Produces a SQL statement which completely erases a table.
-- @param table_name String with the name of the table.
-- @return String with SQL statement.
---------------------------------------------------------------------
function sql_erase_table (table_name)
	return string.format ("delete from %s where 1", table_name)
end
