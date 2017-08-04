#include <stdio.h>
#include <string.h>
#ifdef USE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif
#include "imapinterface.h"
#include "imap.h"
#include "mairix.h"

struct imap_ll *
imap_start(const char *pipe, const char *server, const char *username, const char *password)
{
#ifdef USE_OPENSSL
SSL_CTX *sslctx;
#endif
struct imap_ll *imapc;

#ifdef USE_OPENSSL
	SSL_load_error_strings();
	SSL_library_init();
	sslctx = SSL_CTX_new(SSLv23_client_method());
	if (!sslctx) {
		fprintf(stderr, "SSL_CTX_new failed\n");
		ERR_print_errors_fp(stderr);
		return NULL;
	}
	SSL_CTX_load_verify_locations(sslctx, NULL, "/etc/ssl/certs");
	SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, NULL);
#endif

	imapc = pipe ? imap_ll_pipe_connect(pipe) : imap_ll_connect(server, "143");
	if (!imapc) return NULL;

	switch (imap_login(
		imapc, username, -1, password, -1
#ifdef USE_OPENSSL
		, sslctx, server
#endif
	)) {
		case imap_login_error:
		case imap_login_denied:
			return NULL;
		case imap_login_ok:
			break;
	}

	return imapc;
}

static void
add_imap_message_to_list(const char *folder, const char *uidvalidity, const char *uid, struct msgpath_array *arr)
{
char *pseudopath;

	pseudopath = Malloc(strlen(folder) + strlen(uidvalidity) + strlen(uid) + 3);
	sprintf(pseudopath, "%s:%s:%s", uidvalidity, uid, folder);

	if (arr->n == arr->max) {
		arr->max += 1024;
		arr->paths = grow_array(struct msgpath,    arr->max, arr->paths);
	}
	arr->paths[arr->n].type = MTY_IMAP;
	arr->paths[arr->n].src.mpf.path = pseudopath;
	++arr->n;
}

static void
scan_folder(struct imap_ll *imapc, const char *folder, struct msgpath_array *msgs)
{
struct imap_ll_tokenlist *cmd, *result, *l, *l2, *l3;
const char *uidvalidity;
char *uid;
int deleted;
long exists;

	uidvalidity = imap_select(imapc, folder, -1, 0, &exists);
	if (!uidvalidity) return;
	if (exists < 1) return;

	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "UID", (size_t)-1,
		TLTYPE_ATOM, "FETCH", (size_t)-1,
		TLTYPE_ATOM, "1:*", (size_t)-1,
		TLTYPE_ATOM, "FLAGS", (size_t)-1,
		TLTYPE_END
	);
	result = imap_ll_command(imapc, cmd, 40);
	imap_ll_freeline(cmd);

	if (!result) {
		fprintf(stderr, "unable to FETCH in folder \"%s\": no response from IMAP server\n", folder);
		return;
	}
	if (0 != strcmp(imap_ll_status(result), "OK")) {
		fprintf(stderr, "unable to FETCH in folder \"%s\":\n", folder);
		imap_ll_pprint(result, 0, stderr);
		imap_ll_freeline(result);
		return;
	}

	for (l = result->first; l != result->last; l = l->next) {
		l2 = l->first;
		if (!l2) continue;
		if (l2->type != TLTYPE_ATOM) continue;	/* sequence */
		l2 = l2->next;
		if (l2->type != TLTYPE_ATOM) continue;
		if (0 != strcmp(l2->leaf, "FETCH")) continue;
		l2 = l2->next;
		if (l2->type != TLTYPE_LIST) continue;
		uid = NULL;
		deleted = 0;
		for (l2 = l2->first; l2 && (l2->next); l2 = l2->next) {
			if (l2->type != TLTYPE_ATOM) continue;
			if (0 == strcmp(l2->leaf, "UID")) {
				l2 = l2->next;
				if (l2->type != TLTYPE_ATOM) continue;
				uid = l2->leaf;
			} else if (0 == strcmp(l2->leaf, "FLAGS")) {
				l2 = l2->next;
				if (l2->type != TLTYPE_LIST) continue;
				for (l3 = l2->first; l3; l3 = l3->next) {
					if (l3->type != TLTYPE_ATOM) continue;
					if (0 == strcmp(l3->leaf, "\\Deleted")) {
						deleted = 1;
					}
				}
			}
		}
		if (deleted) continue;
		if (!uid) continue;
		add_imap_message_to_list(folder, uidvalidity, uid, msgs);
	}
	imap_ll_freeline(result);
}

void
build_imap_message_list(
	const char *folders, struct msgpath_array *msgs,
	struct globber_array *omit_globs, struct imap_ll *imapc
)
{
char **folderlist;
int n_folders;
int i;

	split_on_colons(folders, &n_folders, &folderlist);
	for (i = 0; i < n_folders; i++) {
		if (!is_globber_array_match(omit_globs, folderlist[i])) {
			scan_folder(imapc, folderlist[i], msgs);
		}
	}
}

