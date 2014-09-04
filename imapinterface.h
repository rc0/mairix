#ifndef __IMAPMSG_H__
#define __IMAPMSG_H__

#include "mairix.h"

struct imap_ll;

/* set pipe OR server, not both */
struct imap_ll *
imap_start(const char *pipe, const char *server, const char *username, const char *password);

void
build_imap_message_list(const char *folders, struct msgpath_array *msgs, struct globber_array *omit_globs, struct imap_ll *);

/* returns 1 on success, 0 otherwise */
int imap_fetch_message_raw(
	const char *pseudopath, struct imap_ll *imapc,
	/* on success, calls this callback */
	void (*callback)(const char *, size_t, void *), void *arg
	/* after the callback returns, the pointer to the message
	   data is no longer valid. */
);

struct rfc822 *
make_rfc822_from_imap(const char *pseudopath, struct imap_ll *);

#endif
