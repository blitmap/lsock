local core     = require('lsock.core')
local cwrap    = coroutine.wrap
local cyield   = coroutine.yield
local sockaddr = core.sockaddr
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

local mt = debug.getregistry()['lsock.sockaddr']

mt.__pairs =
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

mt.__ipairs =
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

core.sockaddr =
	function (t)
		t = core[t] or t or {}

		local addr = sockaddr()

		local fam = t.ss_family or t.sa_family or t.sin_family or t.sin6_family or t.sun_family or nil

		fam = core[fam] or fam

		if fam then
			addr.ss_family = fam -- redundant but must be set first

			for k, v in pairs(t) do
				addr[k] = core[v] or v
			end
		end

		return addr
	end

-- tmp = lsock.sockaddr 'INADDR_ANY_INIT'
core.INADDR_ANY_INIT       = { sin_family  = 'AF_INET',  sin_addr  = core.INADDR_ANY       }
core.INADDR_LOOPBACK_INIT  = { sin_family  = 'AF_INET',  sin_addr  = core.INADDR_LOOPBACK  }
core.IN6ADDR_ANY_INIT      = { sin6_family = 'AF_INET6', sin6_addr = core.in6addr_any      }
core.IN6ADDR_LOOPBACK_INIT = { sin6_family = 'AF_INET6', sin6_addr = core.in6addr_loopback }

return true
