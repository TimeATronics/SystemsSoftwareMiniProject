#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void do_add_feedback(int uid, struct request *r, struct response *resp) { /* Stores one customer feedback entry. */
    struct bank b;
    if (r->text[0] == '\0') { set_error(resp, "Feedback message is empty"); return; }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (add_feedback(&b, uid, r->text) < 0) { end_write(&b, 0); set_error(resp, "Feedback table is full"); return; }
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save feedback"); return; }
    audit_log(uid, "feedback: %s", r->text);
    set_ok(resp, "Feedback recorded");
}

void do_my_feedback(int uid, struct response *resp) { /* Lists the caller's feedback and replies. */
    struct header h;
    char out[TEXT_MAX];
    if (db_read_header(&h) < 0) { set_error(resp, "Could not read database"); return; }
    out[0] = '\0';
    for (int i = 0; i < h.num_feedback && i < MAX_FEEDBACK; i++) {
        struct feedback f;
        if (db_read_feedback(i, &f) < 0 || f.user_id != uid) continue;
        addf(out, sizeof(out), "Feedback #%d: %s\n", f.id, f.message);
        if (f.reply[0]) addf(out, sizeof(out), "  Manager reply: %s\n", f.reply);
        else addf(out, sizeof(out), "  No reply yet.\n");
    }
    if (out[0] == '\0') snprintf(out, sizeof(out), "No feedback submitted.");
    set_ok(resp, "%s", out);
}

void do_list_feedback(int uid, struct response *resp) { /* Lists feedback for managers. */
    struct header h;
    char out[TEXT_MAX];
    (void)uid;
    if (db_read_header(&h) < 0) { set_error(resp, "Could not read database"); return; }
    out[0] = '\0';
    addf(out, sizeof(out), "Feedback:\n");
    for (int i = 0; i < h.num_feedback && i < MAX_FEEDBACK; i++) {
        struct feedback f;
        if (db_read_feedback(i, &f) < 0) continue;
        addf(out, sizeof(out), "#%d user %d [%s] %s\n",
             f.id, f.user_id, f.status == FEEDBACK_NEW ? "NEW" : "REVIEWED",
             f.message);
        if (f.reply[0]) addf(out, sizeof(out), "   reply: %s\n", f.reply);
    }
    set_ok(resp, "%s", out);
}

void do_review_feedback(int uid, struct request *r, struct response *resp) { /* Marks feedback reviewed and stores a reply. */
    struct bank b;
    struct feedback *f;
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (r->id <= 0 || r->id > MAX_FEEDBACK ||
        b.feedback[r->id - 1].id != r->id) {
        end_write(&b, 0);
        set_error(resp, "Feedback not found");
        return;
    }
    f = &b.feedback[r->id - 1];
    f->status = FEEDBACK_REVIEWED;
    snprintf(f->reply, sizeof(f->reply), "%s", r->text);
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save feedback reply"); return; }
    audit_log(uid, "reviewed feedback %d", f->id);
    set_ok(resp, "Feedback %d reviewed", f->id);
}
