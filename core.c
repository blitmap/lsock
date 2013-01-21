/* compile: gcc -o core{.so,.c} -shared -fPIC -W -Wall -O2 -g -llua -lm -ldl -pedantic -ansi -std=c89 -flto -fstack-protector-all */

#ifndef _POSIX_SOURCE
#	define _POSIX_SOURCE
#endif

#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#ifdef _WIN32
#	pragma comment(lib, "Ws2_32.lib")
#	define WIN32_LEAN_AND_MEAN /* avoid MVC stuffs */
#	include <windows.h>
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <io.h>             /* _open_osfhandle() / _get_osfhandle() */
#else
#	include <arpa/inet.h>
#	include <sys/socket.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <sys/types.h>
#	include <sys/un.h>
#	include <string.h>
#	include <unistd.h>
#	include <errno.h>
#	include <sys/time.h>
#	include <sys/sendfile.h>
#endif

#include <lauxlib.h>
#include <lualib.h>

#define NUL '\0'
#define BZERO(buf, sz) memset(buf, NUL, sz)

#define LSOCK_FDOPEN_MODE "r+b"

/* these are formed by combining the library name + '.' + data structure name (without 'struct') */
#define LSOCK_SOCKET   "lsock.socket"
#define LSOCK_SOCKADDR "lsock.sockaddr"
#define LSOCK_LINGER   "lsock.linger"
#define LSOCK_TIMEVAL  "lsock.timeval"

#ifdef _WIN32
typedef SOCKET lsocket;
#else
typedef int    lsocket;
#endif

#define  LSOCK_NEWLSOCKET(L) ((      lsocket   *) lsock_newudata(L, sizeof(lsocket),         LSOCK_SOCKET  ))
#define LSOCK_NEWSOCKADDR(L) ((union LSockAddr *) lsock_newudata(L, sizeof(union LSockAddr), LSOCK_SOCKADDR))
#define   LSOCK_NEWLINGER(L) ((struct linger   *) lsock_newudata(L, sizeof(struct linger),   LSOCK_LINGER  ))
#define  LSOCK_NEWTIMEVAL(L) ((struct timeval  *) lsock_newudata(L, sizeof(struct timeval),  LSOCK_TIMEVAL ))

#define LSOCK_CHECKSOCKADDR(L, index) ((union LSockAddr *) luaL_checkudata(L, index, LSOCK_SOCKADDR))
#define  LSOCK_CHECKTIMEVAL(L, index) ((struct timeval  *) luaL_checkudata(L, index, LSOCK_TIMEVAL ))
#define   LSOCK_CHECKLINGER(L, index) ((struct linger   *) luaL_checkudata(L, index, LSOCK_LINGER  ))

#define LSOCK_CHECKSTREAM(L, index) ((luaL_Stream *) luaL_checkudata(L, index, LUA_FILEHANDLE))
#define LSOCK_CHECKSOCKET(L, index) stream_to_lsocket(L, LSOCK_CHECKSTREAM(L, index)->f)
#define     LSOCK_CHECKFD(L, index) stream_to_fd(L, LSOCK_CHECKSTREAM(L, index)->f)

#define LSOCK_STRERROR(L, fname) lsock_error(L, errno,     &strerror, fname)
#define LSOCK_GAIERROR(L)        lsock_error(L, errno, &gai_strerror, NULL )
#define LSOCK_STRFATAL(L, fname) lsock_fatal(L, errno,     &strerror, fname)

#ifdef _WIN32
#	define INVALID_SOCKET(s) (INVALID_SOCKET == s)
#else
#	define INVALID_SOCKET(s) (s < 0)
#endif

/* including sockaddr_un in this only increases the byte count by 18 */
union LSockAddr
{
	struct sockaddr_storage storage;
	struct sockaddr         addr;
	struct sockaddr_in      in;
	struct sockaddr_in6     in6;
	struct sockaddr_un      un;
};

