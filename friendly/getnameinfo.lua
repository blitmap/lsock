local basename    = (...):match('^[^.]*')
local core        = require(basename .. '.core')
local getnameinfo = core.getnameinfo
local sockaddr    = core.sockaddr

local tunp = table.unpack
local band = bit32.band

-- getnameinfo('INADDR_ANY_INIT', 'NI_NUMERICHOST') -> getnameinfo(sockaddr 'INADDR_ANY_INIT', NI_NUMERICHOST)
core.getnameinfo =
	function (sa, flags)
		local tmp = type(flags)

		if tmp == 'table' then
			for i, v in ipairs(flags) do
				flags[i] = core[v] or v
			end

			flags = band(tunp(flags))
		elseif tmp == 'string' then
			flags = core[flags] or flags
		end

		return getnameinfo(sa, flags or 0)
	end

return true
