#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#ifdef USE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif
#if defined(__FreeBSD__)
#define __BSD_VISIBLE 1
#endif
#include "imap.h"

struct imap_ll {
	int w_socket;
	int r_socket;
	time_t timeout_time;
	char buffer[1024];
	char *bufferp;
	size_t buffer_left;
	int current_tag_number;
#ifdef USE_OPENSSL
	SSL *ssl;
#endif

	char *selected_folder;
	size_t selected_folder_namelen;
	char *selected_folder_uidvalidity;
	int selected_folder_writable;
	long selected_folder_exists;
};

struct imap_ll *
imap_ll_connect(const char *host, const char *port)
{
struct imap_ll *ll;
struct addrinfo hints, *res, *res0;
int error;
int s;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(host, port, &hints, &res0);
	if (error) {
		fprintf(stderr, "getaddrinfo \"%s\":\"%s\": %s\n",
			host, port, gai_strerror(error)
		);
		return NULL;
	}
	s = -1;
	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s < 0) {
			fprintf(stderr, "socket \"%s\":\"%s\": %s\n",
				host, port, strerror(errno)
			);
			continue;
		}
		if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
			fprintf(stderr, "connect \"%s\":\"%s\": %s\n",
				host, port, strerror(errno)
			);
			close(s);
			s = -1;
			continue;
		}
		break;
	}
	if (s == -1) {
		if (!res0) {
			fprintf(stderr, "server \"%s\":\"%s\": no available addresses\n",
				host, port
			);
		}
		return NULL;
	}

	ll = malloc(sizeof(*ll));
	if (!ll) {
		fprintf(stderr, "malloc failed\n");
		close(s);
		return NULL;
	}
	memset(ll, 0, sizeof(*ll));
	ll->w_socket = ll->r_socket = s;
	ll->current_tag_number = -1;
#ifdef USE_OPENSSL
	ll->ssl = NULL;
#endif
	fcntl(s, F_SETFL, O_NONBLOCK);

	return ll;
}

struct imap_ll *
imap_ll_pipe_connect(const char *cmd)
{
struct imap_ll *ll;
pid_t child;
int status;
int r[2], w[2];

	if (pipe(&(r[0])) < 0) {
		fprintf(stderr, "pipe1 failed\n");
		return NULL;
	}
	if (pipe(&(w[0])) < 0) {
		fprintf(stderr, "pipe2 failed\n");
		close(r[0]);
		close(r[1]);
		return NULL;
	}
	if ((child = fork()) < 0) {
		fprintf(stderr, "fork failed\n");
		return NULL;
	}
	if (child == 0) {
		close(r[0]);
		close(w[1]);
		if (r[1] != STDOUT_FILENO) {
			dup2(r[1], STDOUT_FILENO);
			close(r[1]);
		}
		if (w[0] != STDIN_FILENO) {
			dup2(w[0], STDIN_FILENO);
			close(w[0]);
		}
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		_exit(1);
	}
	close(r[1]);
	close(w[0]);

	ll = malloc(sizeof(*ll));
	if (!ll) {
		fprintf(stderr, "malloc failed\n");
		close(r[0]);
		close(w[1]);
		kill(child, SIGTERM);
		waitpid(child, &status, 0);
		return NULL;
	}
	memset(ll, 0, sizeof(*ll));
	ll->w_socket = w[1];
	ll->r_socket = r[0];
	ll->current_tag_number = -1;
#ifdef USE_OPENSSL
	ll->ssl = NULL;
#endif
	fcntl(w[1], F_SETFL, O_NONBLOCK);
	fcntl(r[0], F_SETFL, O_NONBLOCK);

	return ll;
}

void
imap_ll_timeout(struct imap_ll *ll, int seconds)
{
	ll->timeout_time = time(NULL) + seconds;
}

static struct imap_ll_tokenlist *
mktoken(int type, char *leaf, size_t len)
{
struct imap_ll_tokenlist *tl;

	tl = malloc(sizeof(*tl));
	if (!tl) {
		fprintf(stderr, "imap_ll mktoken: malloc failed (fatal)\n");
		sleep(1);
		_exit(0);
	}
	tl->type = type;
	tl->first = tl->last = NULL;
	tl->parent = NULL;
	tl->leaf = leaf;
	tl->leaflen = len;
	if (leaf) leaf[len] = 0;
	return tl;
}

static void
growlist(struct imap_ll_tokenlist **nodep, struct imap_ll_tokenlist *tl)
{
	if ((*nodep)->last) {
		(*nodep)->last->next = tl;
	} else {
		(*nodep)->first = tl;
	}
	(*nodep)->last = tl;
	tl->parent = *nodep;
	tl->next = NULL;
	if (!(tl->leaf)) {
		*nodep = tl;
	}
}

void
imap_ll_freeline(struct imap_ll_tokenlist *t)
{
struct imap_ll_tokenlist *t2;

	if (t) {
		if (t->leaf) free(t->leaf);
		for (t2 = t->first; t2; t2 = t2->next) {
			imap_ll_freeline(t2);
		}
		free(t);
	}
}