/*
** DONE:
**			- htons()
**			- htonl()
**			- ntohs()
**			- ntohl()
**
**			- socket()
**			- connect()
**			- bind()
**			- listen()
**			- shutdown()
**
**			- strerror()
**			- gai_strerror()
**
**			- sockaddr() -- for bind()/connect()/accept()/getaddrinfo()
**			- linger()   -- for get/setsockopt()
**			- timeval()  -- for get/setsockopt()
**
**			- sendfile() -- only on Linux
**
** ALMOST:  (the bindings are there, they need friendly/ wrapping)
**		
**			- send()/sendto()   -- (sendto())
**			- recv()/recvfrom() -- (recvfrom())
**
** TODO:
**			- select()
**
**			- getaddrinfo()
**			- getnameinfo() -- low priority?
**			- ioctl()
**			- getsockopt()
**			- setsockopt()
**			- getsockname() -- can be done in Lua
**
**			- getsockname()
**			- getpeername()
**
**			- socketpair() -- only on Linux
**			- htond()      -- only on Windows
**			- htonf()      -- only on Windows
**			- htonll()     -- only on Windows
**			- ntohd()      -- only on Windows
**			- ntohf()      -- only on Windows
**			- ntohll()     -- only on Windows
**
**			- addrinfo() -- for getaddrinfo()
*/

/* {{{ lsock_error() */

static int
lsock_error(lua_State * const L, const int err, char * (*errfunc)(int), const char * fname)
{
	char * msg = errfunc(err);

	lua_pushnil(L);

	if (NULL == fname)
		lua_pushstring(L, msg); /* even if err is not a valid errno, strerror() will return a pointer to "" */
	else
		lua_pushfstring(L, "%s: %s", fname, msg);

	lua_pushinteger(L, err);

	return 3;
}

/* }}} */

/* {{{ lsock_fatal() */

static int
lsock_fatal(lua_State * const L, const int err, char * (*errfunc)(int), const char * fname)
{
	char * msg = errfunc(err);

	if (NULL == fname)
		return luaL_error(L, msg);
	else
		return luaL_error(L, "%s: %s", fname, msg);
}

/* }}} */

/* going to and from SOCKET and fd in Windows: _open_osfhandle()/_get_osfhandle() */

/* {{{ stream_to_fd() */

static int
stream_to_fd(lua_State * const L, FILE * const stream)
{
	int fd = -1;

#ifdef _WIN32
	fd = _fileno(stream);
#else
	fd = fileno(stream);
#endif

	if (-1 == fd)
#ifdef _WIN32
		luaL_error(L, "_fileno(): %s", strerror(errno));
#else
		luaL_error(L, "fileno(): %s", strerror(errno));
#endif

	return fd;
}

/* }}} */

/* {{{ fd_to_stream() */

static FILE *
fd_to_stream(lua_State * const L, const int fd)
{
	FILE * stream = NULL;

#ifdef _WIN32
	stream = _fdopen(fd, LSOCK_FDOPEN_MODE);
#else
	stream = fdopen(fd, LSOCK_FDOPEN_MODE);
#endif

	if (NULL == stream)
#ifdef _WIN32
		LSOCK_STRFATAL(L, "_fdopen()");
#else
		LSOCK_STRFATAL(L, "fdopen()");
#endif

	return stream;
}

/* }}} */

/* {{{ lsocket_to_fd() */

static int
lsocket_to_fd(lua_State * const L, const lsocket sock)
{

#ifdef _WIN32
	int fd = -1;

	fd = _open_osfhandle(handle, 0); /* no _O_APPEND, _O_RDONLY, _O_TEXT */

	if (-1 == fd)
		LSOCK_STRFATAL(L, "_open_osfhandle()");

	return fd;
#else
	(void) L;

	return sock;
#endif

}

/* }}} */

/* {{{ fd_to_lsocket() */

static lsocket
fd_to_lsocket(lua_State * const L, const int fd)
{

#ifdef _WIN32
	lsocket l = _get_osfhandle(fd);

	if (INVALID_HANDLE_VALUE == l)
		return luaL_error("_get_osfhandle(): %s", strerror(errno));

	return l;
#else
	(void) L;

	return fd; /* hah */
#endif

}

/* }}} */

/* {{{ stream_to_lsocket() */

static lsocket
stream_to_lsocket(lua_State * const L, FILE * const stream)
{
	/* wheee shortness. */
	return fd_to_lsocket(L, stream_to_fd(L, stream));
}

/* }}} */

/* {{{ lsocket_to_stream() */

static FILE *
lsocket_to_stream(lua_State * const L, const lsocket sock)
{
	/* \o/ \o| |o/ \o/ \o| |o/ \o/ */
	return fd_to_stream(L, lsocket_to_fd(L, sock));
}

/* }}} */

/* {{{ lsock_newudata() */

