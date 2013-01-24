local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')

-- anything that needs more complex friendly setup gets loaded here
for _, v in pairs({ 'bind', 'connect', 'getaddrinfo', 'getnameinfo', 'select', 'socket', 'shutdown' }) do
	require(... .. '.' .. v)
end

-- if it's there...
if core.sendfile   then require(... .. '.sendfile')   end
if core.socketpair then require(... .. '.socketpair') end

return true
