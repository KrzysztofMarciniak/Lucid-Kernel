#ifndef LUCID_STATUS_H
#define LUCID_STATUS_H

/* Shared by every recipe. Not owned by any one of them.
 * Every _pre/_init/_post in every recipe returns one of these.
 *
 * Four fields, three audiences:
 *   ok    - the machine-checkable part. 0 = success, nonzero = failure.
 *   msg   - short, human-readable, ALWAYS set on failure, optional on
 *           success. What a person watching the boot log sees.
 *   debug - technical detail: register/port values, raw state. NULL
 *           if there's nothing more specific than msg. For someone
 *           debugging the kernel itself, not booting it.
 *   hint  - an actionable next step, e.g. "check BIOS PS/2 setting is
 *           enabled". NULL if there isn't an obvious one. This is the
 *           thing that turns a panic from "something broke" into
 *           "here's what to try".
 */

typedef struct {
    int ok;
    const char *msg;
    const char *debug;
    const char *hint;
} lucid_status;

/* Bare success, nothing to report. Existing call sites unaffected. */
#define LUCID_OK \
    ((lucid_status){ .ok = 0, .msg = 0, .debug = 0, .hint = 0 })

/* Success, but worth telling the boot log something specific. */
#define LUCID_OK_MSG(m) \
    ((lucid_status){ .ok = 0, .msg = (m), .debug = 0, .hint = 0 })

/* Failure, short message only -- existing call sites unaffected. */
#define LUCID_FAIL(code, m) \
    ((lucid_status){ .ok = (code), .msg = (m), .debug = 0, .hint = 0 })

/* Failure with full detail: what happened, what state caused it,
 * what to try next. */
#define LUCID_FAIL_EX(code, m, dbg, hnt) \
    ((lucid_status){ .ok = (code), .msg = (m), .debug = (dbg), .hint = (hnt) })

#endif