int
imap_fetch_message_raw(
	const char *pseudopath, struct imap_ll *imapc,
	void (*callback)(const char *, size_t, void *), void *arg
)
{
struct imap_ll_tokenlist *cmd, *result, *l, *l2;
const char *uidvalidity, *uid, *folder, *p;
size_t uidvalidity_len;
size_t uid_len, raw_len, got_uid_len;
const char *actual_uidvalidity, *got_uid, *got_raw;

	/* first part is the uidvalidity */
	uidvalidity = pseudopath;
	p = strchr(pseudopath, ':');
	if (!p) return 0;
	uidvalidity_len = p-pseudopath;
	uid = p+1;

	/* second part is the uid */
	p = strchr(uid, ':');
	if (!p) return 0;
	uid_len = p-uid;
	folder = p+1;

	actual_uidvalidity = imap_select(imapc, folder, -1, 0, NULL);
	if (!actual_uidvalidity) return 0;

	if (
		(strlen(actual_uidvalidity) != uidvalidity_len) ||
		(0 != memcmp(uidvalidity, actual_uidvalidity, uidvalidity_len))
	) {
		fprintf(stderr, "IMAP message \"%s\" cannot be loaded because UIDVALIDITY changed\n", pseudopath);
		return 0;
	}

	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "UID", (size_t)-1,
		TLTYPE_ATOM, "FETCH", (size_t)-1,
		TLTYPE_ATOM, uid, uid_len,
		TLTYPE_ATOM, "BODY.PEEK[]", (size_t)-1,
		TLTYPE_END
	);
	result = imap_ll_command(imapc, cmd, 40);
	imap_ll_freeline(cmd);

	if (!result) {
		fprintf(stderr, "unable to FETCH message \"%s\": no response from IMAP server\n", pseudopath);
		return 0;
	}
	if (0 != strcmp(imap_ll_status(result), "OK")) {
		fprintf(stderr, "unable to FETCH message \"%s\":\n", pseudopath);
		imap_ll_pprint(result, 0, stderr);
		imap_ll_freeline(result);
		return 0;
	}

	for (l = result->first; l != result->last; l = l->next) {
		l2 = l->first;
		if (!l2) continue;
		if (l2->type != TLTYPE_ATOM) continue;	/* sequence */
		l2 = l2->next;
		if (l2->type != TLTYPE_ATOM) continue;
		if (0 != strcmp(l2->leaf, "FETCH")) continue;
		l2 = l2->next;
		if (l2->type != TLTYPE_LIST) continue;
		got_uid = got_raw = NULL;
		for (l2 = l2->first; l2 && (l2->next); l2 = l2->next) {
			if (l2->type != TLTYPE_ATOM) continue;
			if (0 == strcmp(l2->leaf, "UID")) {
				l2 = l2->next;
				if (l2->type != TLTYPE_ATOM) continue;
				got_uid = l2->leaf;
				got_uid_len = l2->leaflen;
			} else if (0 == strcmp(l2->leaf, "BODY[]")) {
				l2 = l2->next;
				if (l2->type != TLTYPE_STRING) continue;
				got_raw = l2->leaf;
				raw_len = l2->leaflen;
			}
		}
		if ((!got_uid) || (!got_raw)) continue;
		if (got_uid_len != uid_len) continue;
		if (0 != memcmp(got_uid, uid, got_uid_len)) continue;
		callback(got_raw, raw_len, arg);
		imap_ll_freeline(result);
		return 1;
	}
	fprintf(stderr, "IMAP server did not return message \"%s\"\n", pseudopath);
	imap_ll_freeline(result);
	return 0;
}

struct make_rfc822_from_imap_s {
	const char *pseudopath;
	struct rfc822 *r;
};

static void
callback822(const char *data, size_t len, void *arg)
{
struct make_rfc822_from_imap_s *s = (struct make_rfc822_from_imap_s *)arg;
struct msg_src src;	/* assuming this is for error message presentation only! */

	src.type = MS_FILE;
	src.filename = (char *)(s->pseudopath);	/* XXX breaking "const" */
	s->r = data_to_rfc822(&src, (char *)data /* XXX breaking "const" */, len, NULL);
}

struct rfc822 *
make_rfc822_from_imap(const char *pseudopath, struct imap_ll *imapc)
{
struct make_rfc822_from_imap_s s;

	s.pseudopath = pseudopath;
	imap_fetch_message_raw(pseudopath, imapc, callback822, &s);
	return s.r;
}

