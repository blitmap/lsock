local core   = require('lsock.core')
local cwrap  = coroutine.wrap
local cyield = coroutine.yield

local mt = {}

mt.__index    = core._linger_getset
mt.__newindex = core._linger_getset

debug.getregistry()['lsock.linger'] = mt

return true