#define MKPRETOKEN(alloc_size) { \
	curtoken = malloc((alloc_size) + 1); \
	if (!curtoken) { \
		fprintf(stderr, "imap_ll_waitline(%d): malloc failed (fatal)\n", (alloc_size)); \
		sleep(1); \
		_exit(0); \
	} \
	curtoken_now = 0; \
	curtoken_max = (alloc_size); \
}

struct imap_ll_tokenlist *
imap_ll_waitline(struct imap_ll *ll)
{
int timeout, n;
#ifdef USE_OPENSSL
int ret2;
#endif
char cur;
struct imap_ll_tokenlist *line = NULL;
struct imap_ll_tokenlist *node = NULL;
int linestate = 0;
int recv_tag;
char *curtoken = NULL;
size_t curtoken_now;
size_t curtoken_max;
struct pollfd pfd;

	for (;;) {
		while (ll->buffer_left) {
			cur = *(ll->bufferp);
			ll->bufferp++;
			ll->buffer_left--;
			if ((cur == 10) && (linestate < 7)) {
				if (curtoken && (linestate == 4)) {
					growlist(&node, mktoken(TLTYPE_ATOM, curtoken, curtoken_now));
				} else if (curtoken) {
					free(curtoken);
				}
				curtoken = NULL;
				return line;
			}
			switch (linestate) {
				case 0:
					/* expect start of line */
					if (cur == '*') {
						linestate = 1;
					} else if (cur == 't') {
						linestate = 2;
						recv_tag = 0;
					} else if (cur == '+') {
						line = mktoken(TLTYPE_CONTINUATION, NULL, 0);
						linestate = 3;
					} else {
						linestate = 3;
					}
					break;
				case 1:
					/* expect space after untagged prefix */
					if (cur == ' ') {
						node = line = mktoken(TLTYPE_UNTAGGED, NULL, 0);
						linestate = 4;
					} else {
						linestate = 3;
					}
					break;
				case 2:
					/* expect tag */
					if (cur == ' ') {
						if (recv_tag == ll->current_tag_number) {
							node = line = mktoken(TLTYPE_TAGGED, NULL, 0);
							linestate = 4;
						} else {
							/* wrong tag */
							linestate = 3;
						}
					} else if ((cur >= '0') && (cur <= '9')) {
						recv_tag *= 10;
						recv_tag += cur - '0';
					} else {
						linestate = 3;
					}
					break;
				case 3:
					/* error -- discard until end of line */
					break;
				case 4:
					/* looking for start of next "thing" */
					if (cur == 34) {
						linestate = 5;
						MKPRETOKEN(50);
					} else if (cur == 123) {
						linestate = 6;
						recv_tag = 0;
					} else if (
						(cur == 91) /* square bracket */ &&
						(line == node) /* top level */ &&
						(line->first) /* not first in list */ &&
						/* list has exactly 1 */
						(line->first == line->last)
					) {
						growlist(&node, mktoken(TLTYPE_SQLIST, NULL, 0));
					} else if (cur == 40) {
						growlist(&node, mktoken(TLTYPE_LIST, NULL, 0));
					} else if ((
						(node->type == TLTYPE_LIST) &&
						(cur == 41)
					) || (
						(node->type == TLTYPE_SQLIST) &&
						(cur == 93)
					)) {
						if (curtoken) {
							growlist(&node, mktoken(TLTYPE_ATOM, curtoken, curtoken_now));
							curtoken = NULL;
						}
						node = node->parent;
					} else if (
	
						(cur == ' ') ||
						(cur == 9) ||
						(cur == 13)
					) {
						if (curtoken) {
							growlist(&node, mktoken(TLTYPE_ATOM, curtoken, curtoken_now));
							curtoken = NULL;
						}
					} else {
						if (!curtoken) {
							MKPRETOKEN(25);
						}
token_extend:
						if (curtoken_now >= curtoken_max) {
							curtoken_max *= 2;
							curtoken = realloc(curtoken, curtoken_max + 1);
							if (!curtoken) {
								fprintf(stderr, "imap_ll_waitline(size=%d): malloc failed (fatal)\n", (int)curtoken_max);
								sleep(1);
								_exit(0);
							}
						}
						curtoken[curtoken_now++] = cur;
					}
					break;
				case 5:
					/* quoted string */
					if (cur == 34) {
						growlist(&node, mktoken(TLTYPE_STRING, curtoken, curtoken_now));
						curtoken = NULL;
						linestate = 4;
					} else {
						goto token_extend;
					}
					break;
				case 6:
					/* literal count */
					if (cur == 125) {
						linestate = 7;
					} else if ((cur >= '0') && (cur <= '9')) {
						recv_tag *= 10;
						recv_tag += cur - '0';
					} else {
						linestate = 3;
					}
					break;
				case 7:
					/* CRLF after {nn} */
					if (cur == 13) break;
					if (cur != 10) {
						linestate = 3;
					}
					MKPRETOKEN(recv_tag);
					if (recv_tag == 0) {
						growlist(&node, mktoken(TLTYPE_STRING, curtoken, curtoken_now));
						curtoken = NULL;
						linestate = 4;
					} else {
						linestate = 8;
					}
					break;
				case 8:
					/* literal contents */
					curtoken[curtoken_now++] = cur;
					if (curtoken_now == curtoken_max) {
						growlist(&node, mktoken(TLTYPE_STRING, curtoken, curtoken_now));
						curtoken = NULL;
						linestate = 4;
					}
					break;
			}
		}
#ifdef USE_OPENSSL
		if (ll->ssl) {
			while ((n = SSL_read(ll->ssl, &((ll->buffer)[0]), sizeof(ll->buffer))) <= 0) {
				ret2 = SSL_get_error(ll->ssl, n);
				if ((ret2 == SSL_ERROR_WANT_READ) || (ret2 == SSL_ERROR_WANT_WRITE)) {
					if (ret2 == SSL_ERROR_WANT_READ) {
						pfd.fd = ll->r_socket;
						pfd.events = POLLIN;
					} else {
						pfd.fd = ll->w_socket;
						pfd.events = POLLOUT;
					}
					timeout = ll->timeout_time - time(NULL);
					if (timeout < 0) timeout = 0;
					if ((n = poll(&pfd, 1, timeout*1000)) < 0) {
						if (errno == EINTR) continue;
						goto pollerr;
					}
					if (n == 0) goto polltimeout;
				} else {
					ERR_print_errors_fp(stderr);
					imap_ll_freeline(line);
					return NULL;
				}
			}
		} else {
#endif
			pfd.fd = ll->r_socket;
			pfd.events = POLLIN;
repoll:
			timeout = ll->timeout_time - time(NULL);
			if (timeout < 0) timeout = 0;
			if ((n = poll(&pfd, 1, timeout*1000)) < 0) {
				if (errno == EINTR) goto repoll;
#ifdef USE_OPENSSL
pollerr:
#endif
				fprintf(stderr, "imap_ll_waitline: poll: %s\n", strerror(errno));
				imap_ll_freeline(line);
				return NULL;
			}
			if (n == 0) {
#ifdef USE_OPENSSL
polltimeout:
#endif
				fprintf(stderr, "imap_ll_waitline: timeout\n");
				imap_ll_freeline(line);
				return NULL;
			}
			if ((n = read(ll->r_socket, &((ll->buffer)[0]), sizeof(ll->buffer))) < 0) {
				if (errno == EAGAIN) continue;
				if (errno == EINTR) continue;
				fprintf(stderr, "imap_ll_waitline: read: %s\n", strerror(errno));
				imap_ll_freeline(line);
				return NULL;
			}
			if (n == 0) {
				fprintf(stderr, "imap_ll_waitline: read: EOF\n");
				imap_ll_freeline(line);
				return NULL;
			}
#ifdef USE_OPENSSL
		}
#endif
		ll->buffer_left = n;
		ll->bufferp = &((ll->buffer)[0]);
	}
}