static void *
lsock_newudata(lua_State * const L, const size_t sz, const char * const registry_type)
{
	void * const p = lua_newuserdata(L, sz);

	BZERO(p, sizeof(sz));

	luaL_setmetatable(L, registry_type);

	return p;
}

/* }}} */

/* {{{ lsock_newstream(): based directly on: newprefile() & newfile() from Lua 5.2 sources */

static int
close_stream(lua_State * const L)
{
	luaL_Stream * const p = LSOCK_CHECKSTREAM(L, 1);

	return luaL_fileresult(L, (0 == fclose(p->f)), NULL);
}

static luaL_Stream *
newstream(lua_State * const L)
{
	luaL_Stream * const p = lsock_newudata(L, sizeof(luaL_Stream), LUA_FILEHANDLE);

	p->f      = NULL;
	p->closef = &close_stream;

	return p;
}

/* }}} */

/* {{{ lsock_htons() */

static int
lsock_htons(lua_State * const L)
{
	lua_Number n = luaL_checknumber(L, 1);
	uint16_t   s = n;

	if (s != n) /* type promotion back to lua_Number */
	{
		lua_pushnil(L);
		lua_pushfstring(L, "number cannot be represented as [network] short (%s)", s > n ? "underflow" : "overflow");
		return 2;
	}

	s = htons(s);

	lua_pushlstring(L, (const char *) &s, sizeof(uint16_t));

	return 1;
}

/* }}} */

/* {{{ lsock_ntohs() */

static int
lsock_ntohs(lua_State * const L)
{
	uint16_t     h = 0;
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	if (sizeof(uint16_t) != l) /* obviously 2 bytes... */
	{
		lua_pushnil(L);
		lua_pushstring(L, "string length must be sizeof(uint16_t) (2 bytes)");
		return 2;
	}

	h = ntohs(*((uint16_t *) s));

	lua_pushnumber(L, h);

	return 1;
}

/* }}} */

/* {{{ lsock_htonl() */

static int
lsock_htonl(lua_State * const L)
{
	lua_Number n = luaL_checknumber(L, 1);
	uint32_t   l = n;

	if (l != n) /* type promotion back to lua_Number */
	{
		lua_pushnil(L);
		lua_pushfstring(L, "number cannot be represented as [network] long (%s)", l > n ? "underflow" : "overflow");
		return 2;
	}

	l = htonl(l);

	lua_pushlstring(L, (const char *) &l, sizeof(uint32_t));

	return 1;
}

/* }}} */

/* {{{ lsock_ntohl() */

static int
lsock_ntohl(lua_State * const L)
{
	uint32_t     h = 0;
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	if (sizeof(uint32_t) != l) /* 4 bytes */
	{
		lua_pushnil(L);
		lua_pushstring(L, "string length must be sizeof(uint32_t) (4 bytes)");
		return 2;
	}

	h = ntohl(*((uint32_t *) s));

	lua_pushnumber(L, h);

	return 1;
}

/* }}} */

#ifndef _WIN32 

/* {{{ lsock_sendfile() */

/* USERS SHOULD SET THE NEW OFFSET OF `in' AFTER SENDFILE()'ING */

static int
lsock_sendfile(lua_State * const L)
{
	int    out    = LSOCK_CHECKFD(L, 1);
	int    in     = LSOCK_CHECKFD(L, 2);
	off_t  offset = luaL_checkint(L, 3);
	size_t count  = luaL_checkint(L, 4);

	/* I know sendfile() acts different when offset is NULL,
	** but you can get the current offset of `in' from Lua with :seek() */
	ssize_t sent = sendfile(out, in, &offset, count);

	if (-1 == sent)
		return LSOCK_STRERROR(L, NULL);

	lua_pushnumber(L, sent);
	lua_pushnumber(L, offset); /* just for aqua */

	return 2;
}

/* }}} */

#endif

/* {{{ lsock_sockaddr() -> lsock_sockaddr userdata */

static int
lsock_sockaddr(lua_State * const L)
{
	/* I could be smart about this and allocate only as per address-family usage,
	** but if all of these sockaddr structures are the same size I can freely
	** convert between them depending on member assignment from Lua */
	(void) LSOCK_NEWSOCKADDR(L);

	return 1;
}

/* }}} */

/* {{{ lsock_timeval() -> lsock timeval userdata */

