local basename    = (...):match('^[^.]*')
local core        = require(basename .. '.core')
local getaddrinfo = core.getaddrinfo

local tunp = table.unpack
local band = bit32.band

-- getaddrinfo { node = 'www.google.com', service = 'http', ai_family = 'AF_INET', ai_flags = { 'AI_CANONNAME' } }
core.getaddrinfo =
	function (node, service, hints)
		hints = hints or {}

		for k, v in pairs(hints) do
			hints[k] = core[v] or v
		end

		if type(hints.ai_flags) == 'table' then
			for k, v in pairs(hints.ai_flags) do
				hints.ai_flags[k] = core[v] or v
			end

			hints.ai_flags = band(tunp(hints.ai_flags))
		end

		hints.ai_family   = core[hints.ai_family  ] or hints.ai_family
		hints.ai_socktype = core[hints.ai_socktype] or hints.ai_socktype
		hints.ai_protocol = core[hints.ai_protocol] or hints.ai_protocol

		return getaddrinfo(node, service, hints)
	end

return true
