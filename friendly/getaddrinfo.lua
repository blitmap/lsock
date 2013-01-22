local basename    = (...):match('^[^.]*')
local core        = require(basename .. '.core')
local getaddrinfo = core.getaddrinfo

local tunp = table.unpack
local band = bit32.band

-- getaddrinfo { node = 'www.google.com', service = 'http', ai_family = 'AF_INET', ai_flags = { 'AI_CANONNAME' } }
core.getaddrinfo =
	function (addr)
		local node, service, hints = addr.node, addr.service, addr

		if type(addr.ai_flags) == 'table' then
			for k, v in pairs(addr.ai_flags) do
				addr.ai_flags[k] = core[v] or v
			end

			addr.ai_flags = band(tunp(addr.ai_flags))
		end

		addr.ai_family   = core[addr.ai_family  ] or addr.ai_family
		addr.ai_socktype = core[addr.ai_socktype] or addr.ai_socktype
		addr.ai_protocol = core[addr.ai_protocol] or addr.ai_protocol

		return getaddrinfo(node, service, hints)
	end

return true
