-- load driver
require"postgres"
-- create environment object
env, err = luasql.postgres()
assert(env, err)
-- connect to data source
con, err = env:connect("luasql-test")
assert(con, err)
-- reset our table
res, err = con:execute"DROP TABLE people"
res, err = con:execute[[
  CREATE TABLE people(
    name  varchar(50),
    email varchar(50)
  )
]]
assert(res, err)
-- add a few elements
list = {
    { name="Tomas Guisasola", email="tomas@kepler.org", },
    { name="Roberto Ierusalimschy", email="roberto@kepler.org", },
    { name="Andre Carregal", email="carregal@kepler.org", },
}
for i, p in pairs (list) do
  res, err = con:execute(string.format([[
    INSERT INTO people
    VALUES ('%s', '%s')]], p.name, p.email)
  )
  assert(res, err)
end
-- retrieve a cursor
cur, err = con:execute"SELECT name, email from people"
assert(cur, err)
-- print all rows
row = cur:fetch ({}, "a")	-- the rows will be indexed by field names
while row do
	print(string.format("Name: %s, E-mail: %s", row.name, row.email))
	row = cur:fetch (row, "a")	-- reusing the table of results
end
-- close everything
cur:close()
con:close()
env:close()
