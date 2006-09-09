#ifndef NVPTYPES_H
#define NVPTYPES_H

enum nvp_action {
  GOT_NAMEVALUE,
  GOT_NAME,
  GOT_MAJORMINOR,
  GOT_TERMINATOR,
  GOT_NOTHING
};

enum nvp_copier {
  COPY_TO_NAME,
  COPY_TO_MINOR,
  COPY_TO_VALUE,
  COPY_NOWHERE
};

#endif
