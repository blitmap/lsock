local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')

local cwrap    = coroutine.wrap
local cyield   = coroutine.yield

local registry = debug.getregistry()
local loaded   = package.loaded

-- this section is for making friendly
-- structures that really don't need much done
-- we set up their pairs()/ipairs() and constructor
local fields =
	{
		-- order of members should match struct definition
		linger  = { 'l_onoff', 'l_linger' },
		timeval = { 'tv_sec',  'tv_usec'  }
	}

for _, v in pairs({ 'linger', 'timeval' }) do
	local mt   = registry[basename .. '.' .. v]
	local ctor = core[v] -- fetch the userdata constructor

	mt.__pairs =
		function (s)
			return
				cwrap
				(
					function ()
						for _, k in pairs(fields) do
							local v = s[k]

							cyield(k, v)
						end
					end
				)
		end

	mt.__ipairs =
		function (s)
			return
				cwrap
				(
					function ()
						for i, k in ipairs(fields) do
							local v = s[k]

							cyield(i, v)
						end
					end
				)
		end

	-- wrap the constructor to accept at table of predefined members
	core[v] =
		function (t)
			local l = ctor()

			for k, v in pairs(t or {}) do
				l[k] = v
			end

			return l
		end

	loaded[... .. '.' .. v] = true -- as require() would do
end

-- anything that needs more complex friendly setup gets loaded here
for _, v in pairs({ 'sockaddr', 'socket', 'listen', 'shutdown' }) do
	require(... .. '.' .. v)
end

-- if it's there...
if core.sendfile   then require(... .. '.sendfile')   end
if core.socketpair then require(... .. '.socketpair') end

return true
