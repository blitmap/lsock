local basename      = (...):match('^[^.]*')
local core          = require(basename .. '.core')
local getnameinfo   = core.getnameinfo
local pack_sockaddr = core.pack_sockaddr

local tunp = table.unpack
local band = bit32.band

core.getnameinfo =
	function (sa, flags)
		sa    = core[sa] or sa
		flags = flags    or 0

		if type(sa) == 'table' then
			sa = pack_sockaddr(sa)
		end

		local tmp = type(flags)

		if tmp == 'table' then
			for i, v in ipairs(flags) do
				flags[i] = core[v] or v
			end

			flags = band(tunp(flags))
		elseif tmp == 'string' then
			flags = core[flags] or flags
		end

		return getnameinfo(sa, flags)
	end

return true
