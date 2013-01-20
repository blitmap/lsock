local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')

local registry = debug.getregistry()
local loaded   = package.loaded

-- the setup for these is so simple I put it
-- here since the code would be duplicated for each:
for _, v in pairs({ 'linger', 'timeval', 'sockaddr' }) do
	local mt = {}
	local getset = core['_' .. v .. '_getset']

	mt.__index    = getset
	mt.__newindex = getset

	
	registry[basename .. '.' .. v] = mt

	loaded[... .. '.' .. v] = true -- as require() would do
end

-- anything more complex to set up:

for _, v in pairs({ 'socket' }) do
	require(... .. '.' .. v)
end

return true
