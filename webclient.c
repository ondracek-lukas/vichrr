// Virtual Choir Rehearsal Room  Copyright (C) 2021  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#ifdef __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <openssl/x509v3.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/rand.h>

#define PORT        8080
#define MAX_URL_LEN 1024


// websocket_parser callbacks
static void websocket_on_unwrapped(void *udata, void *msg, uint64_t len,
                                   char first, char last, char text,
                                   unsigned char rsv) {
	printf("\n-- frame received (%s%s%s) --\n", text ? "text" : "binary", first ? ", msg begin" : "", last ? ", msg end" : "");
	fwrite(msg, 1, len, stdout);
	printf("\n--\n\n");

}
static void websocket_on_protocol_ping(void *udata, void *msg, uint64_t len) {
	printf("ping received\n");
}
static void websocket_on_protocol_pong(void *udata, void *msg, uint64_t len) {
	printf("pong received\n");
}

static void websocket_on_protocol_close(void *udata) {
	printf("close received\n");
}
static void websocket_on_protocol_error(void *udata) {
	printf("error received\n");
}
#include "websocket_parser.h"

void base64encode(char *raw, int len, char *out) { // raw needs zero padding to multiple of 3 bytes, out needs 4/3 size of raw +1
	const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (; len > 0; raw += 3, len -= 3) {
		uint32_t chunk = raw[0] << 16 | raw[1] << 8 | raw[2];
		for (int i = 18; i >= 0; i -= 6) *out++ = tbl[chunk >> i & 0x3F];
	}
	*out = '\0';
	for (; len < 0; len++) *--out = '=';
}

#define ERR(str) { *err = str; return 0; }
int oneTimeHTTPServer(uint16_t port, char *requestStrBuf, char **err) {
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) ERR("Cannot create listening socket.");

	{
		int val = 1;
		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&val, sizeof(int));
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);

	if (bind(sfd, (struct sockaddr*) &addr, sizeof(addr)) != 0) ERR("Cannot bind listening socket.");

	if (listen(sfd, 5) != 0) ERR("Cannot listen.");

	int sfd2 = accept(sfd, NULL, NULL);
	if (sfd2 < 0) ERR("Accept failed.");
	close(sfd);

	char buf[MAX_URL_LEN + 5];
	int size = recv(sfd2, buf, MAX_URL_LEN + 4, 0);
	buf[size] = '\0';

	if (strncmp(buf, "GET ", 4) != 0) ERR("Wrong request.");
	char *end = strchr(buf + 4, ' ');
	if (end) *end = '\0';
	strncpy(requestStrBuf, buf + 4, MAX_URL_LEN);

	char msg[] = "HTTP/1.0 200 OK\r\n\r\n";
	send(sfd2, msg, sizeof(msg) - 1, 0);

	return sfd2;
}

BIO *openSSLConn(char *host, char *port, char **err) {

	// Initialize context (needed until exit so SSL_CTX_free is never called)
	static SSL_CTX* ctx = NULL;
	if (!ctx) {
		SSL_library_init();
		ctx = SSL_CTX_new(SSLv23_client_method());
		if (!ctx) ERR("Cannot initialize SSL context.");
		if (SSL_CTX_set_default_verify_paths(ctx) != 1) ERR("Cannot find CA certificates.");
	}

	// Initialize BIO and SSL objects
	BIO* bio;
	SSL* ssl;

	//bio = BIO_new_ssl_connect(ctx);
	bio = BIO_new(BIO_s_connect());

	BIO_set_conn_hostname(bio, host);
	BIO_set_conn_port(bio, port);

	/*
	BIO_get_ssl(bio, &ssl);
	if(!ssl) ERR("Cannot get SSL object from BIO object.");

	// Enable certificate hostname verification
	if (SSL_set_tlsext_host_name(ssl, host) != 1) ERR("Cannot set tlsext hostname.");
	SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  if (!SSL_set1_host(ssl, host)) ERR("Cannot set hostname for verification.");
  SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
	*/

	// Connect
	if(BIO_do_connect(bio) <= 0) ERR("Connection failed.");

	/*
	// Verify existence of server certificate
	{
		X509* cert = SSL_get_peer_certificate(ssl);
		if (!cert) ERR("Missing SSL certificate.");
		X509_free(cert);
	}

	// Verify the result of chain verification
	if(SSL_get_verify_result(ssl) != X509_V_OK) ERR("SSL certificate verification failed.");
	*/

	return bio;
}

