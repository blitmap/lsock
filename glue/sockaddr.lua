local core   = require('lsock.core')
local cwrap  = coroutine.wrap
local cyield = coroutine.yield

local mt = {}

mt.__index    = core._sockaddr_getset
mt.__newindex = core._sockaddr_getset

debug.getregistry()['lsock.sockaddr'] = mt

return true
