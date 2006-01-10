---------------------------------------------------------------------
-- MySQL specific tests and configurations.
-- $Id: mysql.lua,v 1.1 2006/01/10 18:31:50 tomas Exp $
---------------------------------------------------------------------

---------------------------------------------------------------------
-- Build SQL command to create the test table.
---------------------------------------------------------------------
local _define_table = define_table
function define_table (n)
	return _define_table(n) .. " TYPE = InnoDB;"
end

