#include "server.h"
#include "db.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void set_error(struct response *r, const char *fmt, ...) { /* Formats an error response. */
    va_list ap;
    r->status = -1;
    va_start(ap, fmt);
    vsnprintf(r->text, sizeof(r->text), fmt, ap);
    va_end(ap);
}

void set_ok(struct response *r, const char *fmt, ...) { /* Formats a success response. */
    va_list ap;
    r->status = 0;
    va_start(ap, fmt);
    vsnprintf(r->text, sizeof(r->text), fmt, ap);
    va_end(ap);
}

void addf(char *buf, size_t cap, const char *fmt, ...) { /* Appends formatted text to a bounded buffer. */
    size_t n = strlen(buf);
    va_list ap;
    if (n >= cap - 1) return;
    va_start(ap, fmt);
    vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
}

void money(char *out, size_t cap, long long paise) { /* Formats paise as rupees. */
    snprintf(out, cap, "%lld.%02lld",
             (long long)(paise / 100), (long long)llabs(paise % 100));
}

int find_user_bank(struct bank *b, const char *username,
                          struct user **out, int *out_idx) { /* Finds a user in a loaded database. */
    for (int i = 0; i < b->h.num_users && i < MAX_USERS; i++) {
        if (b->users[i].id != 0 &&
            strcmp(b->users[i].username, username) == 0) {
            if (out) *out = &b->users[i];
            if (out_idx) *out_idx = i;
            return 0;
        }
    }
    return -1;
}

int find_id_bank(struct bank *b, int id, struct user **out, int *out_idx) { /* Finds a user id in a loaded database. */
    if (id <= 0 || id > MAX_USERS) return -1;
    if (b->users[id - 1].id != id) return -1;
    if (out) *out = &b->users[id - 1];
    if (out_idx) *out_idx = id - 1;
    return 0;
}

int add_txn(struct bank *b, int uid, int type, long long amount,
                   long long balance, const char *note) { /* Appends one transaction row. */
    struct transaction *t;
    if (b->h.next_txn > MAX_TXNS) return -1;
    t = &b->txns[b->h.next_txn - 1];
    memset(t, 0, sizeof(*t));
    t->id = b->h.next_txn++;
    t->user_id = uid;
    t->type = type;
    t->amount = amount;
    t->balance_after = balance;
    t->ts = time(NULL);
    snprintf(t->note, sizeof(t->note), "%s", note);
    b->h.num_txns++;
    return 0;
}

int add_feedback(struct bank *b, int uid, const char *message) { /* Appends one feedback row. */
    struct feedback *f;
    if (b->h.next_feedback > MAX_FEEDBACK) return -1;
    f = &b->feedback[b->h.next_feedback - 1];
    memset(f, 0, sizeof(*f));
    f->id = b->h.next_feedback++;
    f->user_id = uid;
    f->status = FEEDBACK_NEW;
    f->ts = time(NULL);
    snprintf(f->message, sizeof(f->message), "%s", message);
    b->h.num_feedback++;
    return 0;
}

int begin_write(struct bank *b) { /* Locks and loads the database for writing. */
    if (db_lock_all(1) < 0) return -1;
    if (db_load(b) < 0) { db_unlock_all(); return -1; }
    return 0;
}

int end_write(struct bank *b, int save) { /* Saves if requested and unlocks the database. */
    int rc = 0;
    if (save && db_save(b) < 0) rc = -1;
    db_unlock_all();
    return rc;
}
