---------------------------------------------------------------------
-- LuaSQL JDBC driver implementation
-- @author Thiago Ponte
---------------------------------------------------------------------

---------------------------------------------------------------------
-- luasql table name
---------------------------------------------------------------------
local libName = "luasql"

local Private = {}

luasql = (_G[libName] and type(_G[libName]) == "table") or {}

Private.createJavaCursor = luajava.loadLib("org.keplerproject.luasql.jdbc.LuaSQLCursor", "open")

---------------------------------------------------------------------
-- function that returns a jdbc environment
---------------------------------------------------------------------
function luasql.jdbc(driver)

    if driver == nil then
        return nil, "Error. Argument #1 must be the jdbc driver class."
    end
    
    local cond, err = pcall(luajava.bindClass, driver)
    if not cond then
        return nil, err
    end
    
    return Private.createEnv()
end

---------------------------------------------------------------------
-- creates a jdbc environment
---------------------------------------------------------------------
function Private.createEnv()

    local isClosed  = false
    local openConns = {}
    openConns.n     = 0

    local env = {}
    
    local function closeConn(con)
    
        if not openConns[con] then
            return false
        end
        
        openConns[con] = nil
        openConns.n = openConns.n - 1
        
        return true
    end
    
    function env.close()
    
        if isClosed or openConns.n ~= 0 then
            return false
        end
        
        isClosed = true
        
        return true
    end
    
    function env.connect(self, sourcename, username, password)
    
        if isClosed then
            return nil, "Environment closed."
        end
        if sourcename == nil then
            return nil, "Invalid sourcename."
        end

        local driverManager = luajava.bindClass("java.sql.DriverManager")
        
        local cond, con
        if username == nil and password == nil then
            cond, con = pcall(driverManager.getConnection, driverManager, sourcename)
        else
            cond, con = pcall(driverManager.getConnection, driverManager, sourcename, username or '', password or '')
        end
        
        if not cond then
            return nil, con
        end
        
        openConns[con] = true
        openConns.n = openConns.n + 1
        
        return Private.createConnection(con, closeConn)
    end
    
    return env
end


---------------------------------------------------------------------
-- creates a jdbc connection
---------------------------------------------------------------------
function Private.createConnection(conObj, closeFunc)

    local openCursors = {}
    openCursors.n = 0
    
    local con = {}
    
    local function closeCursor(cursor)
    
        if not openCursors[cursor] then
            return false
        end
        
        openCursors[cursor] = nil
        openCursors.n = openCursors.n - 1
    end
    
    function con.close()
    
        if conObj:isClosed() or openCursors.n ~= 0 then
            return false
        end
        
        conObj:close()
        closeFunc(conObj)
        
        return true
    end
    
    function con.commit()
    
        local cond, err = pcall(conObj.commit, conObj)
        if not cond then
            return nil, err
        end
    end
    
    function con.execute(self, sql)
    
        local st = conObj:createStatement()

        local cond, isRS = pcall(st.execute, st, sql)
        if not cond then
            return nil, isRS
        end

        local res;
        if isRS then
            res = Private.createCursor(st:getResultSet(), st, closeCursor)
            openCursors[res] = true
            openCursors.n = openCursors.n + 1
        else
            res = st:getUpdateCount()
            st:close();
        end
        
        return res
    end
    
    function con.rollback()
    
        local cond, err = pcall(conObj.rollback, conObj)
        if not cond then
            return nil, err
        end
    end
    
    function con.setautocommit(self, bool)

        local cond, err = pcall(conObj.setAutoCommit, conObj, bool)
        if not cond then
            return nil, err
        end
    end
   
    return con
end

---------------------------------------------------------------------
-- creates a jdbc cursor
---------------------------------------------------------------------
function Private.createCursor(rs, st, closeFunc)

    local isClosed = false
    local cursor = Private.createJavaCursor(rs)
    local res = {}
    
    function res.close()
    
        if isClosed then
            return false
        end
        
        rs:close()
        st:close()
        closeFunc(res)
        
        isClosed = true
        
        return true
    end
    
    function res.fetch(self, tb, modestring)
    
        if tb == nil or type(tb) ~= "table" then
            tb = {}
        end
        
        if modestring == nil or type(modestring) ~= "string" then
            modestring = "n"
        end
        
        local cond, tb = pcall(cursor.fetch, cursor, tb, modestring)
        if not cond then
            return nil, tb
        end
        
        return tb
    end
    
    function res.getcolnames()
    
        local cond, tb = pcall (cursor.getcolnames, cursor)
        if not cond then
            return cond, tb
        end
        
        return tb
    end
    
    function res.getcoltypes()
    
        local cond, tb = pcall(cursor.getcoltypes, cursor)
        if not cond then
            return nil, tb
        end
        
        return tb
    end
    
    return res
end
