#include "libcsnet.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#define RANDOM_SIZE sizeof(int64_t)

static int client_handler(struct csnet_socket* client_sock, int stage, char* data, int len);
static int remote_handler(struct csnet_socket* target_sock, int stage, char* data, int len);

struct csnet_log* LOG = NULL;
struct csnet_conntor* CONNTOR = NULL;
char* passwd;

int
business_init(struct csnet_conntor* conntor, struct csnet_log* log, struct csnet_config* config) {
	CONNTOR = conntor;
	LOG = log;

	rand();

	passwd = csnet_config_find(config, "password", strlen("password"));
	if (!passwd) {
		log_f(LOG, "cant find `password` in config file");
	}

	csnet_crypt_set_iv(passwd);

	log_d(log, "business init done ...");
	return 0;
}

void
business_term(void) {

}

int
business_entry(struct csnet_socket* socket, int stage, char* data, int len) {
	if (socket->type == BTGFW_CLIENT) {
		return client_handler(socket, stage, data, len);
	} else {
		return remote_handler(socket, stage, data, len);
	}
}

static int
client_handler(struct csnet_socket* client_sock, int stage, char* data, int len) {
	int ret = 0;

	switch (stage) {
	case SOCKS5_STAGE_EXMETHOD: {
		int ciphertext_len;
		int plaintext_len;
		char* plaintext;

		if (csnet_slow(len < 4)) {
			ret = 0;
			break;
		}

		memcpy((char*)&ciphertext_len, data, 4);
		if (csnet_slow(len < 4 + ciphertext_len)) {
			ret = 0;
			break;
		}

		plaintext_len = csnet_128cfb_decrypt(&plaintext,
						      data + 4,
						      ciphertext_len,
						      passwd);
		if (plaintext_len < 0) {
			log_e(LOG, "decrypt error. exhost.");
			ret = -1;
			break;
		}

		uint8_t ver = plaintext[0 + RANDOM_SIZE];
		uint8_t cmd = plaintext[1 + RANDOM_SIZE];
		uint8_t rsv = plaintext[2 + RANDOM_SIZE];
		uint8_t typ = plaintext[3 + RANDOM_SIZE];

		if (typ == SOCKS5_ATYP_IPv4) {
			char reply[256];
			char ipv4[32];
			uint16_t nport;
			uint16_t hsport;
			struct csnet_socket* target_sock;
			char* cipher_reply;
			int cipher_reply_len;

			uint8_t p0 = plaintext[4 + RANDOM_SIZE];
			uint8_t p1 = plaintext[5 + RANDOM_SIZE];
			uint8_t p2 = plaintext[6 + RANDOM_SIZE];
			uint8_t p3 = plaintext[7 + RANDOM_SIZE];

			int ipv4_str_len = sprintf(ipv4, "%d.%d.%d.%d", p0, p1, p2, p3);
			memcpy((char*)&nport, plaintext + 8 + RANDOM_SIZE, SOCKS5_PORT_SIZE);
			hsport = ntohs(nport);

			target_sock = csnet_conntor_connectto(CONNTOR, ipv4, hsport);
			if (csnet_slow(!target_sock)) {
				log_e(LOG, "failed to connect target host");
				free(plaintext);
				return -1;
			}

			memcpy(client_sock->host, ipv4, ipv4_str_len);
			client_sock->host[ipv4_str_len] = '\0';
			memcpy(target_sock->host, ipv4, ipv4_str_len);
			target_sock->host[ipv4_str_len] = '\0';

			log_d(LOG, "exhost: socket %d >>> socket %d (%s)",
			      client_sock->fd, target_sock->fd, ipv4);

			int64_t randnum = random();
			memcpy(reply, &randnum, RANDOM_SIZE);
			reply[0 + RANDOM_SIZE] = ver;
			reply[1 + RANDOM_SIZE] = SOCKS5_RSP_SUCCEED;
			reply[2 + RANDOM_SIZE] = SOCKS5_RSV;
			reply[3 + RANDOM_SIZE] = SOCKS5_ATYP_IPv4;
			memcpy(reply + 4 + RANDOM_SIZE, plaintext + 4 + RANDOM_SIZE, 4);
			memcpy(reply + 4 + 4 + RANDOM_SIZE, (char*)&nport, SOCKS5_PORT_SIZE);

			cipher_reply_len = csnet_128cfb_encrypt(&cipher_reply,
							        reply,
								SOCKS5_IPV4_REQ_SIZE + RANDOM_SIZE,
								passwd);

			if (cipher_reply_len < 0) {
				log_e(LOG, "encrypt error. socket %d >>> socket %d (%s)",
				      client_sock->fd, target_sock->fd, ipv4);
				free(plaintext);
				return -1;
			}

			csnet_sb_append(&client_sock->sb, (char*)&cipher_reply_len, 4);
			csnet_sb_append(&client_sock->sb, cipher_reply, cipher_reply_len);
			csnet_socket_send(client_sock);

			log_d(LOG, "exhost: socket %d <<< socket %d (%s)",
				  client_sock->fd, target_sock->fd, ipv4);

			client_sock->sock = target_sock;
			target_sock->sock = client_sock;

			free(cipher_reply);
			free(plaintext);

			client_sock->stage = SOCKS5_STAGE_STREAM;
			target_sock->stage = SOCKS5_STAGE_STREAM;

			ret = 4 + ciphertext_len;
		} else if (typ == SOCKS5_ATYP_DONAME) {
			uint8_t domain_name_len;
			char domain_name[256];
			uint16_t nport;
			uint16_t hsport;
			struct csnet_socket* target_sock;
			char reply[256];
			char* cipher_reply;
			int cipher_reply_len;

			domain_name_len = plaintext[SOCKS5_REQ_HEAD_SIZE + RANDOM_SIZE];
			memcpy(domain_name,
			       plaintext + SOCKS5_REQ_HEAD_SIZE + 1 + RANDOM_SIZE,
			       domain_name_len);
			domain_name[domain_name_len] = '\0';
			memcpy((char*)&nport,
			       plaintext + SOCKS5_REQ_HEAD_SIZE + 1 + domain_name_len + RANDOM_SIZE,
			       SOCKS5_PORT_SIZE);
			hsport = ntohs(nport);

			target_sock = csnet_conntor_connectto(CONNTOR, domain_name, hsport);
			if (csnet_slow(!target_sock)) {
				log_e(LOG, "failed to connect target host");
				free(plaintext);
				return -1;
			}

			memcpy(client_sock->host, domain_name, domain_name_len);
			client_sock->host[domain_name_len] = '\0';
			memcpy(target_sock->host, domain_name, domain_name_len);
			target_sock->host[domain_name_len] = '\0';

			log_d(LOG, "exhost: socket %d >>> socket %d (%s)",
				  client_sock->fd, target_sock->fd, domain_name);

			int64_t randnum = random();
			memcpy(reply, &randnum, RANDOM_SIZE);

			reply[0 + RANDOM_SIZE] = ver;
			reply[1 + RANDOM_SIZE] = SOCKS5_RSP_SUCCEED;
			reply[2 + RANDOM_SIZE] = SOCKS5_RSV;
			reply[3 + RANDOM_SIZE] = SOCKS5_ATYP_DONAME;
			reply[4 + RANDOM_SIZE] = domain_name_len;
			memcpy(reply + 5 + RANDOM_SIZE, domain_name, domain_name_len);
			memcpy(reply + 5 + domain_name_len + RANDOM_SIZE,
			       (char*)&nport,
			       SOCKS5_PORT_SIZE);

			cipher_reply_len = csnet_128cfb_encrypt(&cipher_reply,
								reply,
								5 + domain_name_len + SOCKS5_PORT_SIZE + RANDOM_SIZE,
								passwd);
			if (cipher_reply_len < 0) {
				log_e(LOG, "encrypt error. socket %d >>> socket %d (%s)",
				      client_sock->fd, target_sock->fd, domain_name);
				free(plaintext);
				return -1;
			}

			csnet_sb_append(&client_sock->sb, (char*)&cipher_reply_len, 4);
			csnet_sb_append(&client_sock->sb, cipher_reply, cipher_reply_len);
			csnet_socket_send(client_sock);

			log_d(LOG, "exhost: socket %d <<< socket %d (%s)",
			      client_sock->fd, target_sock->fd, domain_name);

			client_sock->sock = target_sock;
			target_sock->sock = client_sock;

			free(cipher_reply);
			free(plaintext);

			client_sock->stage = SOCKS5_STAGE_STREAM;
			target_sock->stage = SOCKS5_STAGE_STREAM;

			ret = 4 + ciphertext_len;
		} else if (typ == SOCKS5_ATYP_IPv6) {
			log_e(LOG, "unsupport IPv6 yet");
			ret = -1;
		} else {
			log_e(LOG, "unknown address type");
			ret = -1;
		}
		break;
	}

	case SOCKS5_STAGE_STREAM: {
		struct csnet_socket* target_sock = client_sock->sock;
		if (!target_sock) {
			ret = -1;
			break;
		}
		log_d(LOG, "stream: socket %d >>> socket %d (%s)",
		      client_sock->fd, target_sock->fd, target_sock->host);
		csnet_sb_append(&target_sock->sb, data, len);
		csnet_socket_send(target_sock);
		ret = len;
		break;
	}

	default:
		ret = -1;
		break;
	}

	return ret;
}

static int
remote_handler(struct csnet_socket* target_sock, int stage, char* data, int len) {
	struct csnet_socket* client_sock = target_sock->sock;

	if (!client_sock) {
		return -1;
	}

	csnet_sb_append(&client_sock->sb, data, len);
	csnet_socket_send(client_sock);

	log_d(LOG, "stream: socket %d <<< socket %d (%s)",
	      client_sock->fd, target_sock->fd, target_sock->host);

	return len;
}

