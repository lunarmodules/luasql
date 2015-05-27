# LuaSQL 3.0 (proposed)
http://www.keplerproject.org/luasql/
https://github.com/keplerproject/luasql/

LuaSQL is a simple interface from Lua to a DBMS. It enables a Lua program to:

    * Connect to ODBC, ADO, Oracle, MySQL, SQLite and PostgreSQL databases;
    * Execute arbitrary SQL statements;
    * Retrieve results in a row-by-row cursor fashion.

LuaSQL is free software and uses the same license as Lua 5.1. 

Source code for LuaSQL can be downloaded from the Kepler Project Github page.

- - -

## LuaSQL 3.0.0 [09/Apr/2015]
This is a proposed branch to add much needed features
 * Prepared statements with parameters
  + Really a major requirement for any DB system that takes data from the web
 * More flexible connection method
  + Current method of just passing DB name/path with user name+password is not enough to support the complexities of connecting to a DB
 * Explicit handling of BLOB fields (? maybe)
  + It might be useful to handle true binary data from BLOB fields rather than blindly convert them to Lua strings