static int
lsock_timeval(lua_State * const L)
{
	(void) LSOCK_NEWTIMEVAL(L);

	return 1;
}

/* }}} */

/* {{{ lsock_linger() -> lsock linger userdata */

static int
lsock_linger(lua_State * const L)
{
	(void) LSOCK_NEWLINGER(L);

	return 1;
}

/* }}} */

/* {{{ lsock__timeval_getset() */

static const char * const timeval_fields[] = { "tv_sec", "tv_usec" };

enum TIMEVAL_OPTS { TV_SEC, TV_USEC };

static int
lsock__timeval_getset(lua_State * const L)
{
	struct timeval * const  t = LSOCK_CHECKTIMEVAL(L, 1                      );
	const int        o = luaL_checkoption  (L, 2, NULL, timeval_fields);
	const int newindex = !lua_isnone       (L, 3                      );

	if (!newindex)
	{
		lua_pushnumber(L, TV_SEC == o ?  t->tv_sec : t->tv_usec);
		return 1;
	}

	if (lua_isnil(L, 3))
	{
		if (TV_SEC == o) t->tv_sec  = 0;
		else             t->tv_usec = 0;
	}
	else
	{
		lua_Number n = luaL_checkinteger(L, 3);

		if (TV_SEC == o) t->tv_sec  = n;
		else             t->tv_usec = n;
	}

	return 0;
}

/* }}} */

/* {{{ lsock__linger_getset() */

static const char * const linger_fields[] = { "l_onoff", "l_linger" };

enum LINGER_OPTS { L_ONOFF, L_LINGER };

static int
lsock__linger_getset(lua_State * const L)
{
	struct linger * const l        = LSOCK_CHECKLINGER(L, 1);
	const int      o        = luaL_checkoption(L, 2, NULL, linger_fields);
	const int      newindex = !lua_isnone(L, 3);

	if (!newindex)
	{
		lua_pushnumber(L, L_ONOFF == o ? l->l_onoff : l->l_linger);
		return 1;
	}

	if (lua_isnil(L, 3))
	{
		if (L_ONOFF == o) l->l_onoff  = 0;
		else              l->l_linger = 0;
	}
	else
	{
		lua_Number n = luaL_checknumber(L, 3);

		if (L_ONOFF == o) l->l_onoff  = n;
		else              l->l_linger = n;
	}

	return 0;
}

/* }}} */

/* {{{ lsock__sockaddr_getset() */

static const char * const sockaddr_fields[] =
{
	  "ss_family",                                                             /* sockaddr_storage structure */
	  "sa_family",   "sa_data",                                                /* sockaddr         structure */
	 "sin_family",  "sin_port", "sin_addr",                                    /* sockaddr_in      structure */
	"sin6_family", "sin6_port", "sin6_flowinfo", "sin6_addr", "sin6_scope_id", /* sockaddr_in6     structure */
	 "sun_family",  "sun_path",                                                /* sockaddr_un      structure */
	NULL
};

enum SOCKADDR_OPTS
{
	  SS_FAMILY,
	  SA_FAMILY,   SA_DATA,
	 SIN_FAMILY,  SIN_PORT, SIN_ADDR,
	SIN6_FAMILY, SIN6_PORT, SIN6_FLOWINFO, SIN6_ADDR, SIN6_SCOPE_ID,
	 SUN_FAMILY,  SUN_PATH
};