/* this may overestimate the size a bit */
static size_t
string_len(struct imap_ll_tokenlist *t)
{
size_t result;

	switch (t->type) {
		case TLTYPE_UNTAGGED:
			result = 2;
			break;
		case TLTYPE_TAGGED:
			result = 15;
			break;
		case TLTYPE_LIST:
		case TLTYPE_SQLIST:
			result = 2;
			break;
		case TLTYPE_ATOM:
			result = t->leaflen;
			break;
		case TLTYPE_STRING:
			/* worst case is if it's sent as a literal */
			result = 20 + t->leaflen;
			break;
		case TLTYPE_CONTINUATION:
			/* shouldn't be used */
			result = 2;
			break;
		case TLTYPE_POP:
		case TLTYPE_END:
		case TLTYPE_SUB:
		default:
			result = 0;
	}
	for (t = t->first; t; t = t->next) {
		result++;
		result += string_len(t);
	}
	return result;
}

static char *
format_cmd(char *out, struct imap_ll *ll, struct imap_ll_tokenlist *t)
{
int i;
char close = 0;

	switch (t->type) {
		case TLTYPE_UNTAGGED:
			*(out++) = '*';
			*(out++) = ' ';
		case TLTYPE_TAGGED:
			out += sprintf(out, "t%d ", ++(ll->current_tag_number));
			break;
		case TLTYPE_LIST:
			*(out++) = '('; close = ')';
			break;
		case TLTYPE_SQLIST:
			*(out++) = '['; close = ']';
			break;
		case TLTYPE_ATOM:
			memcpy(out, t->leaf, t->leaflen);
			out += t->leaflen;
			break;
		case TLTYPE_STRING:
			for (i = 0; i < t->leaflen; i++) {
				if (
					(t->leaf[i] < 32) ||
					(t->leaf[i] == 34) ||
					(t->leaf[i] > 126)
				) break;
			}
			if (i < t->leaflen) {
				/* send as literal */
				out += sprintf(out, "{%d+}\015\012", (int)(t->leaflen));
				memcpy(out, t->leaf, t->leaflen);
				out += t->leaflen;
			} else {
				/* send as regular string */
				*(out++) = 34;
				memcpy(out, t->leaf, t->leaflen);
				out += t->leaflen;
				*(out++) = 34;
			}
			break;
		case TLTYPE_CONTINUATION:
			*(out++) = '+';
			*(out++) = ' ';
			break;
		case TLTYPE_END:
		case TLTYPE_POP:
		case TLTYPE_SUB:
			break;
	}
	i = 0;
	for (t = t->first; t; t = t->next) {
		if (i) {
			*(out++) = ' ';
		} else {
			i = 1;
		}
		out = format_cmd(out, ll, t);
	}
	if (close) {
		*(out++) = close;
	}
	return out;
}

