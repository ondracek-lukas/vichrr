// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3
// most of this file was taken from `man getaddrinfo`

#ifdef __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum packetType {
	PACKET_HELO,
	PACKET_DATA,
	PACKET_STATUS,
	PACKET_KEY_PRESS
};

struct packetClientHelo {
	char type;
	uint16_t version;
	float aioLatency;
	float dBAdj;
	char name[100];
};
struct packetServerHelo {
	char type;
	uint8_t clientID;
	bindex_t initBlockIndex;
	char str[SHELO_STR_LEN]; // "keys\nhelp"
};
struct packetClientData {
	char type;
	uint8_t clientID;
	bindex_t playBlockIndex; // server index to be played on the client side
	bindex_t blockIndex;
	sample_t block[MONO_BLOCK_SIZE];
};
struct packetServerData {
	char type;
	bindex_t blockIndex;
	sample_t block[STEREO_BLOCK_SIZE];
};
struct packetStatusStr {
	char type;
	uint8_t packetIndex;
	uint8_t packetsCnt;
	bindex_t statusIndex;
	char str[STATUS_LINES_PER_PACKET * (STATUS_WIDTH + 1)];
		// (size of whole IP packet might be limited to 576 B;  IP header is 20--60 B, UDP header is 8 B)
};
struct packetKeyPress {
	char type;
	uint8_t clientID;
	bindex_t playBlockIndex;
	int key;
};

union packet {
	struct packetClientHelo cHelo;
	struct packetServerHelo sHelo;
	struct packetClientData cData;
	struct packetServerData sData;
	struct packetStatusStr  sStat;
	struct packetKeyPress   cKeyP;
};


bool netAddrsEqual(struct sockaddr_storage *addr1, struct sockaddr_storage *addr2) {
	return memcmp(addr1, addr2, sizeof(struct sockaddr_storage)) == 0;
}

int netInit() {
#ifdef __WIN32__
	{
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
			return -1;
		}
	}
#endif
}
int netCleanup() {
#ifdef __WIN32__
	WSACleanup();
#endif
}

int netOpenPort(char *port) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
	hints.ai_protocol = 0;          /* Any protocol */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	s = getaddrinfo(NULL, port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return -1;
	}

	 /* getaddrinfo() returns a list of address structures.
			Try each address until we successfully bind(2).
			If socket(2) (or bind(2)) fails, we (close the socket
			and) try the next address. */

	 for (rp = result; rp != NULL; rp = rp->ai_next) {
			 sfd = socket(rp->ai_family, rp->ai_socktype,
							 rp->ai_protocol);
			 if (sfd == -1)
					 continue;

			 if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
					 break;                  /* Success */

			 close(sfd);
	 }

	 freeaddrinfo(result);           /* No longer needed */

	 if (rp == NULL) {               /* No address succeeded */
			 fprintf(stderr, "Could not bind\n");
			 return -1;
	 }

	 return sfd;
}

int netOpenConn(char *addr, char *port) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s, j;

	/* Obtain address(es) matching host/port */

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          /* Any protocol */

	s = getaddrinfo(addr, port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return -1;
	}

	/* getaddrinfo() returns a list of address structures.
	Try each address until we successfully connect(2).
	If socket(2) (or connect(2)) fails, we (close the socket
	and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;                  /* Success */

		close(sfd);
	}
	freeaddrinfo(result);           /* No longer needed */

	if (rp == NULL) {               /* No address succeeded */
		fprintf(stderr, "Could not connect\n");
		return -1;
	}

#ifdef __WIN32__
	DWORD tv = 1000;
#else
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
#endif
	if (setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, (void *) &tv,sizeof(tv)) < 0) {
			perror("Error");
	}

	return sfd;

}
