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

void imap_clear_folder(struct imap_ll *, const char *);
void imap_append_new_message(struct imap_ll *, const char *folder, const unsigned char *data, size_t len, int seen, int answered, int flagged);
void imap_copy_message(struct imap_ll *, const char *pseudopath, const char *to_folder);

#endif