static int
imap_ll_write(struct imap_ll *ll, const char *p, const char *cmdend)
{
ssize_t len;
struct pollfd pfd;
int ctimeout, n;
#ifdef USE_OPENSSL
int ret2;

	if (ll->ssl) {
		while (p < cmdend) {
			n = SSL_write(ll->ssl, p, cmdend-p);
			if (n > 0) {
				p += n;
			} else {
				ret2 = SSL_get_error(ll->ssl, n);
				if ((ret2 == SSL_ERROR_WANT_READ) || (ret2 == SSL_ERROR_WANT_WRITE)) {
					if (ret2 == SSL_ERROR_WANT_READ) {
						pfd.fd = ll->r_socket;
						pfd.events = POLLIN;
					} else {
						pfd.fd = ll->w_socket;
						pfd.events = POLLOUT;
					}
					ctimeout = ll->timeout_time - time(NULL);
					if (ctimeout < 0) ctimeout = 0;
					if ((n = poll(&pfd, 1, ctimeout*1000)) < 0) {
						if (errno == EINTR) continue;
						goto pollerr;
					}
					if (n == 0) goto polltimeout;
				} else {
					ERR_print_errors_fp(stderr);
					return 0;
				}
			}
		}
	} else {
#endif
		pfd.fd = ll->w_socket;
		pfd.events = POLLOUT;
		while (p < cmdend) {
repoll:
			ctimeout = ll->timeout_time - time(NULL);
			if (ctimeout < 0) ctimeout = 0;
			if ((n = poll(&pfd, 1, ctimeout*1000)) < 0) {
				if (errno == EINTR) goto repoll;
#ifdef USE_OPENSSL
pollerr:
#endif
				fprintf(stderr, "imap_ll_command: poll: %s\n", strerror(errno));
				return 0;
			}
			if (n == 0) {
#ifdef USE_OPENSSL
polltimeout:
#endif
				fprintf(stderr, "imap_ll_command: timeout\n");
				return 0;
			}
			len = write(ll->w_socket, p, cmdend-p);
			if (len < 0) {
				if (errno == EAGAIN) continue;
				if (errno == EINTR) continue;
				fprintf(stderr, "imap_ll_command: write: %s\n", strerror(errno));
				return 0;
			} else if (len == 0) {
				fprintf(stderr, "imap_ll_command: short write\n");
				return 0;
			}
			p += len;
		}
#ifdef USE_OPENSSL
	}
#endif
	return 1;
}

struct imap_ll_tokenlist *
imap_ll_command(struct imap_ll *ll, struct imap_ll_tokenlist *cmd, int timeout)
{
size_t len;
char *cmds, *cmdend;
struct imap_ll_tokenlist *result, *cur;

	len = string_len(cmd);
	cmds = malloc(len+1);
	if (!cmds) {
		fprintf(stderr, "imap_ll_command: malloc failed\n");
		return NULL;
	}
	cmdend = format_cmd(cmds, ll, cmd);
	*(cmdend++) = 13;
	*(cmdend++) = 10;
	imap_ll_timeout(ll, timeout);
	if (!imap_ll_write(ll, cmds, cmdend)) {
		free(cmds);
		return NULL;
	}
	free(cmds);
	result = mktoken(TLTYPE_LIST, NULL, 0);
	for (;;) {
		if (!(cur = imap_ll_waitline(ll))) {
			imap_ll_freeline(result);
			return NULL;
		}
		cur->parent = result;
		cur->next = NULL;
		if (result->first) {
			result->last->next = cur;
		} else {
			result->first = cur;
		}
		result->last = cur;

		if (cur->type == TLTYPE_TAGGED) {
			cur->next = NULL;
			return result;
		}
        }
}

void
imap_ll_logout(struct imap_ll *ll)
{
size_t len;
struct imap_ll_tokenlist *cur;
char cmd[25];

	len = sprintf(cmd, "t%d LOGOUT\015\012", ++(ll->current_tag_number));
	imap_ll_timeout(ll, 5);
	if (!imap_ll_write(ll, cmd, cmd+len)) return;
	imap_ll_timeout(ll, 5);
	for (;;) {
		if (!(cur = imap_ll_waitline(ll))) {
			return;
		}
		if (cur->type == TLTYPE_TAGGED) {
			break;
		}
		imap_ll_freeline(cur);
	}
	imap_ll_freeline(cur);
}

