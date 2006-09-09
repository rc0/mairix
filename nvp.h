#ifndef NVP_H
#define NVP_H

struct nvp;
extern struct nvp *make_nvp(char *);
extern void free_nvp(struct nvp *);
extern void nvp_dump(struct nvp *nvp, FILE *out);
extern const char *nvp_major(struct nvp *n);
extern const char *nvp_minor(struct nvp *n);
extern const char *nvp_first(struct nvp *n);
extern const char *nvp_lookup(struct nvp *n, const char *name);
extern const char *nvp_lookupcase(struct nvp *n, const char *name);

#endif