/* this function handles both __index and __newindex,
** we take advantage of the requirement of Lua's syntax
** that object.member = is invalid, by checking for
** "nothing" as the third arg */
static int
lsock__sockaddr_getset(lua_State * const L)
{
	union LSockAddr * const p = LSOCK_CHECKSOCKADDR(L, 1);
	int                     o = luaL_checkoption(L, 2, NULL, sockaddr_fields);
	int              newindex = !lua_isnone(L, 3);
	const int              af = p->storage.ss_family; /* we should be working from this!, not `o' */

	switch (o)
	{
		/* same place and size in memory */
		case   SS_FAMILY:
		case   SA_FAMILY:
		case  SUN_FAMILY:
		case  SIN_FAMILY:
		case SIN6_FAMILY:

			if (newindex)
				p->storage.ss_family = lua_isnil(L, 3) ? 0 : luaL_checkinteger(L, 3);
			else
				lua_pushinteger(L, p->storage.ss_family);

			break;

		case  SIN_PORT:
		case SIN6_PORT:

			if ((o == SIN_PORT && af != AF_INET) || (o == SIN6_PORT && af != AF_INET6))
				luaL_error(L, "invalid index \"%s\"; wrong address family", lua_tostring(L, 2));
			
			if (newindex)
			{
				short s = lua_isnil(L, 3) ? 0 : htons(luaL_checkinteger(L, 3));

				if (SIN_PORT == o) p->in.sin_port   = s;
				else               p->in6.sin6_port = s;
			}
			else
				lua_pushinteger(L, ntohs(o == SIN_PORT ? p->in.sin_port : p->in6.sin6_port));

			break;

		case  SIN_ADDR:
		case SIN6_ADDR:
			if ((o == SIN_ADDR && af != AF_INET) || (o == SIN6_ADDR && af != AF_INET6))
				luaL_error(L, "invalid index \"%s\"; wrong address family", lua_tostring(L, 2));

			if (newindex)
			{
				size_t l = 0;
				const char * src = luaL_checklstring(L, 3, &l);
				size_t sz = 0;
				void * dst = NULL;
				int stat;

				if (af == AF_INET)
				{
					dst = &p->in.sin_addr;
					sz  = sizeof(p->in.sin_addr);
				}
				else
				{
					dst = &p->in6.sin6_addr;
					sz  = sizeof(p->in6.sin6_addr);
				}

				/* NUL this just to be sure */
				BZERO(dst, sz);

				stat = inet_pton(af, src, dst);

				if (1 != stat) /* success is 1, funnily enough */
				{
					if (0 == stat)
						luaL_error(L, "invalid address for address family (AF_INET%s): %s", o == SIN_ADDR ? "" : "6", src);
					else
						luaL_error(L, strerror(errno));
				}
			}
			else
			{
				/* +1 for good measure? */
				char dst[INET6_ADDRSTRLEN];

				void * src = NULL;

				if (AF_INET == af) src = &p->in.sin_addr;
				else               src = &p->in6.sin6_addr;

				BZERO(dst, sizeof(dst));

				if (NULL == inet_ntop(af, src, dst, sizeof(dst)))
					luaL_error(L, strerror(errno));

				lua_pushstring(L, dst);
			}

			break;
			
		case  SA_DATA:
		case SUN_PATH:

			{
				size_t sz = SA_DATA == o ? 14 : 108;

				if (newindex)
				{

					size_t l = 0;
					const char * s = luaL_checklstring(L, 3, &l);
					void * dst = p->addr.sa_data;

					/* .sun_path gets an address family check, .sa_data does not */
					if (o == SUN_PATH)
					{
						if (af != AF_UNIX)
							luaL_error(L, "invalid index \"sun_path\"; address family is not AF_UNIX");

						if (l > 108)
							luaL_error(L, "%s (greater than 108 bytes)", strerror(ENAMETOOLONG));

						if (s[l] != NUL)
							luaL_error(L, "field \"sun_path\" must be NUL-terminated");

						dst = &p->un.sun_path;
					}

					BZERO(dst, sz); /* clear this first */

					memcpy(dst, s, l);
				}
				else
				{
					void * src = NULL;

					if (o == SUN_PATH) src = &p->addr.sa_data;
					else               src = &p->un.sun_path;

					lua_pushlstring(L, (const char *) src, sz);
				}
			}

			break;

		case SIN6_FLOWINFO:
		case SIN6_SCOPE_ID:

			if (af != AF_INET6)
				luaL_error(L, "invalid index \"%s\"; address family is not AF_INET6", lua_tostring(L, 2));

			if (newindex)
			{
				long int l = lua_isnil(L, 3) ? 0 : htonl(luaL_checknumber(L, 3));

				if (SIN6_FLOWINFO == o) p->in6.sin6_flowinfo = l;
				else                    p->in6.sin6_scope_id = l;
			}
			else
				lua_pushnumber(L, ntohl(o == SIN6_FLOWINFO ? p->in6.sin6_flowinfo : p->in6.sin6_scope_id));

			break;
			
	}

	return newindex ? 0 : 1;
}

/* }}} */

/* {{{ lsock_accept() */