struct imap_ll_tokenlist *
imap_ll_build(enum imap_ll_tltype maintype, ...)
{
va_list ap;
struct imap_ll_tokenlist *top = NULL;
struct imap_ll_tokenlist *cur = NULL;
struct imap_ll_tokenlist *l;
char *s;
size_t slen;
int descend;
enum imap_ll_tltype next_type = maintype;

	va_start(ap, maintype);
	for (;;) {
		switch (next_type) {
			default:
			case TLTYPE_END:
				va_end(ap);
				return top;
			case TLTYPE_SUB:
				l = va_arg(ap, struct imap_ll_tokenlist *);
				descend = 0;
				break;
			case TLTYPE_ATOM:
			case TLTYPE_STRING:
				s = va_arg(ap, char *);
				slen = va_arg(ap, size_t);
				if (slen == -1) slen = strlen(s);
				l = malloc(sizeof(*l));
				if (!l) {
nomem:
					fprintf(stderr, "imap_ll_build: malloc failure\n");
					imap_ll_freeline(top);
					va_end(ap);
					return NULL;
				}
				l->type = next_type;
				l->leaf = malloc(slen+1);
				if (!(l->leaf)) {
					free(l);
					goto nomem;
				}
				memcpy(l->leaf, s, slen);
				l->leaf[slen] = 0;
				l->leaflen = slen;
				descend = 0;
				break;
			case TLTYPE_TAGGED:
			case TLTYPE_UNTAGGED:
			case TLTYPE_LIST:
			case TLTYPE_SQLIST:
				l = malloc(sizeof(*l));
				if (!l) goto nomem;
				l->type = next_type;
				l->leaf = NULL;
				descend = 1;
				break;
			case TLTYPE_CONTINUATION:
				l = malloc(sizeof(*l));
				if (!l) goto nomem;
				l->type = TLTYPE_CONTINUATION;
				l->leaf = NULL;
				descend = 0;
				break;
			case TLTYPE_POP:
				break;
		}
		if (next_type == TLTYPE_POP) {
			cur = cur->parent;
		} else {
			if (next_type != TLTYPE_SUB) l->first = l->last = NULL;
			l->parent = cur;
			l->next = NULL;
			if (cur) {
				if (cur->last) {
					cur->last->next = l;
					cur->last = l;
				} else {
					cur->first = cur->last = l;
				}
			} else {
				top = cur = l;
			}
			if (descend) cur = l;
		}
		next_type = va_arg(ap, int);
	}
}

void
imap_ll_append(struct imap_ll_tokenlist *list, struct imap_ll_tokenlist *item)
{
	if (list->last) {
		list->last->next = item;
	} else {
		list->first = item;
	}
	list->last = item;
	item->parent = list;
}

const char *
imap_ll_status(struct imap_ll_tokenlist *t)
{
	t = t->last->first;
	if (!t) return "BAD";
	if ((t->type) != TLTYPE_ATOM) return "BAD";
	return t->leaf;
}

int
imap_ll_is_trycreate(struct imap_ll_tokenlist *t)
{
	return (
		(0 == strcmp(imap_ll_status(t), "NO")) &&
		(t->last->first->next) &&
		((t->last->first->next->type) == TLTYPE_SQLIST) &&
		(t->last->first->next->first) &&
		((t->last->first->next->first->type) == TLTYPE_ATOM) &&
		(0 == strcmp(t->last->first->next->first->leaf, "TRYCREATE"))
	);
}

