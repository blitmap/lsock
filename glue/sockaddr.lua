local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')

local mt = {}

mt.__index    = core._sockaddr_getset
mt.__newindex = core._sockaddr_getset

debug.getregistry()[basename .. '.sockaddr'] = mt

return true
