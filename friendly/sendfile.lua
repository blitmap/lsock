local basename = (...):match('^[^.]*')
local core     = require(basename .. '.core')
local sendfile = core.sendfile

-- users should set the new offset with :seek() with the return from sendfile()!
core.sendfile =
	function (self, file, opts)
		opts = opts or {}

		local offset = opts.offset or file:seek()
		local count  = opts.count  or (file:seek('end') - offset)

		-- reset this to the original
		file:seek('set', offset)

		-- sendfile(out, in, nil, 1024) -> read 1024 bytes from the current offset of `in_fh'
		return sendfile(self, file, offset, count)
	end

return true