#ifdef USE_OPENSSL
enum imap_ll_starttls_result
imap_ll_starttls(struct imap_ll *ll, SSL_CTX *sslctx, const char *servername)
{
struct imap_ll_tokenlist *cmd, *l;
const char *status;
struct pollfd pfd;
int ret, ret2, timeout;
SSL *ssl;
long vr;
X509 *peer;
size_t servernamelen, certnamelen;
char buf[256];
char buf2[256];

	ssl = SSL_new(sslctx);
	if (!ssl) {
		ERR_print_errors_fp(stderr);
	}

	cmd = imap_ll_build(TLTYPE_TAGGED, TLTYPE_ATOM, "STARTTLS", (size_t)-1, TLTYPE_END);
	if (!cmd) {
		fprintf(stderr, "could not build STARTTLS command\n");
		SSL_free(ll->ssl); ll->ssl = NULL;
		return IMAP_LL_STARTTLS_FAILED_PROCEED;
	}
	l = imap_ll_command(ll, cmd, 5);
        imap_ll_freeline(cmd);
        status = imap_ll_status(l);
        if (0 == strcmp(status, "NO")) {
		fprintf(stderr, "STARTTLS not accepted by server\n");
		imap_ll_freeline(l);
		SSL_free(ll->ssl); ll->ssl = NULL;
		return IMAP_LL_STARTTLS_FAILED_PROCEED;
	} else if (0 != strcmp(status, "OK")) {
		fprintf(stderr, "STARTTLS not understood by server\n");
		imap_ll_freeline(l);
		SSL_free(ll->ssl); ll->ssl = NULL;
		return IMAP_LL_STARTTLS_FAILED_PROCEED;
	}
	imap_ll_freeline(l);

	SSL_set_rfd(ssl, ll->r_socket);
	SSL_set_wfd(ssl, ll->w_socket);

	imap_ll_timeout(ll, 10);	/* reset clock */
	for (;;) {
		ret = SSL_connect(ssl);
		if (ret == 1) break;
		ret2 = SSL_get_error(ssl, ret);
		if ((ret2 == SSL_ERROR_WANT_READ) || (ret2 == SSL_ERROR_WANT_WRITE)) {
			if (ret2 == SSL_ERROR_WANT_READ) {
				pfd.fd = ll->r_socket;
				pfd.events = POLLIN;
			} else {
				pfd.fd = ll->w_socket;
				pfd.events = POLLOUT;
			}
			timeout = ll->timeout_time - time(NULL);
			if (timeout < 0) timeout = 0;
			if ((ret = poll(&pfd, 1, timeout*1000)) < 0) {
				if (errno == EINTR) continue;
				fprintf(stderr, "STARTTLS poll error: %s\n", strerror(errno));
				SSL_free(ssl);
				return IMAP_LL_STARTTLS_FAILED;
			}
			if (ret == 0) {
				fprintf(stderr, "STARTTLS timeout\n");
				SSL_free(ssl);
				return IMAP_LL_STARTTLS_FAILED;
			}
		} else {
			ERR_error_string(ERR_get_error(), &(buf[0]));
			fprintf(stderr, "STARTTLS: %s\n", buf);
			SSL_free(ssl);
			return IMAP_LL_STARTTLS_FAILED;
		}
	}
	if ((vr = SSL_get_verify_result(ssl)) != X509_V_OK) {
		fprintf(stderr, "STARTTLS: ceritifcate verification failed (%ld)\n", vr);
certfail:
		SSL_free(ssl);
		return IMAP_LL_STARTTLS_FAILED_CERT;
	}
	if (!(peer = SSL_get_peer_certificate(ssl))) {
		fprintf(stderr, "STARTTLS: no peer certificate?\n");
		goto certfail;
	}
	if (X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, buf, sizeof(buf)) == -1) {
		fprintf(stderr, "STARTTLS: no common name in certificate?\n");
		goto certfail;
	}
	if (!servername) goto certok; /* caller does not want us to check */
	if (0 != strcasecmp(buf, servername)){
		/* Certificate name does not match exactly.
		   Check for a wildcard certificate name.
		   The only valid form for certificate naming wildcards is
		   an asterisk as the first character followed by any number
		   of non-wild characters */
		if (buf[0] == '*') {
			servernamelen = strlen(servername);
			certnamelen = strlen(buf+1);
			if (
				(servernamelen >= certnamelen) &&
				(0 == strcasecmp(
					buf+1,
					servername + (servernamelen-certnamelen))
				)
			) {
				goto certok;
			}
		}
		snprintf(buf2, sizeof(buf2), "%s", servername);
		fprintf(stderr, "STARTTLS: server name \"%s\" != certificate common name \"%s\"\n", buf2, buf);
		goto certfail;
	}

certok:
	ll->ssl = ssl;
	return IMAP_LL_STARTTLS_SUCCESS;
}
#endif

void
imap_ll_pprint(struct imap_ll_tokenlist *t, int indent, FILE *out)
{
int plusindent = 0;
int i;
const char *close = NULL;

	switch (t->type) {
		case TLTYPE_UNTAGGED:
			for (i = 0; i < indent; i++) fputc(9, out);
			fprintf(out, "*\n");
			break;
		case TLTYPE_TAGGED:
			break;
		case TLTYPE_LIST:
			plusindent = 1;
			for (i = 0; i < indent; i++) fputc(9, out);
			fprintf(out, "(\n"); close = ")\n";
			break;
		case TLTYPE_SQLIST:
			plusindent = 1;
			for (i = 0; i < indent; i++) fputc(9, out);
			fprintf(out, "[\n"); close = "]\n";
			break;
		case TLTYPE_ATOM:
		case TLTYPE_STRING:
			for (i = 0; i < indent; i++) fputc(9, out);
			if ((t->type) == TLTYPE_STRING) fputc(34, out);
			for (i = 0; i < (t->leaflen); i++) {
				if (
					(t->leaf[i] >= ' ') &&
					(t->leaf[i] < 127) &&
					(t->leaf[i] != 34) &&
					(t->leaf[i] != 92)
				) {
					fputc(t->leaf[i], out);
				} else {
					fprintf(out, "\\x%02x", t->leaf[i]);
				}
			}
			if ((t->type) == TLTYPE_STRING) fputc(34, out);
			fputc('\n', out);
			break;
		case TLTYPE_CONTINUATION:
			for (i = 0; i < indent; i++) fputc(9, out);
			fputs("+ \n", out);
			break;
		case TLTYPE_END:
		case TLTYPE_POP:
		case TLTYPE_SUB:
			break;
	}
	indent += plusindent;
	for (t = t->first; t; t = t->next) {
		imap_ll_pprint(t, indent, out);
	}
	indent -= plusindent;
	if (close) {
		for (i = 0; i < indent; i++) fputc(9, out);
		fputs(close, out);
	}
}

