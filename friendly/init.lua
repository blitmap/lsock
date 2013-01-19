for _, v in ipairs({ 'socket', 'sockaddr', 'linger', 'timeval', 'listen', 'shutdown' }) do
	assert(require('lsock.friendly.' .. v))
end

return true
