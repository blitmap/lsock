local core   = require('lsock.core')
local cwrap  = coroutine.wrap
local cyield = coroutine.yield

local mt = {}

mt.__index    = core._timeval_getset
mt.__newindex = core._timeval_getset

debug.getregistry()['lsock.timeval'] = mt

return true
