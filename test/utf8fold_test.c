#include "utf8fold.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect_fold(const char *name, char *actual, const char *expected)
{
  utf8_casefold_inplace(actual);
  if (strcmp(actual, expected) != 0) {
    fprintf(stderr, "%s: unexpected fold\n", name);
    ++failures;
  }
}

static void expect_bytes(const char *name, unsigned char *actual,
                         const unsigned char *expected, size_t size)
{
  utf8_casefold_inplace((char *)actual);
  if (memcmp(actual, expected, size) != 0) {
    fprintf(stderr, "%s: invalid UTF-8 was not preserved\n", name);
    ++failures;
  }
}

int main(void)
{
  char ascii[] = "MAIRIX";
  char latin[] = "CAF\xc3\x89";
  char greek[] = "\xce\x9f\xce\x94\xce\x9f\xce\xa3 \xcf\x82";
  char cyrillic[] = "\xd0\x9f\xd0\xa0\xd0\x98\xd0\x92\xd0\x95\xd0\xa2";
  char deseret[] = "\xf0\x90\x90\x80";
  char sharp_s[] = "\xc3\x9f";
  unsigned char overlong[] = {0xe0, 0x80, 0x80, 0};
  unsigned char overlong_expected[] = {0xe0, 0x80, 0x80, 0};
  unsigned char surrogate[] = {0xed, 0xa0, 0x80, 0};
  unsigned char surrogate_expected[] = {0xed, 0xa0, 0x80, 0};
  unsigned char too_high[] = {0xf4, 0x90, 0x80, 0x80, 0};
  unsigned char too_high_expected[] = {0xf4, 0x90, 0x80, 0x80, 0};
  unsigned char truncated[] = {0xe2, 0x82, 0};
  unsigned char truncated_expected[] = {0xe2, 0x82, 0};

  expect_fold("ASCII", ascii, "mairix");
  expect_fold("Latin", latin, "caf\xc3\xa9");
  expect_fold("Greek", greek, "\xce\xbf\xce\xb4\xce\xbf\xcf\x83 \xcf\x83");
  expect_fold("Cyrillic", cyrillic,
              "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82");
  expect_fold("Deseret", deseret, "\xf0\x90\x90\xa8");
  expect_fold("length-changing", sharp_s, "\xc3\x9f");
  expect_bytes("overlong", overlong, overlong_expected, sizeof(overlong));
  expect_bytes("surrogate", surrogate, surrogate_expected, sizeof(surrogate));
  expect_bytes("too high", too_high, too_high_expected, sizeof(too_high));
  expect_bytes("truncated", truncated, truncated_expected, sizeof(truncated));
  utf8_casefold_inplace(NULL);
  if (failures) return 1;
  puts("utf8fold tests passed");
  return 0;
}