BIO *openWebSocket(char *host, char *port, char *path, char **err) {
	BIO *bio = openSSLConn(host, port, err);
	if (!bio) return NULL;

	BIO_puts(bio, "GET /");
	BIO_puts(bio, path);
	BIO_puts(bio, " HTTP/1.1\r\nHost: ");
	BIO_puts(bio, host);
	BIO_puts(bio, "\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Key: ");
	{
		char b64[25];
		char rand[18] = {0};
		RAND_bytes(rand, 16);
		base64encode(rand, 16, b64);
		BIO_puts(bio, b64);
	}
	BIO_puts(bio, "\r\n"
			"Sec-WebSocket-Version: 13\r\n\r\n");

	char buffer[4096];
	char *b;
	int line = 0;
	do { // check only response code 101 (Sec-WebSocket-Accept is currently not checked)
		b = buffer;
		while ((BIO_read(bio, b, 1) == 1) && (*b++ != '\n') && (b - buffer < 4095)) { }; b--;
		while ((*b == '\n') || (*b == '\r')) b--; *++b = '\0';
		printf("+ %s\n", buffer);
		if ((line == 0) && (strncmp(buffer, "HTTP/1.1 101 ", 13) != 0)) ERR("Wrong answer from server.");
		line++;
	} while (buffer != b);

	return bio;
}

bool webSocketSendSpecial(BIO *bio, char *msg, char opcode, bool first, bool last) {
	static char *buffer = NULL;
	static size_t bufferSize = 0;

	size_t len = msg ? strlen(msg) : 0;
	size_t size = websocket_wrapped_len(len) + 4;
	if (bufferSize < size) {
		buffer = realloc(buffer, size);
	}
	size = websocket_server_wrap(buffer, msg, len, opcode, first, last, 0);
	return BIO_write(bio, buffer, size) > 0;
}
bool webSocketSend(BIO *bio, char *msg) {
	return webSocketSendSpecial(bio, msg, 1, 1, 1); // add fragmentation support
}
bool webSocketSendClose(BIO *bio) {
	return webSocketSendSpecial(bio, NULL, 8, 1, 1);
}
bool webSocketRecvIter(BIO *bio) { // currently only one connection is supported at a time
	static char *buffer = NULL;
	static size_t bufferSize = 0;
	static size_t bufferUsed = 0;

	if (bufferSize - bufferUsed < 1024) {
		buffer = realloc(buffer, 1024 + bufferUsed);
	}

	size_t size = BIO_read(bio, buffer + bufferUsed, 1024);
	if (size <= 0) return false;
	bufferUsed += size;
	bufferUsed = websocket_consume(buffer, bufferUsed, NULL, 0);
	return true;
}
#undef ERR

void *stdinHandler(void *argBio) {
	BIO *bio = argBio;
	char str[4096];
	while (fgets(str, 4096, stdin)) {
		if (!webSocketSend(bio, str)) return NULL;
	}
	webSocketSendClose(bio);
	return NULL;
}

int main() {
	char *err;

	printf("Starting mini HTTP server on port %d...\n", PORT);
	printf("INFO: Make request on http://127.0.0.1:%d/ADDR:PORT/PATH\n");
	printf("INFO: Secure WebSocket connection will be opened to ws://ADDR:PORT/PATH\n");  // SSL wss://
	printf("INFO: PATH may contain '?' followed by parameters\n");

	char requestStr[MAX_URL_LEN];
	int browserSocket = oneTimeHTTPServer(PORT, requestStr, &err);
	if (!browserSocket) {
		printf("Error while waiting for browser connection: %s\n", err);
		return 1;
	}

	{
		char msg[] = "OK.";
		send(browserSocket, msg, sizeof(msg) - 1, 0);
	}
	close(browserSocket);


	char *path = strchr(requestStr + 1, '/');
	if (!path) {
		printf("Error while parsing browser request.\n");
		return 1;
	}
	*path++ = '\0';
	char *port = strchr(requestStr, ':');
	if (port) {
		*port++ = '\0';
	} else port = "80";  // SSL 443
	char *addr = requestStr + 1;

	printf("Connecting to %s on port %s requesting /%s...\n", addr, port, path);


	BIO *bio = openWebSocket(addr, port, path, &err);
	if (!bio) {
		printf("WebSocket error: %s\n", err);
		return 1;
	}

	printf("Connected. Forwarding data from/to stdin/stdout:\n\n");
	pthread_t stdinHandlerThread;
	pthread_create(&stdinHandlerThread, NULL, &stdinHandler, (void *)bio);

	while (webSocketRecvIter(bio)) { } // incoming close requests are not handled yet

	pthread_join(stdinHandlerThread, NULL);
	BIO_free_all(bio);

	return 0;
}

