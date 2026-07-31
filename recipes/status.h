#ifndef LUCID_STATUS_H
#define LUCID_STATUS_H

/* Shared by every recipe. Not owned by any one of them.
 * Every _pre/_init/_post in every recipe returns one of these. */

typedef struct {
    int ok;             /* 0 = success, nonzero = failure code, meaning is impl-defined */
    const char *msg;    /* short static string, human-readable, NULL on success */
} lucid_status;

#define LUCID_OK ((lucid_status){ .ok = 0, .msg = 0 })
#define LUCID_FAIL(code, m) ((lucid_status){ .ok = (code), .msg = (m) })

#endif
