/* compile: gcc -o lsock.{so,c} -shared -fPIC -pedantic -std=c89 -W -Wall -Wextra -Werror -llua -fstack-protector-all -fvisibility=hidden -Os -s */

#define LSOCK_EXPORT __attribute__((visibility("default")))

/* cross-platform includes */
#include <sys/types.h>
#include <errno.h>
#include <lauxlib.h>
#include <lualib.h>

/* OS-specific includes and definitions */
#if _WIN32
#	pragma comment(lib, "Ws2_32.lib")
#	define _CRT_SECURE_NO_WARNINGS /* blah!! */
#	define WIN32_LEAN_AND_MEAN /* avoid MVC stuffs */
#	include <windows.h>
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <io.h>             /* _open_osfhandle() / _get_osfhandle() */
#endif

#ifdef __APPLE__
#	define __APPLE_USE_RFC_2292
#	include <sys/uio.h>
#endif

#ifdef __linux
#	define _POSIX_SOURCE
#	define _GNU_SOURCE
#	include <stropts.h>
#	include <sys/sendfile.h>
#endif

/* Mac OS X + Linux */
#ifndef _WIN32
#	include <arpa/inet.h>
#	include <sys/socket.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <netinet/udp.h>
#	include <net/if.h>
#	include <sys/types.h>
#	include <sys/un.h>
#	include <string.h>
#	include <unistd.h>
#	include <sys/time.h>
#	include <fcntl.h>
#	include <sys/ioctl.h>
#	include <sys/select.h>
#endif

#define NUL                               '\0'
#define BZERO(buf, sz)                    memset(buf, NUL, sz)
#define LSOCK_SIZEOF_MEMBER(type, member) sizeof(((type *) NULL)->member)

#define UNIX_PATH_MAX           LSOCK_SIZEOF_MEMBER(struct sockaddr_un, sun_path)
#define LSOCK_ARRAY_LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))

#ifdef _WIN32
typedef SOCKET lsocket;
typedef SSIZE_T ssize_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int16 uint16_t;
#else
typedef int lsocket;
#endif

#define LSOCK_NEWUDATA(L, sz) BZERO(lua_newuserdata(L, sz), sz)

#define   LSOCK_CHECKFH(L, index) ((luaL_Stream *) luaL_checkudata(L, index, LUA_FILEHANDLE))
#define LSOCK_CHECKSOCK(L, index) file_to_sock(L, LSOCK_CHECKFH(L, index)->f)
#define   LSOCK_CHECKFD(L, index) file_to_fd(L, LSOCK_CHECKFH(L, index)->f)

#define LSOCK_STRERROR(L, fname) lsock_error(L, LSOCK_NET_ERROR, (char * (*)(int)) &strerror,     fname)
#define LSOCK_GAIERROR(L, err  ) lsock_error(L, err,             (char * (*)(int)) &gai_strerror, NULL )
#define LSOCK_STRFATAL(L, fname) lsock_fatal(L, LSOCK_NET_ERROR, (char * (*)(int)) &strerror,     fname)

/* combination of lua_push*() + lua_setfield() macros */
#define PUSHFIELD_NUM(L, tidx, key, val) \
	do { lua_pushnumber(L, val); lua_setfield(L, -1 + (tidx), key); } while (0)

#define PUSHFIELD_INT(L, tidx, key, val) \
	do { lua_pushinteger(L, val); lua_setfield(L, -1 + (tidx), key); } while (0)

#define PUSHFIELD_PSTR(L, tidx, key, val) \
	do { lua_pushstring(L, val); lua_setfield(L, -1 + (tidx), key); } while (0)

#define PUSHFIELD_NSTR(L, tidx, key, val, val_len) \
	do { lua_pushlstring(L, val, val_len); lua_setfield(L, -1 + (tidx), key); } while (0)

#define PUSHFIELD_LSTR(L, tidx, key, val) \
	do { lua_pushliteral(L, val); lua_setfield(L, -1 + (tidx), key); } while (0)

#ifdef _WIN32
#	define LSOCK_OPERATION_FAILED(s) (SOCKET_ERROR == (s))
#	define LSOCK_CREATION_FAILED(s)  (INVALID_SOCKET == (s))
#	define LSOCK_NET_ERROR           WSAGetLastError()
#else
#	define LSOCK_OPERATION_FAILED(s) (s < 0)
#	define LSOCK_CREATION_FAILED(s)  (s < 0)
#	define LSOCK_NET_ERROR           (errno)
#endif

#define LSOCK_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LSOCK_MIN(a, b) ((a) < (b) ? (a) : (b))

/* including sockaddr_un in this only increases the byte count by 18 */
typedef union
{
	struct sockaddr_storage ss;
	struct sockaddr         sa;
	struct sockaddr_in      in;
	struct sockaddr_in6     in6;
#ifndef _WIN32
	struct sockaddr_un      un;
#endif
} lsockaddr;

/*
** DONE:
**			- htons()
**			- htonl()
**			- ntohs()
**			- ntohl()
**
**			- pack_sockaddr()
**			- unpack_sockaddr()
**
**			- socket()
**			- bind()
**			- shouldblock() -- for nonblocking io
**			- connect()
**			- listen()
**			- shutdown()
**			- recv()     -- wrapper over recvfrom()
**			- recvfrom()
**			- send()     -- wrapper over sendto()
**			- sendto()
**
**			- getsockopt()
**			- setsockopt()
**
**			- select() -- I might make an fd_set constructor + helper methods later on
**
**			- strerror()
**			- gai_strerror()
**
**			- sendfile()   -- only on Linux
**			- socketpair() -- only on Linux
**
**			- getsockname()
**			- getpeername()
**
**			- getaddrinfo()
**			- getnameinfo()
**
**			- bytes_available() - return how many bytes can be read (upper limit: most recv() can read)
**
** TODO:
**			- ioctl()       -- unnecessary?
**
**			- htond()       -- only on Windows
**			- htonf()       -- only on Windows
**			- htonll()      -- only on Windows
**			- ntohd()       -- only on Windows
**			- ntohf()       -- only on Windows
**			- ntohll()      -- only on Windows
*/

/*  {{{ - error stuff - */

/* {{{ lsock_error() */

static int
lsock_error(lua_State * L, int err, char * (*errfunc)(int), char * fname)
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
lsock_fatal(lua_State * L, int err, char * (*errfunc)(int), char * fname)
{
	char * msg = errfunc(err);

	if (NULL == fname)
		return luaL_error(L, msg);
	else
		return luaL_error(L, "%s: %s", fname, msg);
}

/* }}} */

/* {{{ lsock_strerror() */

static int
lsock_strerror(lua_State * L)
{
	lua_pushstring(L, strerror(luaL_checkint(L, 1)));

	return 1;
}

/* }}} */

/* {{{ lsock_gai_strerror() */

static int
lsock_gai_strerror(lua_State * L)
{
	lua_pushstring(L, (char *) gai_strerror(luaL_checkint(L, 1)));

	return 1;
}

/* }}} */

/* }}} */

/* {{{ strij_to_payload() */

/* this is evil, unreadable, and clever; ripped from 5.2 */
static size_t
posrelat(ptrdiff_t pos, size_t len)
{
	if (pos >= 0)
		return (size_t) pos;
	else if (0u - (size_t) pos > len)
		return 0;
	else
		return len - ((size_t) -pos) + 1;
}

static void
strij_to_payload(lua_State * L, int idx, const char ** s, size_t * count)
{
	int t;

	size_t l = 0;
	size_t i;
	size_t j;
	const char * str;

	idx = lua_absindex(L, idx);
	t   = lua_type(L, idx);

	luaL_argcheck(L, LUA_TSTRING == t || LUA_TTABLE == t, idx, "string or table expected");

	if (LUA_TSTRING == t)
	{
		*s = luaL_optlstring(L, idx, "", count);
		return;
	}

	/* for safey? */
	*s     = "";
	*count = 0;

	/* from here we assume table -- see above argcheck() */

	/* ---- */

	lua_pushnumber(L, 1);
	lua_gettable(L, idx);

	str = luaL_optlstring(L, -1, "", &l);
	lua_pop(L, 1);

	/* ---- */

	lua_pushnumber(L, 2);
	lua_gettable(L, idx);

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_getfield(L, idx, "i");
	}

	i = posrelat(luaL_optint(L, -1, 1), 1);
	lua_pop(L, 1);

	/* ---- */

	lua_pushnumber(L, 3);
	lua_gettable(L, idx);

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_getfield(L, idx, "j");
	}

	j = posrelat(luaL_optint(L, -1, l), l);
	lua_pop(L, 1);

	/* ---- */

	/* pretty identical to string.sub() here */
	i = LSOCK_MAX(i, 1);
	j = LSOCK_MIN(j, l);

	/* string.sub('abcdefghij', -4, -6) -> string.sub('abcdefghij', 7, 5) -> ''
	** we are asserting that i comes before j */
	if (i > j)
		return;

	*s     = (str - 1) + i;
	*count = (j   - i) + 1;
}

