local core   = require('lsock.core')
local cwrap  = coroutine.wrap
local cyield = coroutine.yield
local fields =
	{
		[core.AF_INET] =
			{
				'sin_family',
				'sin_port',
				'sin_addr'
			},
		[core.AF_INET6] =
			{
				'sin6_family',
				'sin6_port',
				'sin6_flowinfo',
				'sin6_addr',
				'sin6_scope_id'
			},
		[core.AF_UNIX] =
			{
				'sun_family',
				'sun_path'
			},
		-- the anything-else sockaddr type
		-- 'ss_family' isn't part of this
		sockaddr =
			{
				'sa_family',
				'sa_data'
			}
		}

local sockaddr_mt = {}

sockaddr_mt.__index    = core._sockaddr_getset
sockaddr_mt.__newindex = core._sockaddr_getset

sockaddr_mt.__pairs =
	function (s)
		return
			cwrap
			(
				function ()
					for _, k in pairs(fields[s.ss_family] or fields.sockaddr) do
						local v = s[k]

						cyield(k, v)
					end
				end
			)
	end

sockaddr_mt.__ipairs =
	function (s)
		return
			cwrap
			(
				function ()
					for i, k in ipairs(fields[s.ss_family] or fields.sockaddr) do
						local v = s[k]

						cyield(i, v)
					end
				end
			)
	end

debug.getregistry()['lsock sockaddr'] = sockaddr_mt

return true
