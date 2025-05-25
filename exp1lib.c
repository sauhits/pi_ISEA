#include "exp1.h"
#include "exp1lib.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int exp1_tcp_listen(int port)
{
	int sock;
	struct sockaddr_in addr;
	int yes = 1;
	int ret;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("socket");
		exit(1);
	}

	// Test Code: check socket buffer size
	//	socklen_t slen;
	//	int sockBufSize;
	//	getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &sockBufSize, &slen);
	//	printf("Socket Buffer Size(default): %d\n", sockBufSize);

	bzero((char *)&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0)
	{
		perror("bind");
		exit(1);
	}

	ret = listen(sock, 5);
	if (ret < 0)
	{
		perror("reader: listen");
		close(sock);
		exit(-1);
	}

	return sock;
}

int exp1_tcp_connect(const char *hostname, int port)
{
	int sock;
	int ret;
	struct sockaddr_in addr;
	struct hostent *host;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	addr.sin_family = AF_INET;
	host = gethostbyname(hostname);
	addr.sin_addr = *(struct in_addr *)(host->h_addr_list[0]);
	addr.sin_port = htons(port);

	ret = connect(sock, (struct sockaddr *)&addr, sizeof addr);
	if (ret < 0)
	{
		return -1;
	}
	else
	{
		return sock;
	}
}

int exp1_udp_listen(int port)
{
	int sock;
	struct sockaddr_in addr;
	int yes = 1;
	int ret;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
	{
		perror("socket");
		exit(1);
	}

	bzero((char *)&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0)
	{
		perror("bind");
		exit(1);
	}
	return sock;
}

int exp1_udp_connect(const char *hostname, int port)
{
	int sock;
	int ret;
	struct sockaddr_in addr;
	struct hostent *host;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	addr.sin_family = AF_INET;
	host = gethostbyname(hostname);
	addr.sin_addr = *(struct in_addr *)(host->h_addr_list[0]);
	addr.sin_port = htons(port);

	//	return sock;

	ret = connect(sock, (struct sockaddr *)&addr, sizeof addr);
	if (ret < 0)
	{
		return -1;
	}
	else
	{
		return sock;
	}
}

double gettimeofday_sec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}

int exp1_do_talk(int sock)
{
	fd_set fds;
	char buf[1024];

	FD_ZERO(&fds);
	FD_SET(0, &fds);
	FD_SET(sock, &fds);

	select(sock + 1, &fds, NULL, NULL, NULL);

	if (FD_ISSET(0, &fds) != 0)
	{
		char *res = fgets(buf, 1024, stdin);
		if (res == NULL)
		{
			perror("error in fgets: ");
		}
		int size = write(sock, buf, strlen(buf));
		if (size == -1)
		{
			perror("error in write: ");
		}
	}

	if (FD_ISSET(sock, &fds) != 0)
	{
		int ret = recv(sock, buf, 1024, 0);
		if (ret > 0)
		{
			int size = write(1, buf, ret);
			if (size == -1)
			{
				perror("error in write: ");
			}
		}
		else
		{
			return -1;
		}
	}

	return 1;
}
