local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')

local mt = {}

mt.__index    = core._timeval_getset
mt.__newindex = core._timeval_getset

debug.getregistry()[basename .. '.timeval'] = mt

return true