static int
lsock_accept(lua_State * const L)
{
	socklen_t         sz      = 0;
	lsocket           serv    = LSOCK_CHECKSOCKET(L, 1);
	union LSockAddr * info    = NULL;
	luaL_Stream     * newfh   = NULL;

	lsocket           newsock = accept(serv, (struct sockaddr *) info, &sz);

	if (INVALID_SOCKET(newsock))
		return LSOCK_STRERROR(L, NULL);

	newfh = newstream(L);
	info  = LSOCK_NEWSOCKADDR(L);
	sz    = lua_rawlen(L, -1); /* just because */

	newfh->f = lsocket_to_stream(L, newsock);

	return 2;
}

/* }}} */

/* {{{ lsock_listen(sock, backlog_number) */

static int
lsock_listen(lua_State * const L)
{
	lsocket      serv = LSOCK_CHECKSOCKET(L, 1);
	const int backlog = luaL_checknumber(L, 2);

	if (listen(serv, backlog))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_bind() */

/* bind(sock, sockaddr_userdata) -> true -or- nil, 'error message', error_constant */

static int
lsock_bind(lua_State * const L)
{
	lsocket                 serv =   LSOCK_CHECKSOCKET(L, 1);
	union LSockAddr * const addr = LSOCK_CHECKSOCKADDR(L, 2);

	if (bind(serv, (const struct sockaddr *) addr, lua_rawlen(L, 2)))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);
	
	return 1;
}

/* }}} */

/* {{{ lsock_connect(sock, sockaddr_userdata) -> true -or- nil, 'error message', error_constant */

/* identical to lsock_bind(), pretty much */

