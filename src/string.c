/* string.c - minimal freestanding libc stubs.
 *
 * -nostdlib means there is no libc, but the compiler still silently
 * emits calls to memset/memmove/memcpy for things like struct
 * returns, compound-literal zero-init, and array copies -- you don't
 * write these calls yourself, the compiler inserts them. Without
 * these, ANY function returning a struct by value (e.g. every
 * recipe's LUCID_OK/LUCID_FAIL) fails to link.
 *
 * Fixed core: every recipe implicitly depends on these existing, so
 * this isn't a recipe itself -- there's no meaningful "codename"
 * tradeoff for memset, just one correct implementation.
 */
#include "stddef.h"

void *memset(void *dst, int val, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (unsigned char)val;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0) return dst;

    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
