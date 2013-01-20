local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')

local mt = {}

mt.__index    = core._linger_getset
mt.__newindex = core._linger_getset

debug.getregistry()[basename .. '.linger'] = mt

return true