static int
lsock_connect(lua_State * const L)
{
	lsocket                 client = LSOCK_CHECKSOCKET(L, 1);
	union LSockAddr * const addr   = LSOCK_CHECKSOCKADDR(L, 2);

	if (connect(client, (const struct sockaddr *) addr, lua_rawlen(L, 2))) /* rawlen() for safety */
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_recv() */

static int
lsock_recv(lua_State * const L)
{
	lsocket sock  = LSOCK_CHECKSOCKET(L, 1);
	size_t length = luaL_checknumber(L, 2);
	int    flags  = luaL_checknumber(L, 3);

	char * buf    = NULL;
	int    gotten = 0;

	luaL_Buffer B;

	buf = luaL_buffinitsize(L, &B, length);

	BZERO(buf, length); /* just for the hell of it */

	gotten = recv(sock, buf, length, flags);
	
	if (-1 == gotten)
		return LSOCK_STRERROR(L, NULL);

	luaL_pushresultsize(&B, gotten);

	return 1; /* success! */
}

/* }}} */

/* {{{ lsock_sendto() */

static int
lsock_sendto(lua_State * const L)
{
	size_t s_len = 0;

	lsocket           sock   = LSOCK_CHECKSOCKET(L, 1        );
	const char      * s      = luaL_checklstring(L, 2, &s_len);
	int               i      = luaL_checknumber (L, 3        );
	size_t            j      = luaL_checknumber (L, 4        );
	int               flags  = luaL_checknumber (L, 5        );
	union LSockAddr * to     = NULL;
	size_t            to_len = 0;

	ssize_t sent             = 0;

	if (!lua_isnone(L, 6))
	{
		to     = LSOCK_CHECKSOCKADDR(L, 6);
		to_len = lua_rawlen(L, 6); /* important */
	}

	sent = 0;

	if (i < 1 || j > s_len)
		luaL_error(L, "out of bounds (%d,%d), send data is %d bytes", i, j, s_len);

	sent = sendto(sock, s + (i - 1), j - (i + 1), flags, (const struct sockaddr *) to, to_len);

	if (-1 == sent)
		return LSOCK_STRERROR(L, NULL);

	lua_pushnumber(L, sent);

	return 1; /* success! */
}

/* }}} */

/* {{{ lsock_shutdown() */

/* shutdown(sock, how) -> true  -or-  nil, errno */
/* lsock/init.lua wraps this and sets it as the __gc for sockets */

static int
lsock_shutdown(lua_State * const L)
{
	lsocket sock = LSOCK_CHECKSOCKET(L, 1);
	int     how  =  luaL_checknumber(L, 2);

	if (shutdown(sock, how))
		return LSOCK_STRERROR(L, NULL);
	
	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_socket() */

/* socket('AF_INET', 'SOCK_STREAM', 0) -> 29 */

static int
lsock_socket(lua_State * const L)
{
	int domain   = luaL_checknumber(L, 1),
		type     = luaL_checknumber(L, 2),
		protocol = luaL_checknumber(L, 3);

	lsocket       sock   = socket(domain, type, protocol);
	luaL_Stream * stream = NULL;

	if (INVALID_SOCKET(sock))
		return LSOCK_STRERROR(L, NULL);

	stream = newstream(L);

	stream->f = lsocket_to_stream(L, sock);

	return 1;
}

/* }}} */

/* {{{ lsock_strerror() */

static int
lsock_strerror(lua_State * const L)
{
	lua_pushstring(L, strerror(luaL_checknumber(L, 1)));

	return 1;
}

/* }}} */

/* {{{ lsock_gai_strerror() */

static int
lsock_gai_strerror(lua_State * const L)
{
	lua_pushstring(L, gai_strerror(luaL_checkint(L, 1)));

	return 1;
}

/* }}} */

/* {{{ lsock_getfd() */

/* for working with (some) luasocket stuff */

static int
lsock_getfd(lua_State * const L)
{
	luaL_Stream * const file = LSOCK_CHECKSTREAM(L, 1);

	lua_pushnumber(L, stream_to_fd(L, file->f));

	return 1;
}

/* }}} */

#ifdef _WIN32

/* {{{ lsock_shutdown() */

static void
lsock_cleanup(lua_State * const L)
{
	((void) L);

	WSACleanup();
}

/* }}} */

#endif

/* {{{ luaopen_lsock_core() */

#define LUA_REG(x) { #x, lsock_##x }

static const luaL_Reg lsocklib[] =
{
	/* alphabetical */
	LUA_REG(accept),
	LUA_REG(bind),
	LUA_REG(connect),
	LUA_REG(gai_strerror),
	LUA_REG(getfd),
	LUA_REG(linger),
	LUA_REG(listen),
	LUA_REG(recv),
	LUA_REG(sendfile),
	LUA_REG(sendto),
	LUA_REG(shutdown),
	LUA_REG(sockaddr),
	LUA_REG(socket),
	LUA_REG(strerror),
	LUA_REG(timeval),
	LUA_REG(htons),
	LUA_REG(ntohs),
	LUA_REG(htonl),
	LUA_REG(ntohl),
	LUA_REG(_sockaddr_getset),
	LUA_REG(_timeval_getset),
	LUA_REG(_linger_getset),
	{ NULL, NULL }
};

#undef LUA_REG


LUALIB_API int
luaopen_lsock_core(lua_State * const L)
{

#ifdef _WIN32

	WSADATA wsaData;

	int stat = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (stat != 0)
	{
		WSACleanup();
		luaL_error(L, "WSAStartup() failed [%d]: %s\n", stat, strerror(stat));
	}

	if
	(
		2 != LOBYTE(wsaData.wVersion) ||
		2 != HIBYTE(wsaData.wVersion)
	)
	{
		WSACleanup();
		luaL_error(L, "Could not find a usable version of Winsock.dll");
	}

#endif

	luaL_newlib(L, lsocklib);

#define LSOCK_CONST(C) \
    lua_pushinteger(L, C);  \
    lua_setfield(L, -2, #C)

	/* protocol family constants */
	LSOCK_CONST(PF_APPLETALK);
	LSOCK_CONST(PF_INET     );
	LSOCK_CONST(PF_INET6    );
	LSOCK_CONST(PF_IPX      );
	LSOCK_CONST(PF_IRDA     );
	LSOCK_CONST(PF_UNIX     );
	LSOCK_CONST(PF_UNSPEC   );

	/* address family constants */
	LSOCK_CONST(AF_APPLETALK);
	LSOCK_CONST(AF_INET     );
	LSOCK_CONST(AF_INET6    );
	LSOCK_CONST(AF_IPX      );
	LSOCK_CONST(AF_IRDA     );
	LSOCK_CONST(AF_UNIX     );
	LSOCK_CONST(AF_UNSPEC   );

	/* socket type constants */
	LSOCK_CONST(SOCK_DGRAM    );
	LSOCK_CONST(SOCK_RAW      );
	LSOCK_CONST(SOCK_RDM      );
	LSOCK_CONST(SOCK_SEQPACKET);
	LSOCK_CONST(SOCK_STREAM   );

	/* protocol constants */
	LSOCK_CONST(IPPROTO_ICMP  );
	LSOCK_CONST(IPPROTO_ICMPV6);
	LSOCK_CONST(IPPROTO_IGMP  );
	LSOCK_CONST(IPPROTO_TCP   );
	LSOCK_CONST(IPPROTO_UDP   );

	/* errno's, alphabetical */
	LSOCK_CONST(EACCES         );
	LSOCK_CONST(EADDRINUSE     );
	LSOCK_CONST(EADDRNOTAVAIL  );
	LSOCK_CONST(EAFNOSUPPORT   );
	LSOCK_CONST(EAGAIN         );
	LSOCK_CONST(EBADF          );
	LSOCK_CONST(ECONNABORTED   );
	LSOCK_CONST(EDESTADDRREQ   );
	LSOCK_CONST(EINVAL         );
	LSOCK_CONST(EINTR          );
	LSOCK_CONST(EIO            );
	LSOCK_CONST(EISDIR         );
	LSOCK_CONST(ELOOP          );
	LSOCK_CONST(EMFILE         );
	LSOCK_CONST(ENAMETOOLONG   );
	LSOCK_CONST(ENFILE         );
	LSOCK_CONST(ENOBUFS        );
	LSOCK_CONST(ENOENT         );
	LSOCK_CONST(ENOTDIR        );
	LSOCK_CONST(ENOTSOCK       );
	LSOCK_CONST(ENOBUFS        );
	LSOCK_CONST(ENOMEM         );
	LSOCK_CONST(ENOTCONN       );
	LSOCK_CONST(ENOTSOCK       );
	LSOCK_CONST(EOPNOTSUPP     );
	LSOCK_CONST(EPROTO         );
	LSOCK_CONST(EPROTONOSUPPORT);
	LSOCK_CONST(EPROTOTYPE     );
	LSOCK_CONST(EROFS          );
	LSOCK_CONST(EWOULDBLOCK    );

	/* getaddrinfo() constants */
	LSOCK_CONST(AI_ADDRCONFIG              );
	LSOCK_CONST(AI_ALL                     );
	LSOCK_CONST(AI_CANONIDN                );
	LSOCK_CONST(AI_CANONNAME               );
	LSOCK_CONST(AI_IDN                     );
	LSOCK_CONST(AI_IDN_ALLOW_UNASSIGNED    );
	LSOCK_CONST(AI_IDN_USE_STD3_ASCII_RULES);
	LSOCK_CONST(AI_NUMERICHOST             );
	LSOCK_CONST(AI_NUMERICSERV             );
	LSOCK_CONST(AI_PASSIVE                 );
	LSOCK_CONST(AI_V4MAPPED                );

	/* getaddrinfo() errors */
	LSOCK_CONST(EAI_ADDRFAMILY);
	LSOCK_CONST(EAI_AGAIN     );
	LSOCK_CONST(EAI_BADFLAGS  );
	LSOCK_CONST(EAI_FAIL      );
	LSOCK_CONST(EAI_FAMILY    );
	LSOCK_CONST(EAI_MEMORY    );
	LSOCK_CONST(EAI_NODATA    );
	LSOCK_CONST(EAI_NONAME    );
	LSOCK_CONST(EAI_OVERFLOW  );
	LSOCK_CONST(EAI_SERVICE   );
	LSOCK_CONST(EAI_SOCKTYPE  );
	LSOCK_CONST(EAI_SYSTEM    );

	/* send() & recv() flag constants */
	LSOCK_CONST(MSG_EOR    );
	LSOCK_CONST(MSG_OOB    );
	LSOCK_CONST(MSG_PEEK   );
	LSOCK_CONST(MSG_WAITALL);

	LSOCK_CONST(SHUT_RD  );
	LSOCK_CONST(SHUT_RDWR);
	LSOCK_CONST(SHUT_WR  );

	/* miscellaneous extras */
	lua_pushstring(L, "0.0.0.0");   lua_setfield(L, -2, "INADDR_ANY");
	lua_pushstring(L, "127.0.0.1"); lua_setfield(L, -2, "INADDR_LOOPBACK");
	lua_pushstring(L, "::");        lua_setfield(L, -2, "in6addr_any");
	lua_pushstring(L, "::1");       lua_setfield(L, -2, "in6addr_loopback");

#undef LSOCK_CONST

#ifdef _WIN32

	lua_puchcfunction(L, &lsock_cleanup);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1)
	lua_setmetatable(L, -1) /* it is its own metatable */

#endif

	return 1;
}

/* }}} */