static struct imap_ll_tokenlist *
find_capability_list_in_untagged_response(struct imap_ll_tokenlist *l)
{
	for (l = l->first; l; l = l->next) {
		if (
			((l->type) == TLTYPE_UNTAGGED) &&
			((l->first->type) == TLTYPE_ATOM) &&
			(0 == strcmp(l->first->leaf, "CAPABILITY"))
		) return l->first->next;
	}
	fprintf(stderr, "IMAP client cannot find capabilities list\n");
	return NULL;
}

static struct imap_ll_tokenlist *
ask_for_capabilities(struct imap_ll *ll)
{
struct imap_ll_tokenlist *cmd, *result;

	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "CAPABILITY", (size_t)-1,
		TLTYPE_END
	);
	result = imap_ll_command(ll, cmd, 10);
	imap_ll_freeline(cmd);
	return result;
}

/* caller should free whatever this function fills into freeme_p */
static struct imap_ll_tokenlist *
find_capability_list_from_greeting(struct imap_ll *ll, struct imap_ll_tokenlist *l, struct imap_ll_tokenlist **freeme_p)
{
	*freeme_p = NULL;
	if (
		(!(l->first)) || (!(l->first->next)) ||
		(l->first->next->type != TLTYPE_SQLIST) ||
		(!(l->first->next->first)) ||
		(!(l->first->next->first->leaf)) ||
		(0 != strcmp(l->first->next->first->leaf, "CAPABILITY"))
	) {
		/* Server did not send capabilities in greeting. */
		*freeme_p = l = ask_for_capabilities(ll);
		return find_capability_list_in_untagged_response(l);
	} else {
		return l->first->next->first->next;
	}
}

#ifdef USE_OPENSSL
static int
have_capability(struct imap_ll_tokenlist *l, const char *cap)
{
	for (; l; l = l->next) {
		if ((l->leaf) && (0 == strcmp(l->leaf, cap))) return 1;
	}
	return 0;
}
#endif

static int
need_capability(struct imap_ll_tokenlist *l, const char *cap)
{
	for (; l; l = l->next) {
		if ((l->leaf) && (0 == strcmp(l->leaf, cap))) return 1;
	}
	fprintf(stderr, "IMAP server missing required capability %s\n", cap);
	return 0;
}

enum imap_login_result
imap_login(
	struct imap_ll *ll,
	const char *username, size_t username_len,
	const char *password, size_t password_len
#ifdef USE_OPENSSL
	, SSL_CTX *sslctx, const char *servername
#endif
) {
struct imap_ll_tokenlist *cmd, *result, *caps, *freeme;
enum imap_login_result ret;
const char *status;

	freeme = NULL;
	imap_ll_timeout(ll, 60);
	if (!(result = imap_ll_waitline(ll))) {
		fprintf(stderr, "error reading IMAP server greeting\n");
		return imap_login_error;
	}

	if (
		(!(result->first)) ||
		((result->first->type) != TLTYPE_ATOM)
	) {
		fprintf(stderr, "cannot parse IMAP server greeting\n");
		imap_ll_freeline(result);
		return imap_login_error;
	}
	if (0 == strcmp(result->first->leaf, "BYE")) {
		imap_ll_freeline(result);
		fprintf(stderr, "IMAP server rejected connection\n");
		return imap_login_error;
	} else if (0 == strcmp(result->first->leaf, "PREAUTH")) {
		if (!(caps = find_capability_list_from_greeting(ll, result, &freeme))) {
			imap_ll_freeline(result);
			if (freeme) imap_ll_freeline(freeme);
			return imap_login_error;
		}
		goto logged_in;
	} else if (0 != strcmp(result->first->leaf, "OK")) {
		fprintf(stderr, "IMAP server unacceptable greeting\n");
		return imap_login_error;
	}

	if (!(caps = find_capability_list_from_greeting(ll, result, &freeme))) {
		imap_ll_freeline(result);
		if (freeme) imap_ll_freeline(freeme);
		return imap_login_error;
	}

#ifdef USE_OPENSSL
	if (have_capability(caps, "STARTTLS")) {
		switch (imap_ll_starttls(ll, sslctx, servername)) {
			case IMAP_LL_STARTTLS_FAILED_PROCEED:
				break;
			case IMAP_LL_STARTTLS_FAILED:
			case IMAP_LL_STARTTLS_FAILED_CERT:
				if (freeme) imap_ll_freeline(freeme);
				imap_ll_freeline(result);
				return imap_login_error;
			case IMAP_LL_STARTTLS_SUCCESS:
				if (freeme) imap_ll_freeline(freeme);
				freeme = NULL;
				imap_ll_freeline(result);
				result = ask_for_capabilities(ll);
				if (!(caps = find_capability_list_in_untagged_response(result))) {
					imap_ll_freeline(result);
					return imap_login_error;
				}
		}
	}
#endif

	if (!need_capability(caps, "LITERAL+")) {
		ret = imap_login_error;
		goto exit;
	}
	if ((!username) || (!password)) {
		fprintf(stderr, "IMAP login credentials are needed\n");
		ret = imap_login_denied;
		goto exit;
	}
	if (!need_capability(caps, "AUTH=PLAIN")) {
		ret = imap_login_error;
		goto exit;
	}
	if (freeme) imap_ll_freeline(freeme);
	imap_ll_freeline(result);
	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "LOGIN", (size_t)-1,
		TLTYPE_STRING, username, username_len,
		TLTYPE_STRING, password, password_len,
		TLTYPE_END
	);
	result = imap_ll_command(ll, cmd, 40);
	imap_ll_freeline(cmd);
	status = imap_ll_status(result);
	if (0 == strcmp(status, "OK")) {
		if (!(caps = find_capability_list_from_greeting(ll, result->last, &freeme))) {
			if (freeme) imap_ll_freeline(freeme);
			imap_ll_freeline(result);
			return imap_login_error;
		}
logged_in:
		ret = imap_login_ok;
		if (!need_capability(caps, "LITERAL+")) ret = imap_login_error;
exit:
		imap_ll_freeline(result);
		if (freeme) imap_ll_freeline(freeme);
		return ret;
	} else if (0 == strcmp(status, "NO")) {
		fprintf(stderr, "IMAP authentication failed\n");
		imap_ll_freeline(result);
		return imap_login_denied;
	}
	fprintf(stderr, "IMAP server did not understand login command\n");
	imap_ll_freeline(result);
	return imap_login_error;
}