void
imap_clear_folder(struct imap_ll *imapc, const char *folder)
{
struct imap_ll_tokenlist *cmd, *result;

	if (!imap_select(imapc, folder, -1, 1, NULL)) return;
	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "STORE", (size_t)-1,
		TLTYPE_ATOM, "1:*", (size_t)-1,
		TLTYPE_ATOM, "+FLAGS", (size_t)-1,
		TLTYPE_ATOM, "\\Deleted", (size_t)-1,
		TLTYPE_END
	);
	result = imap_ll_command(imapc, cmd, 10);
	imap_ll_freeline(cmd);
	imap_ll_freeline(result);
	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "EXPUNGE", (size_t)-1,
		TLTYPE_END
	);
	result = imap_ll_command(imapc, cmd, 10);
	imap_ll_freeline(cmd);
	imap_ll_freeline(result);
}

void
create_folder(struct imap_ll *imapc, const char *folder)
{
struct imap_ll_tokenlist *cmd, *result;

	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "CREATE", (size_t)-1,
		TLTYPE_STRING, folder, (size_t)-1,
		TLTYPE_END
	);
	result = imap_ll_command(imapc, cmd, 10);
	imap_ll_freeline(cmd);
	imap_ll_freeline(result);
}

void
imap_copy_message(struct imap_ll *imapc, const char *pseudopath, const char *to_folder)
{
struct imap_ll_tokenlist *cmd, *result;
const char *uidvalidity, *uid, *folder, *p;
size_t uidvalidity_len, uid_len;
const char *actual_uidvalidity;

	/* first part is the uidvalidity */
	uidvalidity = pseudopath;
	p = strchr(pseudopath, ':');
	if (!p) return;
	uidvalidity_len = p-pseudopath;
	uid = p+1;

	/* second part is the uid */
	p = strchr(uid, ':');
	if (!p) return;
	uid_len = p-uid;
	folder = p+1;

	actual_uidvalidity = imap_select(imapc, folder, -1, 0, NULL);
	if (!actual_uidvalidity) return;

	if (
		(strlen(actual_uidvalidity) != uidvalidity_len) ||
		(0 != memcmp(uidvalidity, actual_uidvalidity, uidvalidity_len))
	) {
		return;
	}

	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "UID", (size_t)-1,
		TLTYPE_ATOM, "COPY", (size_t)-1,
		TLTYPE_ATOM, uid, uid_len,
		TLTYPE_STRING, to_folder, (size_t)-1,
		TLTYPE_END
	);
	result = imap_ll_command(imapc, cmd, 10);

	if (imap_ll_is_trycreate(result)) {
		imap_ll_freeline(result);
		create_folder(imapc, to_folder);
		result = imap_ll_command(imapc, cmd, 10);
	}
	imap_ll_freeline(cmd);

	if (0 != strcmp(imap_ll_status(result), "OK")) {
		fprintf(stderr, "unable to COPY message to folder \"%s\":\n", to_folder);
		imap_ll_pprint(result, 0, stderr);
	}
	imap_ll_freeline(result);
}

void
imap_append_new_message(
	struct imap_ll *imapc, const char *folder,
	const unsigned char *data, size_t len,
	int seen, int answered, int flagged
)
{
struct imap_ll_tokenlist *flags, *cmd, *result;

	if (!imap_select(imapc, folder, -1, 1, NULL)) return;
	flags = imap_ll_build(TLTYPE_LIST, TLTYPE_END);
	if (seen) {
		imap_ll_append(flags, imap_ll_build(
			TLTYPE_ATOM, "\\Seen", (size_t)-1, TLTYPE_END
		));
	}
	if (answered) {
		imap_ll_append(flags, imap_ll_build(
			TLTYPE_ATOM, "\\Answered", (size_t)-1, TLTYPE_END
		));
	}
	if (flagged) {
		imap_ll_append(flags, imap_ll_build(
			TLTYPE_ATOM, "\\Flagged", (size_t)-1, TLTYPE_END
		));
	}
	cmd = imap_ll_build(
		TLTYPE_TAGGED,
		TLTYPE_ATOM, "APPEND", (size_t)-1,
		TLTYPE_STRING, folder, (size_t)-1,
		TLTYPE_SUB, flags,
		TLTYPE_STRING, data, len,
		TLTYPE_END
	);
	result = imap_ll_command(imapc, cmd, 10);

	if (imap_ll_is_trycreate(result)) {
		imap_ll_freeline(result);
		create_folder(imapc, folder);
		result = imap_ll_command(imapc, cmd, 10);
	}
	imap_ll_freeline(cmd);

	if (0 != strcmp(imap_ll_status(result), "OK")) {
		fprintf(stderr, "unable to APPEND message to folder \"%s\":\n", folder);
		imap_ll_pprint(result, 0, stderr);
	}
	imap_ll_freeline(result);
}
