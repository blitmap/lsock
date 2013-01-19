for _, v in ipairs({ 'socket', 'sockaddr', 'linger', 'timeval', 'listen', 'shutdown' }) do
	require('lsock.friendly.' .. v)
end

return true