const char *
imap_select(
	struct imap_ll *ll,
	const char *foldername, size_t foldername_len,
	int need_write, long *exists_p
) {
const char *command_name;
struct imap_ll_tokenlist *cmd, *result, *l, *l2;
char *uidvalidity = NULL;
long exists = 0;

	if (foldername_len == -1) foldername_len = strlen(foldername);
	if (
		(ll->selected_folder) &&
		((ll->selected_folder_namelen) == foldername_len) &&
		(0 == memcmp(ll->selected_folder, foldername, foldername_len)) &&
		((!need_write) || (ll->selected_folder_writable))
	) {
		if (exists_p) *exists_p = ll->selected_folder_exists;
		return ll->selected_folder_uidvalidity;
	}

	command_name = need_write ? "SELECT" : "EXAMINE";
	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, command_name, (size_t)-1,
		TLTYPE_STRING, foldername, foldername_len,
		TLTYPE_END
	);
	result = imap_ll_command(ll, cmd, 40);
	imap_ll_freeline(cmd);

	if (!result) {
		fprintf(stderr, "unable to %s folder \"%s\": no response from IMAP server\n", command_name, foldername);
		return NULL;
	}
	if (0 != strcmp(imap_ll_status(result), "OK")) {
		fprintf(stderr, "unable to %s folder \"%s\":\n", command_name, foldername);
		imap_ll_pprint(result, 0, stderr);
		imap_ll_freeline(result);
		return NULL;
	}

	for (l = result->first; l != result->last; l = l->next) {
		/* Check if this is UIDVALIDITY */
		l2 = l->first;
		if (!l2) continue;
		if (l2->type != TLTYPE_ATOM) continue;
		if (0 == strcmp(l2->leaf, "OK")) {
			l2 = l2->next;
			if (!l2) continue;
			if (l2->type != TLTYPE_SQLIST) continue;
			l2 = l2->first;
			if (!l2) continue;
			if (l2->type != TLTYPE_ATOM) continue;
			if (0 != strcmp(l2->leaf, "UIDVALIDITY")) continue;
			l2 = l2->next;
			if (!l2) continue;
			if (l2->type != TLTYPE_ATOM) continue;
			uidvalidity = l2->leaf;
		} else {
			if (!(l2->next)) continue;
			if ((l2->next->type) != TLTYPE_ATOM) continue;
			if (0 != strcmp(l2->next->leaf, "EXISTS")) continue;
			exists = atol(l2->leaf);
		}
	}
	if (!uidvalidity) {
		fprintf(stderr, "no uidvalidity in EXAMINE response for folder \"%s\":\n", foldername);
		imap_ll_pprint(result, 0, stderr);
		imap_ll_freeline(result);
		return NULL;
	}

	if (ll->selected_folder) free(ll->selected_folder);
	if (ll->selected_folder_uidvalidity) free(ll->selected_folder_uidvalidity);
	ll->selected_folder = malloc(foldername_len);
	if (ll->selected_folder) {
		memcpy(ll->selected_folder, foldername, foldername_len);
	}
	ll->selected_folder_namelen = foldername_len;
	ll->selected_folder_uidvalidity = strdup(uidvalidity);
	ll->selected_folder_writable = need_write;
	ll->selected_folder_exists = exists;
	if (exists_p) *exists_p = exists;
	imap_ll_freeline(result);
	return ll->selected_folder_uidvalidity;
}
