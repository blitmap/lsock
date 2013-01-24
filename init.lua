local core = require(... .. '.core')

for _, v in ipairs({ 'friendly' }) do
	require(... .. '.' .. v) -- provide a second arg, the base name of the library
end

return core
