#ifndef __IMAPLL_H__
#define __IMAPLL_H__

#ifdef USE_OPENSSL
#include <openssl/ssl.h>
#endif
#include <poll.h>

enum imap_ll_tltype {
	TLTYPE_UNTAGGED = 1,
	TLTYPE_TAGGED = 2,
	TLTYPE_LIST = 3,
	TLTYPE_SQLIST = 4,
	TLTYPE_ATOM = 5,
	TLTYPE_STRING = 6,
	TLTYPE_CONTINUATION = 7,
	/* the following only for imap_ll_build */
	TLTYPE_END = 100,
	TLTYPE_POP = 101,
	TLTYPE_SUB = 102
};

struct imap_ll_tokenlist {
	enum imap_ll_tltype type;
	char *leaf;
	size_t leaflen;
	struct imap_ll_tokenlist *parent;
	struct imap_ll_tokenlist *next;
	/* children */
	struct imap_ll_tokenlist *first;
	struct imap_ll_tokenlist *last;
};

struct imap_ll *imap_ll_connect(const char *host, const char *port);
struct imap_ll *imap_ll_pipe_connect(const char *command);
void imap_ll_timeout(struct imap_ll *, int seconds);
struct imap_ll_tokenlist *imap_ll_waitline(struct imap_ll *);
void imap_ll_freeline(struct imap_ll_tokenlist *);
struct imap_ll_tokenlist *imap_ll_build(enum imap_ll_tltype maintype, ...);
void imap_ll_append(struct imap_ll_tokenlist *, struct imap_ll_tokenlist *);
void imap_ll_pprint(struct imap_ll_tokenlist *, int indent, FILE *);
struct imap_ll_tokenlist *imap_ll_command(struct imap_ll *, struct imap_ll_tokenlist *, int timeout);
const char *imap_ll_status(struct imap_ll_tokenlist *);
int imap_ll_is_trycreate(struct imap_ll_tokenlist *);

#ifdef USE_OPENSSL
enum imap_ll_starttls_result {
	IMAP_LL_STARTTLS_FAILED_PROCEED,	/* STARTTLS failed but session still OK */
	IMAP_LL_STARTTLS_FAILED,	/* session must be closed */
	IMAP_LL_STARTTLS_FAILED_CERT,	/* certificate problem (session must be closed) */
	IMAP_LL_STARTTLS_SUCCESS	/* certificate problem (session must be closed) */
};
enum imap_ll_starttls_result imap_ll_starttls(struct imap_ll *, SSL_CTX *, const char *servername);
#endif

void imap_ll_logout(struct imap_ll *);

enum imap_login_result {
	imap_login_ok = 0,
	imap_login_denied = 1,
	imap_login_error = 2
};

enum imap_login_result
imap_login(
	struct imap_ll *,
	const char *username, size_t username_len,
	const char *password, size_t password_len
#ifdef USE_OPENSSL
	, SSL_CTX *, const char *servername
#endif
);

/* returns the uidvalidity of the opened folder if successful */
const char *imap_select(
	struct imap_ll *,
	const char *foldername, size_t foldername_len,
	int need_write,
	long *exists_p	/* optional, return number of messages which exist */
);

#endif
