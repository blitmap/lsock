local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')
local _select  = core.select -- name conflict

local modf  = math.modf
local floor = math.floor

-- select({}, {}, {}, 3) -> select({}, {}, {}, timeval { tv_sec = 3, tv_usec = 0 })
core.select =
	function (r, w, e, t)
		if type(t) == 'number' then
			local s, ms = modf(t)

			ms = floor(1000 * ms)

			t = { tv_sec = s, tv_usec = ms }
		end

		return _select(r, w, e, t)
	end

return true
