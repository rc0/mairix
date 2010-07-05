/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2006,2010
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **********************************************************************
 */

#ifndef NVP_H
#define NVP_H

struct nvp;
struct msg_src;
extern struct nvp *make_nvp(struct msg_src *, char *, const char *);
extern void free_nvp(struct nvp *);
extern void nvp_dump(struct nvp *nvp, FILE *out);
extern const char *nvp_major(struct nvp *n);
extern const char *nvp_minor(struct nvp *n);
extern const char *nvp_first(struct nvp *n);
extern const char *nvp_lookup(struct nvp *n, const char *name);
extern const char *nvp_lookupcase(struct nvp *n, const char *name);

#endif

