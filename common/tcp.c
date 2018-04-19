#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"


int
tcp_connect(const char *hname, const char *service)
{
	struct addrinfo hints, *hlist, *rp;
	int ret, fd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	ret = getaddrinfo(hname, service, &hints, &hlist);
	if (0 != ret) {
		set_system_error(gai_strerror(ret));
		return -1;
	}

	for (rp = hlist; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (-1 == fd)
			continue;

		if (-1 != connect(fd, rp->ai_addr, rp->ai_addrlen))
			break;

		close(fd);
	}
	freeaddrinfo(hlist);

	if (NULL == rp) {
		set_error(ERR_CONNECT, "tcp_connect");
		return -1;
	}
	return fd;
}