/* }}} */

/* {{{ - FILE * <-> fd <-> SOCKET stuff - */

/* going to and from SOCKET and fd in Windows: _open_osfhandle()/_get_osfhandle() */

/* {{{ file_to_fd() */

static int
file_to_fd(lua_State * L, FILE * stream)
{
	int fd = -1;

#ifdef _WIN32
	fd = _fileno(stream);
#else
	fd = fileno(stream);
#endif

	if (-1 == fd)
#ifdef _WIN32
		LSOCK_STRFATAL(L, "_fileno()");
#else
		LSOCK_STRFATAL(L, "fileno()");
#endif

	return fd;
}

/* }}} */

/* {{{ fd_to_file() */

static FILE *
fd_to_file(lua_State * L, int fd, char * mode)
{
	FILE * stream = NULL;

	if (NULL == mode)
		mode = "r+b";

#ifdef _WIN32
	stream = _fdopen(fd, mode);
#else
	stream = fdopen(fd, mode);
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

/* {{{ sock_to_fd() */

static int
sock_to_fd(lua_State * L, lsocket sock)
{

#ifdef _WIN32
	int fd = -1;

	fd = _open_osfhandle(sock, 0); /* no _O_APPEND, _O_RDONLY, _O_TEXT */

	if (-1 == fd)
		LSOCK_STRFATAL(L, "_open_osfhandle()");

	return fd;
#else
	(void) L;

	return sock;
#endif

}

/* }}} */

/* {{{ fd_to_sock() */

static lsocket
fd_to_sock(lua_State * L, int fd)
{

#ifdef _WIN32
	/* for the record: I feel bad about this */
	uintptr_t l = _get_osfhandle(fd);

	if (INVALID_HANDLE_VALUE == (HANDLE) l)
		return LSOCK_STRFATAL(L, "_get_osfhandle()");

	return (lsocket) l;
#else
	(void) L;

	return fd; /* hah */
#endif

}

/* }}} */

/* {{{ file_to_sock() */

static lsocket
file_to_sock(lua_State * L, FILE * stream)
{
	/* wheee shortness. */
	return fd_to_sock(L, file_to_fd(L, stream));
}

/* }}} */

/* {{{ sock_to_file() */

static FILE *
sock_to_file(lua_State * L, lsocket sock, char * mode)
{
	/* \o/ \o| |o/ \o/ \o| |o/ \o/ */
	return fd_to_file(L, sock_to_fd(L, sock), mode);
}

/* }}} */

/* }}} */

/* {{{ - table <-> structure stuff - */

/* {{{ timeval_to_table() */

/* not currently used.... */
#if 0
static void
timeval_to_table(lua_State * L, struct timeval * t)
{
	lua_createtable(L, 0, 2);

	PUSHFIELD_NUM(L, -1, "tv_sec",  t->tv_sec );
	PUSHFIELD_NUM(L, -1, "tv_usec", t->tv_usec);
}
#endif

/* }}} */

/* {{{ table_to_timeval() */

static struct timeval *
table_to_timeval(lua_State * L, int idx)
{
	struct timeval * t;

	idx = lua_absindex(L, idx);

	t = (struct timeval *) LSOCK_NEWUDATA(L, sizeof(struct timeval));

	lua_getfield(L, idx, "tv_sec");

	if (!lua_isnil(L, -1))
		t->tv_sec = (long) lua_tonumber(L, -1);

	lua_pop(L, 1);

	lua_getfield(L, idx, "tv_usec");

	if (!lua_isnil(L, -1))
		t->tv_usec = (long) lua_tonumber(L, -1);

	lua_pop(L, 1);

	return t;
}

/* }}} */

/* {{{ linger_to_table() */

static void
linger_to_table(lua_State * L, struct linger * l)
{
	lua_createtable(L, 0, 2);

	PUSHFIELD_NUM(L, -1, "l_onoff",  l->l_onoff );
	PUSHFIELD_NUM(L, -1, "l_linger", l->l_linger);
}

/* }}} */

/* {{{ table_to_linger() */

static struct linger *
table_to_linger(lua_State * L, int idx)
{
	struct linger * l;

	idx = lua_absindex(L, idx);

	l = (struct linger *) LSOCK_NEWUDATA(L, sizeof(struct linger));

	lua_getfield(L, idx, "l_onoff");

	if (!lua_isnil(L, -1))
		l->l_onoff = (u_short) lua_tonumber(L, -1);

	lua_pop(L, 1);

	lua_getfield(L, idx, "l_linger");

	if (!lua_isnil(L, -1))
		l->l_linger = (u_short) lua_tonumber(L, -1);

	lua_pop(L, 1);

	return l;
}

/* }}} */

/* {{{ sockaddr_to_table() */

static int
sockaddr_to_table(lua_State * L, const char * sa, size_t lsa_sz)
{
	lsockaddr * lsa = (lsockaddr *) sa;

	if (lsa_sz < LSOCK_SIZEOF_MEMBER(struct sockaddr, sa_family))
		return 0;

	switch (lsa->sa.sa_family)
	{
#ifndef _WIN32
		case AF_UNIX:  if (lsa_sz < sizeof(struct sockaddr_un))  goto invalid_sockaddr; break;
#endif
		case AF_INET:  if (lsa_sz < sizeof(struct sockaddr_in))  goto invalid_sockaddr; break;
		case AF_INET6: if (lsa_sz < sizeof(struct sockaddr_in6)) goto invalid_sockaddr; break;
		default:       if (lsa_sz < sizeof(lsockaddr))           goto invalid_sockaddr; break;
invalid_sockaddr:
		return 0;
	}

	lua_createtable(L, 0, 7); /* 5 fields in sockaddr_in6 + 2 in sockaddr_storage */

	PUSHFIELD_NUM(L, -1, "ss_family", lsa->ss.ss_family);
	PUSHFIELD_NUM(L, -1, "sa_family", lsa->sa.sa_family);

	PUSHFIELD_NSTR(L, -1, "sa_data", lsa->sa.sa_data, LSOCK_SIZEOF_MEMBER(lsockaddr, sa.sa_data));

	switch (lsa->ss.ss_family)
	{
		case AF_INET:
			{
				char dst[INET_ADDRSTRLEN];

				BZERO(dst, sizeof(dst));

				PUSHFIELD_NUM(L, -1, "sin_family", lsa->in.sin_family);
				PUSHFIELD_NUM(L, -1, "sin_port",   ntohs(lsa->in.sin_port));

#ifdef _WIN32
				if (NULL == InetNtop(AF_INET, &lsa->in.sin_addr, (PWSTR) dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "InetNtop()");
#else
				if (NULL == inet_ntop(AF_INET, &lsa->in.sin_addr, dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "inet_ntop()");
#endif

				PUSHFIELD_PSTR(L, -1, "sin_addr", dst);
			}

			break;

		case AF_INET6:
			{
				char dst[INET6_ADDRSTRLEN];

				BZERO(dst, sizeof(dst));

				PUSHFIELD_NUM(L, -1, "sin6_family",   lsa->in6.sin6_family         );
				PUSHFIELD_NUM(L, -1, "sin6_port",     ntohs(lsa->in6.sin6_port)    );
				PUSHFIELD_NUM(L, -1, "sin6_flowinfo", ntohl(lsa->in6.sin6_flowinfo));
				PUSHFIELD_NUM(L, -1, "sin6_scope_id", ntohl(lsa->in6.sin6_scope_id));

#ifdef _WIN32
				if (NULL == InetNtop(AF_INET6, (char *) &lsa->in6.sin6_addr, (PWSTR) dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "InetNtop()");
#else
				if (NULL == inet_ntop(AF_INET6, (char *) &lsa->in6.sin6_addr, dst, sizeof(dst)))
					LSOCK_STRFATAL(L, "inet_ntop()");
#endif

				PUSHFIELD_PSTR(L, -1, "sin6_addr", dst);
			}

			break;

#ifndef _WIN32
		case AF_UNIX:
			PUSHFIELD_NUM(L, -1, "sun_family", lsa->un.sun_family);
			PUSHFIELD_NSTR(L, -1, "sun_path", lsa->un.sun_path, UNIX_PATH_MAX);

			break;
#endif
	}

	return 1;
}

/* }}} */

/* {{{ table_to_sockaddr() */

static char * members[] =
{
	"ss_family",
	"sa_family",   "sa_data",
	"sin_family",  "sin_port",  "sin_addr",
	"sin6_family", "sin6_port", "sin6_flowinfo", "sin6_addr", "sin6_scope_id",
#ifndef _WIN32
	"sun_family",  "sun_path"
#endif
};

enum SOCKADDR_FIELDS
{
	SS_FAMILY,
	SA_FAMILY,   SA_DATA,
	SIN_FAMILY,  SIN_PORT,  SIN_ADDR,
	SIN6_FAMILY, SIN6_PORT, SIN6_ADDR, SIN6_FLOWINFO, SIN6_SCOPE_ID,
	SUN_FAMILY,  SUN_PATH
};

static const char *
table_to_sockaddr(lua_State * L, int idx)
{
	unsigned int i;

	size_t out_sz = 0;
	lsockaddr lsa;

	BZERO(&lsa, sizeof(lsa));

	idx = lua_absindex(L, idx);

	for (i = 0; i < LSOCK_ARRAY_LENGTH(members); i++)
	{
		lua_getfield(L, idx, members[i]);

		if (lua_isnil(L, -1))
			goto next_member;

		switch (i)
		{
			case   SS_FAMILY:
			case   SA_FAMILY:
			case  SIN_FAMILY:
			case SIN6_FAMILY:
#ifndef _WIN32
			case  SUN_FAMILY:
#endif
				lsa.ss.ss_family = (u_short) luaL_checkint(L, -1);
				break;

			case  SIN_PORT:   lsa.in.sin_port = (u_short) htons(luaL_checkint(L, -1)); break;
			case SIN6_PORT: lsa.in6.sin6_port = (u_short) htons(luaL_checkint(L, -1)); break;

			case SIN6_FLOWINFO: lsa.in6.sin6_flowinfo = htonl(luaL_checklong(L, -1)); break;
			case SIN6_SCOPE_ID: lsa.in6.sin6_scope_id = htonl(luaL_checklong(L, -1)); break;

			case  SA_DATA:
#ifndef _WIN32
			case SUN_PATH:
#endif
				{
					size_t sz = 0;
					void       * dst = NULL;
					const char * src = NULL;

					src = luaL_checklstring(L, -1, &sz);

					if (i == SA_DATA)
					{
						dst = &lsa.sa.sa_data;
						sz  = LSOCK_MAX(LSOCK_SIZEOF_MEMBER(struct sockaddr, sa_data), sz); /* should be LSOCK_MAX(14, l) */
					}
#ifndef _WIN32
					else
					{
						dst = &lsa.un.sun_path;
						sz  = LSOCK_MAX(UNIX_PATH_MAX, sz); /* should be LSOCK_MAX(108, l) */
					}
#endif

					memcpy(dst, src, sz);
				}

				break;

			case  SIN_ADDR:
			case SIN6_ADDR:
				{
					int stat, af;
					void       * dst = NULL;
					const char * src = NULL;

					src = luaL_checkstring(L, -1);

					if (i == SIN_ADDR)
					{
						af  = AF_INET;
						dst = &lsa.in.sin_addr;
					}
					else
					{
						af  = AF_INET6;
						dst = &lsa.in6.sin6_addr;
					}

#ifdef _WIN32
					stat = InetPton(af, (PCWSTR) src, dst);
#else
					stat = inet_pton(af, src, dst);
#endif

					if (1 != stat) /* success is 1, funnily enough */
					{
						if (0 == stat)
							luaL_error(L, "invalid address for family (AF_INET%s)", i == SIN_ADDR ? "" : "6");
						else
#ifdef _WIN32
							LSOCK_STRFATAL(L, "InetPton()");
#else
							LSOCK_STRFATAL(L, "inet_pton()");
#endif
					}
				}

				break;
		}

next_member:
		lua_pop(L, 1);
	}

	switch (lsa.ss.ss_family)
	{
#ifndef _WIN32
		case AF_UNIX:  out_sz = sizeof(struct sockaddr_un);  break;
#endif
		case AF_INET:  out_sz = sizeof(struct sockaddr_in);  break;
		case AF_INET6: out_sz = sizeof(struct sockaddr_in6); break;
		default:       out_sz = sizeof(lsockaddr);
	}

	return lua_pushlstring(L, (char *) &lsa, out_sz);
}

/* }}} */

/* {{{ lsock_pack_sockaddr() */

static int
lsock_pack_sockaddr(lua_State * L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	(void) table_to_sockaddr(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_unpack_sockaddr() */

static int
lsock_unpack_sockaddr(lua_State * L)
{
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	return sockaddr_to_table(L, s, l);
}

/* }}} */

/* }}} */

/* {{{ lsock_newfile(): based directly on: newprefile() & newfile() from Lua 5.2 sources */

static int
close_stream(lua_State * L)
{
	luaL_Stream * p = LSOCK_CHECKFH(L, 1);

#ifdef _WIN32
	SOCKET s = file_to_sock(p->f);

	return luaL_fileresult(L, (LSOCK_OPERATION_FAILED(closesocket(s)) && 0 == fclose(p->f)), NULL);
#else
	return luaL_fileresult(L, (0 == fclose(p->f)), NULL);
#endif
}

static luaL_Stream *
newfile(lua_State * L)
{
	luaL_Stream * p = (luaL_Stream *) LSOCK_NEWUDATA(L, sizeof(luaL_Stream));

	luaL_setmetatable(L, LUA_FILEHANDLE);

	p->f      = NULL;
	p->closef = &close_stream;

	return p;
}

/* }}} */

/* {{{ network <-> host byte order stuff */

/* {{{ lsock_htons() */

static int
lsock_htons(lua_State * L)
{
	lua_Number n = luaL_checknumber(L, 1);
	uint16_t   s = (uint16_t) n;

	if (s != n) /* type promotion back to lua_Number */
	{
		lua_pushnil(L);
		lua_pushfstring(L, "number cannot be represented as [network] short (%s)", s > n ? "underflow" : "overflow");
		return 2;
	}

	s = htons(s);

	lua_pushlstring(L, (char *) &s, sizeof(uint16_t));

	return 1;
}

/* }}} */

/* {{{ lsock_ntohs() */

static int
lsock_ntohs(lua_State * L)
{
	uint16_t     h = 0;
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	if (sizeof(uint16_t) != l) /* obviously 2 bytes... */
	{
		lua_pushnil(L);
		lua_pushliteral(L, "string length must be sizeof(uint16_t) (2 bytes)");
		return 2;
	}

	h = ntohs(*((uint16_t *) s));

	lua_pushnumber(L, h);

	return 1;
}

/* }}} */

/* {{{ lsock_htonl() */

static int
lsock_htonl(lua_State * L)
{
	lua_Number n = luaL_checknumber(L, 1);
	uint32_t   l = (uint32_t) n;

	if (l != n) /* type promotion back to lua_Number */
	{
		lua_pushnil(L);
		lua_pushfstring(L, "number cannot be represented as [network] long (%s)", l > n ? "underflow" : "overflow");
		return 2;
	}

	l = htonl(l);

	lua_pushlstring(L, (char *) &l, sizeof(uint32_t));

	return 1;
}

/* }}} */

/* {{{ lsock_ntohl() */

static int
lsock_ntohl(lua_State * L)
{
	uint32_t     h = 0;
	size_t       l = 0;
	const char * s = luaL_checklstring(L, 1, &l);

	if (sizeof(uint32_t) != l) /* 4 bytes */
	{
		lua_pushnil(L);
		lua_pushliteral(L, "string length must be sizeof(uint32_t) (4 bytes)");
		return 2;
	}

	h = ntohl(*((uint32_t *) s));

	lua_pushnumber(L, h);

	return 1;
}

/* }}} */

/* }}} */

/* {{{ - socket operations - */

/* {{{ lsock_accept() */

static int
lsock_accept(lua_State * L)
{
	luaL_Stream * fh;
	lsocket       new_sock;
	lsockaddr     info;

	lsocket       serv = LSOCK_CHECKSOCK(L, 1);
	socklen_t     sz   = sizeof(lsockaddr);

	BZERO(&info, sizeof(info));

	new_sock = accept(serv, (struct sockaddr *) &info, &sz);

	if (LSOCK_CREATION_FAILED(new_sock))
		return LSOCK_STRERROR(L, NULL);

	fh = newfile(L);
	fh->f = sock_to_file(L, new_sock, NULL);

	lua_pushlstring(L, (char *) &info, sz);

	return 2;
}

/* }}} */

/* {{{ lsock_listen() */

static int
lsock_listen(lua_State * L)
{
	lsocket serv = LSOCK_CHECKSOCK(L, 1);
	int  backlog = luaL_optinteger(L, 2, 0);

	/* a backlog of zero hints the implementation
	** to set the ideal connect queue size */
	if (LSOCK_OPERATION_FAILED(listen(serv, backlog)))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_bind() */

static int
lsock_bind(lua_State * L)
{
	size_t       sz   = 0;
	lsocket      serv = LSOCK_CHECKSOCK(L, 1);
	const char * addr = luaL_checklstring(L, 2, &sz);

	if (LSOCK_OPERATION_FAILED(bind(serv, (struct sockaddr *) addr, sz)))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_connect() */

static int
lsock_connect(lua_State * L)
{
	size_t       sz     = 0;
	lsocket      client = LSOCK_CHECKSOCK(L, 1);
	const char * addr   = luaL_checklstring(L, 2, &sz);

	if (LSOCK_OPERATION_FAILED(connect(client, (struct sockaddr *) addr, sz)))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_getsockname() */

static int
lsock_getsockname(lua_State * L)
{
	lsockaddr addr;

	lsocket    s = LSOCK_CHECKSOCK(L, 1);
	socklen_t sz = sizeof(addr);

	BZERO(&addr, sizeof(addr));

	if (LSOCK_OPERATION_FAILED(getsockname(s, (struct sockaddr *) &addr, &sz)))
		return LSOCK_STRERROR(L, NULL);

	lua_pushlstring(L, (char *) &addr, sz);

	return 1;
}

/* }}} */

/* {{{ lsock_getpeername() */

static int
lsock_getpeername(lua_State * L)
{
	lsockaddr addr;

	lsocket    s = LSOCK_CHECKSOCK(L, 1);
	socklen_t sz = sizeof(addr);

	BZERO(&addr, sizeof(addr));

	if (LSOCK_OPERATION_FAILED(getpeername(s, (struct sockaddr *) &addr, &sz)))
		return LSOCK_STRERROR(L, NULL);

	lua_pushlstring(L, (char *) &addr, sz);

	return 1;
}

/* }}} */

/* {{{ lsock_sendto() */

static int
lsock_sendto(lua_State * L)
{
	ssize_t sent;

	int flags;

	size_t data_len = 0;
	const char * data = NULL;

	size_t sa_len;
	const char * sa;

	lsocket s = LSOCK_CHECKSOCK(L, 1);
	strij_to_payload(L, 2, &data, &data_len);
	flags = luaL_checkint(L, 3);

	sa = luaL_optlstring(L, 4, "", &sa_len);

	sent = sendto(s, data, data_len, flags, (struct sockaddr *) &sa, sa_len);

	if (LSOCK_OPERATION_FAILED(sent))
		return LSOCK_STRERROR(L, NULL);

	lua_pushnumber(L, sent);

	return 1;
}

/* }}} */

/* {{{ lsock_send() */

static int
lsock_send(lua_State * L)
{
	int n = lua_gettop(L);

	/* the sock is already associated with a sockaddr (connect())
	** arg-shave-off anything after arg 3 to avoid confusing sendto() */
	if (n > 3)
		lua_pop(L, n - 3);

	return lsock_sendto(L);
}

/* }}} */

/* {{{ lsock_recvfrom() */

static int
lsock_recvfrom(lua_State * L)
{
	ssize_t gotten;

	const char * from     = NULL;
	socklen_t    from_len = 0;

	char * buf;
	luaL_Buffer B;

	lsocket s      = LSOCK_CHECKSOCK(L, 1);
	size_t  buflen = luaL_checkint  (L, 2);
	int     flags  = luaL_checkint  (L, 3);

	from = luaL_optlstring(L, 4, "", (size_t *) &from_len);

	buf = luaL_buffinitsize(L, &B, buflen);

	BZERO(buf, buflen); /* a must! */

	gotten = recvfrom(s, buf, buflen, flags, (struct sockaddr *) from, &from_len);

	if (LSOCK_OPERATION_FAILED(gotten))
		return LSOCK_STRERROR(L, NULL);

	luaL_pushresultsize(&B, gotten);

	return 1; /* success! */
}

/* }}} */

/* {{{ lsock_recv() */

static int
lsock_recv(lua_State * L)
{
	int n = lua_gettop(L);

	/* the sock is already associated with a sockaddr (connect())
	** arg-shave-off anything after arg 3 to avoid confusing recvfrom() */
	if (n > 3)
		lua_pop(L, n - 3);

	return lsock_recvfrom(L);
}

/* }}} */

/* {{{ lsock_shutdown() */

/* shutdown(sock, how) -> true  -or-  nil, errno */

static int
lsock_shutdown(lua_State * L)
{
	lsocket sock = LSOCK_CHECKSOCK(L, 1);
	int     how  = luaL_checkint  (L, 2);

	if (LSOCK_OPERATION_FAILED(shutdown(sock, how)))
		return LSOCK_STRERROR(L, NULL);

	lua_pushboolean(L, 1);

	return 1;
}

/* }}} */

/* {{{ lsock_socket() */

static int
lsock_socket(lua_State * L)
{
	luaL_Stream * stream;

	int domain   = luaL_checkint(L, 1),
		type     = luaL_checkint(L, 2),
		protocol = luaL_optint  (L, 3, 0);

	lsocket new_sock = socket(domain, type, protocol);

	if (LSOCK_CREATION_FAILED(new_sock))
		return LSOCK_STRERROR(L, NULL);

	stream    = newfile(L);
	stream->f = sock_to_file(L, new_sock, NULL);

	return 1;
}

/* }}} */

/* {{{ lsock_shouldblock() */

static int
lsock_shouldblock(lua_State * L)
{
	lsocket s = LSOCK_CHECKSOCK(L, 1);
	int     b = lua_isnone(L, 2) ? 1 : lua_toboolean(L, 2);

#ifdef _WIN32
	if (SOCKET_ERROR == ioctlsocket(s, FIONBIO, (u_long *) &b))
		return LSOCK_STRERROR(L, "ioctlsocket()");
#else
	int flags = fcntl(s, F_GETFL);

	if (-1 == flags)
		return LSOCK_STRERROR(L, "fcntl(F_GETFL)");

	if (b)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	/* could be redundant (O_NONBLOCK might already be set) */
	flags = fcntl(s, F_SETFL, flags);

	if (-1 == flags)
		return LSOCK_STRERROR(L, "fcntl(F_SETFL)");

	flags = b ? 1 : 0;

	flags = ioctl(s, FIONBIO, &flags);

	if (-1 == flags)
		return LSOCK_STRERROR(L, "ioctl()");
#endif

	lua_pushboolean(L, 1); /* success, not the passed/read blocking state */

	return 1;
}

/* }}} */

/* {{{ lsock_close() */

/* you can also use io.close()... */

static int
lsock_close(lua_State * L)
{
	return close_stream(L);
}

/* }}} */

/* {{{ sockopt_boolean() */

static int
sockopt_boolean(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	int value = 0;
	socklen_t sz = sizeof(value);

	if (get)
	{
		if (LSOCK_OPERATION_FAILED(getsockopt(s, level, option, (char *) &value, &sz)))
			return LSOCK_STRERROR(L, NULL);

		lua_pushboolean(L, value);
	}
	else
	{
		value = lua_isnumber(L, 4) ? lua_tointeger(L, 4) : lua_toboolean(L, 4);

		if (LSOCK_OPERATION_FAILED(setsockopt(s, level, option, (char *) &value, sz)))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

/* }}} */

/* {{{ sockopt_integer() */

static int
sockopt_integer(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	int value = 0;
	socklen_t sz = sizeof(value);

	if (get)
	{
		if (LSOCK_OPERATION_FAILED(getsockopt(s, level, option, (char *) &value, &sz)))
			return LSOCK_STRERROR(L, NULL);

		lua_pushnumber(L, value);
	}
	else
	{
		value = luaL_checkint(L, 4);

		if (LSOCK_OPERATION_FAILED(setsockopt(s, level, option, (char *) &value, sz)))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

/* }}} */

/* {{{ sockopt_linger() */

static int sockopt_linger(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	if (get)
	{
		struct linger l;
		socklen_t sz = sizeof(l);

		BZERO(&l, sz);

		if (LSOCK_OPERATION_FAILED(getsockopt(s, level, option, (char *) &l, &sz)))
			return LSOCK_STRERROR(L, NULL);

		linger_to_table(L, &l);
	}
	else
	{
		struct linger * l = NULL;
		socklen_t sz = 0;

		luaL_checktype(L, 4, LUA_TTABLE);

		l  = table_to_linger(L, 4);
		sz = lua_rawlen(L, -1);

		if (LSOCK_OPERATION_FAILED(setsockopt(s, level, option, (char *) l, sz)))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

/* }}} */

/* {{{ sockopt_ifnam() */

#ifndef _WIN32

static int sockopt_ifnam(lua_State * L)
{
	lsocket s  = LSOCK_CHECKSOCK(L, 1);
	int level  =   luaL_checkint(L, 2);
	int option =   luaL_checkint(L, 3);
	int get    =      lua_isnone(L, 4);

	if (get)
	{
		char buf[IFNAMSIZ + 1]; /* +1 for safety (this is like ~17 bytes anyway) */
		socklen_t sz = sizeof(buf);

		BZERO(buf, sizeof(buf));

		if (LSOCK_OPERATION_FAILED(getsockopt(s, level, option, buf, &sz)))
			return LSOCK_STRERROR(L, NULL);

		lua_pushlstring(L, buf, sz);
	}
	else
	{
		socklen_t sz = 0;

		const char * value = luaL_checklstring(L, 4, (size_t *) &sz);

		if (LSOCK_OPERATION_FAILED(setsockopt(s, level, option, value, sz)))
			return LSOCK_STRERROR(L, NULL);
	}

	return get;
}

#endif

/* }}} */

enum { SOCKOPT_BOOLEAN, SOCKOPT_INTEGER, SOCKOPT_LINGER, SOCKOPT_INADDR, SOCKOPT_IN6ADDR, SOCKOPT_IFNAM };

/* {{{ option_to_handler() */

static int option_to_handler(const int level, const int option)
{
	if (SOL_SOCKET == level)
	{
		switch (option)
		{
			case SO_ACCEPTCONN:
			case SO_BROADCAST:
			case SO_REUSEADDR:
			case SO_KEEPALIVE:
			case SO_OOBINLINE:
			case SO_DONTROUTE:
			case SO_DEBUG:
#ifndef _WIN32
			case SO_TIMESTAMP:
#endif
#ifdef __linux
			case SO_BSDCOMPAT:
			case SO_NO_CHECK:
			case SO_MARK:
#endif
				return SOCKOPT_BOOLEAN;

			case SO_RCVLOWAT:
			case SO_RCVTIMEO:
			case SO_SNDLOWAT:
			case SO_SNDTIMEO:
			case SO_SNDBUF:
			case SO_RCVBUF:
			case SO_ERROR:
			case SO_TYPE:
#ifdef __linux
			case SO_PRIORITY:
			case SO_PROTOCOL:
			case SO_RCVBUFFORCE:
			case SO_SNDBUFFORCE:
			case SO_PEEK_OFF:
			case SO_DOMAIN:
#endif
				return SOCKOPT_INTEGER;

			case SO_LINGER:
				return SOCKOPT_LINGER;

#ifdef __linux
			case SO_BINDTODEVICE:
				return SOCKOPT_IFNAM;
#endif
		}
	}
	else if (IPPROTO_TCP == level) /* IPPROTO_TCP == SOL_TCP */
	{
		switch (option)
		{
			case TCP_MAXSEG:
#ifndef _WIN32
			case TCP_KEEPINTVL:
			case TCP_KEEPCNT:
#endif
#ifdef __linux
			case TCP_WINDOW_CLAMP:
			case TCP_DEFER_ACCEPT: /* NOT A BOOLEAN, number is seconds to wait for data or drop */
			case TCP_LINGER2:
			case TCP_KEEPIDLE:
			case TCP_SYNCNT:
#endif
				return SOCKOPT_INTEGER;

			case TCP_NODELAY:
#ifdef __linux
			case TCP_CORK:
#endif
				return SOCKOPT_BOOLEAN;

			/* maybe handle TCP_INFO at some point */
		}
	}
	else if (IPPROTO_UDP == level) /* IPPROTO_UDP == SOL_UDP */
	{
		switch (option)
		{
#ifdef _WIN32
			case UDP_CHECKSUM_COVERAGE:
			case UDP_NOCHECKSUM:
#endif
#ifdef __linux
			case UDP_CORK:
#endif
				return SOCKOPT_BOOLEAN;
		}
	}
	else if (IPPROTO_IP == level) /* IPPROTO_IP == SOL_IP */
	{
		switch (option)
		{
			case IP_TTL:
			case IP_TOS:
			case IP_MULTICAST_TTL:
#ifndef _WIN32
			case IP_RECVTTL:
#endif
#ifdef __linux
			case IP_MTU_DISCOVER:
			case IP_RECVTOS:
#endif
				return SOCKOPT_INTEGER;

			case IP_HDRINCL:
			case IP_MULTICAST_LOOP:
#ifndef _WIN32
			case IP_RECVOPTS:
			case IP_RETOPTS:
#endif
#ifdef __linux
			case IP_PKTINFO:
			case IP_RECVERR:
			case IP_ROUTER_ALERT:
#endif
				return SOCKOPT_BOOLEAN;

			case IP_MULTICAST_IF:
				return SOCKOPT_INADDR;
		}
	}
	else if (IPPROTO_IPV6 == level) /* IPPROTO_IPV6 == SOL_IPV6 */
	{
		switch (option)
		{

			case IPV6_MULTICAST_LOOP:
#ifndef _WIN32
#endif
#ifdef __linux
			case IPV6_ROUTER_ALERT:
			case IPV6_NEXTHOP:
			case IPV6_HOPLIMIT:
			case IPV6_DSTOPTS:
			case IPV6_HOPOPTS:
#endif
				return SOCKOPT_BOOLEAN;

			case IPV6_CHECKSUM:
			case IPV6_MULTICAST_HOPS:
			case IPV6_UNICAST_HOPS:
#ifdef __linux
			case IPV6_AUTHHDR:
			case IPV6_ADDRFORM:
			case IPV6_PKTINFO:
#endif
				return SOCKOPT_INTEGER;
		}
	}

	/* unknown level+option combo */
	return -1;
}

/* }}} */

/* {{{ sockopt() */

static int
sockopt(lua_State * L)
{
	int level, option, get;

	LSOCK_CHECKSOCK(L, 1);

	level  = luaL_checkint(L, 2);
	option = luaL_checkint(L, 3);
	get    =    lua_isnone(L, 4);

	switch (option_to_handler(level, option))
	{
		case SOCKOPT_BOOLEAN: return sockopt_boolean(L);
		case SOCKOPT_INTEGER: return sockopt_integer(L);
		case SOCKOPT_LINGER:  return sockopt_linger(L);
#ifndef _WIN32
		case SOCKOPT_IFNAM:   return sockopt_ifnam(L);
#endif
	}

	/* unkown level+option */
	lua_pushnil(L);
	lua_pushfstring
	(
		L,
		"unknown level or option passed to %ssockopt(); you can force it with %ssockopt_boolean/integer()..",
		get ? "get" : "set",
		get ? "get" : "set"
	);

	return 2;
}

/* }}} */

/* {{{ lsock_getsockopt() */

static int
lsock_getsockopt(lua_State * L)
{
	int n = lua_gettop(L);

	if (n > 3)
		lua_pop(L, n - 3);

	return sockopt(L);
}

/* }}} */

/* {{{ lsock_setsockopt() */

static int
lsock_setsockopt(lua_State * L)
{
	luaL_checkany(L, 4);

	return sockopt(L);
}

/* }}} */

/* {{{ lsock_getaddrinfo() */

static int
lsock_getaddrinfo(lua_State * L)
{
	int ret;
	int i = 1;

	struct addrinfo hints, * info, * p;

	/* node and service both cannot be NULL, getaddrinfo() will spout EAI_NONAME */
	const char * nname = lua_isnil(L, 1) ? NULL : luaL_checkstring(L, 1);
	const char * sname = lua_isnil(L, 2) ? NULL : luaL_checkstring(L, 2);

	BZERO(&hints, sizeof(struct addrinfo));

	if (!lua_isnone(L, 3))
	{
		luaL_checktype(L, 3, LUA_TTABLE); /* getaddrinfo('www.google.com', 'http', { ... }) */

		lua_getfield(L, 3, "ai_flags");

		if (!lua_isnil(L, -1))
			hints.ai_flags = lua_tointeger(L, -1);

		lua_pop(L, 1);

		lua_getfield(L, 3, "ai_family");

		if (!lua_isnil(L, -1))
			hints.ai_family = lua_tointeger(L, -1);

		lua_pop(L, 1);

		lua_getfield(L, 3, "ai_socktype");

		if (!lua_isnil(L, -1))
			hints.ai_socktype = lua_tointeger(L, -1);

		lua_pop(L, 1);

		lua_getfield(L, 3, "ai_protocol");

		if (!lua_isnil(L, -1))
			hints.ai_protocol = lua_tointeger(L, -1);

		lua_pop(L, 1);
	}

	info = NULL;

	ret = getaddrinfo(nname, sname, &hints, &info);

	if (0 != ret)
		return LSOCK_GAIERROR(L, ret);

	ret = 1; /* to reflect how many returns */

	lua_newtable(L);

	for (p = info; p != NULL; p = (i++, p->ai_next))
	{
		/* the sequence index in the outer table */
		lua_pushnumber(L, i);

		/* allocate for 7-at-most members */
		lua_createtable(L, 0, 7);

		PUSHFIELD_NUM(L, -1, "ai_flags",    p->ai_flags   );
		PUSHFIELD_NUM(L, -1, "ai_family",   p->ai_family  );
		PUSHFIELD_NUM(L, -1, "ai_socktype", p->ai_socktype);
		PUSHFIELD_NUM(L, -1, "ai_protocol", p->ai_protocol);
		PUSHFIELD_NUM(L, -1, "ai_addrlen",  p->ai_addrlen );

		PUSHFIELD_NSTR(L, -1, "ai_addr", (char *) p->ai_addr, p->ai_addrlen);

		if (NULL != p->ai_canonname)
			PUSHFIELD_PSTR(L, -1, "ai_canonname", p->ai_canonname);

		lua_settable(L, -3);
	}

	if (NULL != info->ai_canonname)
	{
		lua_pushstring(L, info->ai_canonname);
		ret++;
	}

	freeaddrinfo(info);

	return ret;
}

/* }}} */

/* {{{ lsock_getnameinfo() */

static int
lsock_getnameinfo(lua_State * L)
{
	int stat;

	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	size_t       sz    = 0;
	const char * sa    = luaL_checklstring(L, 1, &sz);
	int          flags = luaL_checkint(L, 2);

	BZERO(hbuf, NI_MAXHOST);
	BZERO(sbuf, NI_MAXSERV);

	stat = getnameinfo((struct sockaddr *) sa, sz, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), flags);

	if (0 != stat)
		return LSOCK_GAIERROR(L, stat);

	lua_pushstring(L, hbuf);
	lua_pushstring(L, sbuf);

	return 2;
}

/* }}} */

/* {{{ lsock_select() */

enum { R, W, E };

static int
lsock_select(lua_State * L)
{
	int x, y, stat;

	fd_set set[3];
	int highsock = 0;
	struct timeval * t = NULL;

	luaL_checktype(L, 1, LUA_TTABLE); /*  readfds */
	luaL_checktype(L, 2, LUA_TTABLE); /* writefds */
	luaL_checktype(L, 3, LUA_TTABLE); /* errorfds */

	if (!lua_isnoneornil(L, 4))
	{
		luaL_checktype(L, 4, LUA_TTABLE);
		t = table_to_timeval(L, 4);
	}

	BZERO(&set, sizeof(set));

	/* parse tables into fd_sets */
	for (x = 1; x <= 3; x++)
	{
		int how_many = luaL_len(L, x);

		for (y = 1; y <= how_many; y++)
		{
			int fd = -1;

			lua_pushnumber(L, y);
			lua_gettable(L, x);

			fd = LSOCK_CHECKFD(L, -1);

			highsock = LSOCK_MAX(highsock, fd);

			FD_SET(fd, &set[x - 1]);
		}
	}

	stat = select(highsock + 1, &set[R], &set[W], &set[E], t);

	if (-1 == stat)
		LSOCK_STRERROR(L, NULL);

	/* parse fd_sets back into tables */
	for (x = 1; x <= 3; x++)
	{
		int how_many = luaL_len(L, x);

		/* reuse stat for how many array elems we might have */
		lua_createtable(L, stat, 0);

		for (y = 1; y <= how_many; y++)
		{
			int fd;

			/* push the file handle (socket) userdata */
			lua_pushnumber(L, y);
			lua_gettable(L, x);

			fd = LSOCK_CHECKFD(L, -1);

			if (FD_ISSET(fd, &set[y - 1]))
			{
				lua_pushnumber(L, y); /* the numeric index */
				lua_pushvalue(L, -2); /* the file handle */
				lua_settable(L, -4);  /* the new outgoing table */
			}

			/* remove the file handle userdata */
			lua_pop(L, 1);
		}
	}

	/* returns the read, write, and exception tables */
	return 3;
}

/* {{{ lsock_bytes_available() */

/* note: can be used to get the bytes available
** or as a boolean "ready for reading" check */
static int
lsock_bytes_available(lua_State * L)
{
	lsocket s = LSOCK_CHECKSOCK(L, 1);

	size_t available = 0;

#ifdef _WIN32
	if (LSOCK_OPERATION_FAILED(ioctlsocket(s, FIONREAD, (u_long *) &available)))
		return LSOCK_STRERROR(L, "ioctlsocket()");
#else
	if (LSOCK_OPERATION_FAILED(ioctl(s, FIONREAD, &available)))
		return LSOCK_STRERROR(L, "ioctl()");
#endif

	lua_pushnumber(L, available);
	return 1;
}

/* }}} */

/* }}} */

/* {{{ lsock_pipe() */

static int
lsock_pipe(lua_State * L)
{
	lsocket pair[2] = { -1, -1 };

	luaL_Stream * r = NULL;
	luaL_Stream * w = NULL;

#ifdef _WIN32
	if (0 == CreatePipe((PHANDLE) &pair[0], (PHANDLE) &pair[1], NULL, (DWORD) luaL_optnumber(L, 1, 0)))
		return lsock_error(L, GetLastError(), (char * (*)(int)) &strerror, "CreatePipe()");
#else
	if (LSOCK_OPERATION_FAILED(pipe(pair)))
		return LSOCK_STRERROR(L, NULL);
#endif

	r = newfile(L);
	r->f = fd_to_file(L, pair[0], "rb");

	w = newfile(L);
	w->f = fd_to_file(L, pair[1], "wb");

	return 2;
}

/* }}} */

#ifndef _WIN32
/* look into CreatePipe() */
/* {{{ lsock_socketpair() */

static int
lsock_socketpair(lua_State * L)
{
	int pair[2] = { -1, -1 };

	luaL_Stream * one = NULL;
	luaL_Stream * two = NULL;

	int domain   = luaL_checknumber(L, 1),
		type     = luaL_checknumber(L, 2),
		protocol = luaL_checknumber(L, 3);

	if (LSOCK_OPERATION_FAILED(socketpair(domain, type, protocol, pair)))
		return LSOCK_STRERROR(L, NULL);

	one = newfile(L);
	one->f = fd_to_file(L, pair[0], NULL);

	two = newfile(L);
	two->f = fd_to_file(L, pair[1], NULL);

	return 2;
}

/* }}} */

/* {{{ lsock_sendfile() */

/* look at using TransmitFile() on Windows */

static int
lsock_sendfile(lua_State * L)
{
	ssize_t sent;

	int    out    =    LSOCK_CHECKFD(L, 1);
	int    in     =    LSOCK_CHECKFD(L, 2);
	off_t  offset =   luaL_optnumber(L, 3, 0);
#ifdef __linux
	size_t count  = luaL_checknumber(L, 4);
#endif
#ifdef __APPLE__
	off_t count = luaL_checknumber(L, 4);
#endif

#ifdef __linux
	sent = sendfile(out, in, lua_isnil(L, 3) ? NULL : &offset, count);
#endif
#ifdef __APPLE__
	/* Differences:
	** 1) out<->in reversed from Linux sendfile()
	** 2) NULL for the header/trailer structure (Mac OS X sendfile rocks!)
	** 3) 0 for flags (future expansion); otherwise EINVAL will be returned */
	sent = sendfile(in, out, offset, &count, NULL, 0);
#endif

	if (-1 == sent)
		return LSOCK_STRERROR(L, NULL);

#ifdef __APPLE__
	sent = count;
#endif

	lua_pushnumber(L, sent);

	return 1;
}

/* }}} */
#endif

/* {{{ lsock_getfd() */

/* for luasocket-dependant stuff */

static int
lsock_getfd(lua_State * L)
{
	lua_pushnumber(L, LSOCK_CHECKFD(L, 1));

	return 1;
}

/* }}} */

#ifdef _WIN32

/* {{{ lsock_cleanup() */

static int
lsock_cleanup(lua_State * L)
{
	((void) L);

	WSACleanup();

	return 0;
}

/* }}} */

/* {{{ lsock_startup() */

static void
lsock_startup(lua_State * L)
{
	WSADATA wsaData;

	int stat = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (stat != 0)
	{
		lsock_cleanup(L);
		luaL_error(L, "WSAStartup() failed [%d]: %s\n", stat, strerror(stat));
	}

	if (2 != LOBYTE(wsaData.wVersion) || 2 != HIBYTE(wsaData.wVersion))
	{
		lsock_cleanup(L);
		luaL_error(L, "Could not find a usable version of Winsock.dll");
	}
}

/* }}} */


#endif

/* {{{ luaopen_lsock() */

#define LUA_REG(x) { #x, lsock_##x }

static luaL_Reg lsocklib[] =
{
	/* the portable API */
	LUA_REG(accept),
	LUA_REG(bind),
	LUA_REG(bytes_available),
	LUA_REG(shouldblock),
	LUA_REG(close),
	LUA_REG(connect),
	LUA_REG(gai_strerror),
	LUA_REG(getaddrinfo),
	LUA_REG(getfd),
	LUA_REG(getpeername),
	LUA_REG(getnameinfo),
	LUA_REG(getsockname),
	LUA_REG(getsockopt),
	LUA_REG(htons),
	LUA_REG(ntohs),
	LUA_REG(htonl),
	LUA_REG(ntohl),
	LUA_REG(listen),
	LUA_REG(pack_sockaddr),
	LUA_REG(pipe),
	LUA_REG(recv),
	LUA_REG(recvfrom),
	LUA_REG(select),
	LUA_REG(send),
	LUA_REG(sendto),
	LUA_REG(setsockopt),
	LUA_REG(shutdown),
	LUA_REG(socket),
	LUA_REG(strerror),
	LUA_REG(unpack_sockaddr),

	/* Linux + Mac-specific API */
#ifndef _WIN32
	LUA_REG(sendfile),
	LUA_REG(socketpair),
#endif
	{ NULL, NULL }
};

#undef LUA_REG

LSOCK_EXPORT int
luaopen_lsock(lua_State * L)
{
#ifdef _WIN32
	lsock_startup(L);
#endif

	luaL_newlib(L, lsocklib);

#define PUSH_CONST(C) \
    lua_pushinteger(L, C);  \
    lua_setfield(L, -2, #C)

	lua_newtable(L);

	/* portable constants (alphabetical) */
	PUSH_CONST(AF_APPLETALK);
	PUSH_CONST(AF_INET);
	PUSH_CONST(AF_INET6);
	PUSH_CONST(AF_IPX);
	PUSH_CONST(AF_UNSPEC);
	PUSH_CONST(AI_ADDRCONFIG);
	PUSH_CONST(AI_ALL);
	PUSH_CONST(AI_CANONNAME);
	PUSH_CONST(AI_NUMERICHOST);
	PUSH_CONST(AI_NUMERICSERV);
	PUSH_CONST(AI_PASSIVE);
	PUSH_CONST(AI_V4MAPPED);
	PUSH_CONST(EACCES);
	PUSH_CONST(EADDRINUSE);
	PUSH_CONST(EADDRNOTAVAIL);
	PUSH_CONST(EAFNOSUPPORT);
	PUSH_CONST(EAGAIN);
	PUSH_CONST(EAI_AGAIN);
	PUSH_CONST(EAI_BADFLAGS);
	PUSH_CONST(EAI_FAIL);
	PUSH_CONST(EAI_FAMILY);
	PUSH_CONST(EAI_MEMORY);
	PUSH_CONST(EAI_NODATA);
	PUSH_CONST(EAI_NONAME);
	PUSH_CONST(EAI_SERVICE);
	PUSH_CONST(EAI_SOCKTYPE);
	PUSH_CONST(EBADF);
	PUSH_CONST(ECONNABORTED);
	PUSH_CONST(EDESTADDRREQ);
	PUSH_CONST(EINTR);
	PUSH_CONST(EINVAL);
	PUSH_CONST(EIO);
	PUSH_CONST(EISDIR);
	PUSH_CONST(ELOOP);
	PUSH_CONST(EMFILE);
	PUSH_CONST(ENAMETOOLONG);
	PUSH_CONST(ENFILE);
	PUSH_CONST(ENOBUFS);
	PUSH_CONST(ENOENT);
	PUSH_CONST(ENOMEM);
	PUSH_CONST(ENOTCONN);
	PUSH_CONST(ENOTDIR);
	PUSH_CONST(ENOTSOCK);
	PUSH_CONST(EOPNOTSUPP);
	PUSH_CONST(EPROTO);
	PUSH_CONST(EPROTONOSUPPORT);
	PUSH_CONST(EPROTOTYPE);
	PUSH_CONST(EROFS);
	PUSH_CONST(EWOULDBLOCK);
	PUSH_CONST(IPPROTO_AH);
	PUSH_CONST(IPPROTO_EGP);
	PUSH_CONST(IPPROTO_ESP);
	PUSH_CONST(IPPROTO_FRAGMENT);
	PUSH_CONST(IPPROTO_ICMP);
	PUSH_CONST(IPPROTO_ICMPV6);
	PUSH_CONST(IPPROTO_IDP);
	PUSH_CONST(IPPROTO_IGMP);
	PUSH_CONST(IPPROTO_IP);
	PUSH_CONST(IPPROTO_IPV6);
	PUSH_CONST(IPPROTO_MAX);
	PUSH_CONST(IPPROTO_NONE);
	PUSH_CONST(IPPROTO_PIM);
	PUSH_CONST(IPPROTO_PUP);
	PUSH_CONST(IPPROTO_RAW);
	PUSH_CONST(IPPROTO_ROUTING);
	PUSH_CONST(IPPROTO_SCTP);
	PUSH_CONST(IPPROTO_TCP);
	PUSH_CONST(IPPROTO_UDP);
	PUSH_CONST(IPV6_CHECKSUM);
	PUSH_CONST(IPV6_HOPLIMIT);
	PUSH_CONST(IPV6_JOIN_GROUP);
	PUSH_CONST(IPV6_LEAVE_GROUP);
	PUSH_CONST(IPV6_MULTICAST_HOPS);
	PUSH_CONST(IPV6_MULTICAST_IF);
	PUSH_CONST(IPV6_MULTICAST_LOOP);
	PUSH_CONST(IPV6_PKTINFO);
	PUSH_CONST(IPV6_UNICAST_HOPS);
	PUSH_CONST(IPV6_V6ONLY);
	PUSH_CONST(IP_ADD_MEMBERSHIP);
	PUSH_CONST(IP_ADD_SOURCE_MEMBERSHIP);
	PUSH_CONST(IP_BLOCK_SOURCE);
	PUSH_CONST(IP_DROP_MEMBERSHIP);
	PUSH_CONST(IP_DROP_SOURCE_MEMBERSHIP);
	PUSH_CONST(IP_HDRINCL);
	PUSH_CONST(IP_MULTICAST_IF);
	PUSH_CONST(IP_MULTICAST_LOOP);
	PUSH_CONST(IP_MULTICAST_TTL);
	PUSH_CONST(IP_OPTIONS);
	PUSH_CONST(IP_PKTINFO);
	PUSH_CONST(IP_TOS);
	PUSH_CONST(IP_TTL);
	PUSH_CONST(IP_UNBLOCK_SOURCE);
	PUSH_CONST(MSG_CTRUNC);
	PUSH_CONST(MSG_DONTROUTE);
	PUSH_CONST(MSG_OOB);
	PUSH_CONST(MSG_PEEK);
	PUSH_CONST(MSG_TRUNC);
	PUSH_CONST(MSG_WAITALL);
	PUSH_CONST(NI_DGRAM);
	PUSH_CONST(NI_NAMEREQD);
	PUSH_CONST(NI_NOFQDN);
	PUSH_CONST(NI_NUMERICHOST);
	PUSH_CONST(NI_NUMERICSERV);
	PUSH_CONST(PF_APPLETALK);
	PUSH_CONST(PF_INET);
	PUSH_CONST(PF_INET6);
	PUSH_CONST(PF_IPX);
	PUSH_CONST(PF_UNSPEC);
	PUSH_CONST(SOCK_DGRAM);
	PUSH_CONST(SOCK_RAW);
	PUSH_CONST(SOCK_RDM);
	PUSH_CONST(SOCK_SEQPACKET);
	PUSH_CONST(SOCK_STREAM);
	PUSH_CONST(SOL_SOCKET);
	PUSH_CONST(SOMAXCONN);
	PUSH_CONST(SO_ACCEPTCONN);
	PUSH_CONST(SO_BROADCAST);
	PUSH_CONST(SO_DEBUG);
	PUSH_CONST(SO_DONTROUTE);
	PUSH_CONST(SO_ERROR);
	PUSH_CONST(SO_KEEPALIVE);
	PUSH_CONST(SO_LINGER);
	PUSH_CONST(SO_OOBINLINE);
	PUSH_CONST(SO_RCVBUF);
	PUSH_CONST(SO_RCVLOWAT);
	PUSH_CONST(SO_RCVTIMEO);
	PUSH_CONST(SO_REUSEADDR);
	PUSH_CONST(SO_SNDBUF);
	PUSH_CONST(SO_SNDLOWAT);
	PUSH_CONST(SO_SNDTIMEO);
	PUSH_CONST(SO_TYPE);
	PUSH_CONST(TCP_MAXSEG);
	PUSH_CONST(TCP_NODELAY);

/* Windows-only constants */
#ifdef _WIN32
	PUSH_CONST(SD_BOTH);
	PUSH_CONST(SD_RECEIVE);
	PUSH_CONST(SD_SEND);
	PUSH_CONST(UDP_CHECKSUM_COVERAGE);
	PUSH_CONST(UDP_NOCHECKSUM);
#endif

/* Linux-only constants */
#ifdef __linux
	PUSH_CONST(AF_IRDA);
	PUSH_CONST(AI_CANONIDN);
	PUSH_CONST(AI_IDN);
	PUSH_CONST(AI_IDN_ALLOW_UNASSIGNED);
	PUSH_CONST(AI_IDN_USE_STD3_ASCII_RULES);
	PUSH_CONST(IPPROTO_COMP);
	PUSH_CONST(IPPROTO_DCCP);
	PUSH_CONST(IPPROTO_DSTOPTS);
	PUSH_CONST(IPPROTO_HOPOPTS);
	PUSH_CONST(IPPROTO_UDPLITE);
	PUSH_CONST(IPV6_ADDRFORM);
	PUSH_CONST(IPV6_AUTHHDR);
	PUSH_CONST(IPV6_DSTOPTS);
	PUSH_CONST(IPV6_HOPOPTS);
	PUSH_CONST(IPV6_NEXTHOP);
	PUSH_CONST(IPV6_ROUTER_ALERT);
	PUSH_CONST(IP_MTU_DISCOVER);
	PUSH_CONST(IP_RECVERR);
	PUSH_CONST(IP_RECVTOS);
	PUSH_CONST(IP_ROUTER_ALERT);
	PUSH_CONST(MSG_CONFIRM);
	PUSH_CONST(MSG_ERRQUEUE);
	PUSH_CONST(MSG_FASTOPEN);
	PUSH_CONST(MSG_FIN);
	PUSH_CONST(MSG_MORE);
	PUSH_CONST(MSG_NOSIGNAL);
	PUSH_CONST(MSG_PROXY);
	PUSH_CONST(MSG_RST);
	PUSH_CONST(MSG_SYN);
	PUSH_CONST(MSG_TRYHARD);
	PUSH_CONST(MSG_WAITFORONE);
	PUSH_CONST(NI_IDN);
	PUSH_CONST(NI_IDN_ALLOW_UNASSIGNED);
	PUSH_CONST(NI_IDN_USE_STD3_ASCII_RULES);
	PUSH_CONST(PF_IRDA);
	PUSH_CONST(SOL_IP);
	PUSH_CONST(SOL_IPV6);
	PUSH_CONST(SOL_TCP);
	PUSH_CONST(SOL_UDP);
	PUSH_CONST(SO_BINDTODEVICE);
	PUSH_CONST(SO_BSDCOMPAT);
	PUSH_CONST(SO_DOMAIN);
	PUSH_CONST(SO_MARK);
	PUSH_CONST(SO_NO_CHECK);
	PUSH_CONST(SO_PEEK_OFF);
	PUSH_CONST(SO_PRIORITY);
	PUSH_CONST(SO_PROTOCOL);
	PUSH_CONST(SO_RCVBUFFORCE);
	PUSH_CONST(SO_SNDBUFFORCE);
	PUSH_CONST(TCP_CORK);
	PUSH_CONST(TCP_DEFER_ACCEPT);
	PUSH_CONST(TCP_KEEPIDLE);
	PUSH_CONST(TCP_LINGER2);
	PUSH_CONST(TCP_SYNCNT);
	PUSH_CONST(TCP_WINDOW_CLAMP);
	PUSH_CONST(UDP_CORK);
#endif

/* Linux + Mac shared constants */
#ifndef _WIN32
	PUSH_CONST(EAI_ADDRFAMILY);
	PUSH_CONST(EAI_OVERFLOW);
	PUSH_CONST(EAI_SYSTEM);
	PUSH_CONST(IPPROTO_ENCAP);
	PUSH_CONST(IPPROTO_GRE);
	PUSH_CONST(IPPROTO_IPIP);
	PUSH_CONST(IPPROTO_MTP);
	PUSH_CONST(IPPROTO_RSVP);
	PUSH_CONST(IPPROTO_TP);
	PUSH_CONST(IP_RECVOPTS);
	PUSH_CONST(IP_RETOPTS);
	PUSH_CONST(MSG_DONTWAIT);
	PUSH_CONST(MSG_EOR);
	PUSH_CONST(PF_LOCAL);
	PUSH_CONST(SHUT_RD);
	PUSH_CONST(SHUT_RDWR);
	PUSH_CONST(SHUT_WR);
	PUSH_CONST(SO_TIMESTAMP);
	PUSH_CONST(TCP_KEEPCNT);
	PUSH_CONST(TCP_KEEPINTVL);
#endif

#undef PUSH_CONST

	PUSHFIELD_LSTR(L, -1, "INADDR_ANY",             "0.0.0.0"        );
	PUSHFIELD_LSTR(L, -1, "INADDR_BROADCAST",       "255.255.255.255");
	PUSHFIELD_LSTR(L, -1, "INADDR_NONE",            "255.255.255.255");
	PUSHFIELD_LSTR(L, -1, "INADDR_LOOPBACK",        "127.0.0.1"      );
	PUSHFIELD_LSTR(L, -1, "INADDR_UNSPEC_GROUP",    "224.0.0.0"      );
	PUSHFIELD_LSTR(L, -1, "INADDR_ALLHOSTS_GROUP",  "224.0.0.1"      );
	PUSHFIELD_LSTR(L, -1, "INADDR_ALLRTRS_GROUP",   "224.0.0.2"      );
	PUSHFIELD_LSTR(L, -1, "INADDR_MAX_LOCAL_GROUP", "224.0.0.255"    );

	PUSHFIELD_LSTR(L, -1, "in6addr_any",      "::" );
	PUSHFIELD_LSTR(L, -1, "in6addr_loopback", "::1");

	/* the _INIT's are just tables to pass to pack_sockaddr */
	lua_createtable(L, 0, 2);
	PUSHFIELD_INT(L, -1, "sin6_family", AF_INET6);
	PUSHFIELD_LSTR(L, -1, "sin6_addr", "::");
	lua_setfield(L, -2, "IN6ADDR_ANY_INIT");

	lua_createtable(L, 0, 2);
	PUSHFIELD_INT(L, -1, "sin6_family", AF_INET6);
	PUSHFIELD_LSTR(L, -1, "sin6_addr", "::1");
	lua_setfield(L, -2, "IN6ADDR_LOOPBACK_INIT");

	PUSHFIELD_INT(L, -1, "INET_ADDRSTRLEN",  16);
	PUSHFIELD_INT(L, -1, "INET6_ADDRSTRLEN", 46);

	/* table of constants is reachable under 2 names */
	lua_pushvalue(L, -1);
	lua_setfield(L, -3, "C");
	lua_setfield(L, -2, "constants");

#ifdef _WIN32
	lua_pushcfunction(L, &lsock_cleanup);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2); /* it is its own metatable */
#endif

	return 1;
}

/* }}} */
