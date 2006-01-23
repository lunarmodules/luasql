---------------------------------------------------------------------
-- ODBC specific tests and configurations.
-- $Id: odbc.lua,v 1.1 2006/01/23 20:13:25 tomas Exp $
---------------------------------------------------------------------

QUERYING_STRING_TYPE_NAME = "string"
-- The CREATE_TABLE_RETURN_VALUE and DROP_TABLE_RETURN_VALUE works
-- with -1 on MS Acess Driver, and 0 on SQL Server Driver
CREATE_TABLE_RETURN_VALUE = -1
DROP_TABLE_RETURN_VALUE = -1
