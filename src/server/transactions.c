#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void list_history(int uid, char *out, size_t cap) { /* Formats one customer's transaction history. */
    struct header h;
    out[0] = '\0';
    if (db_read_header(&h) < 0) { snprintf(out, cap, "Could not read database"); return; }
    addf(out, cap, "%-4s %-10s %12s %14s  %s\n",
         "ID", "TYPE", "AMOUNT", "BALANCE", "TIME/NOTE");
    for (int i = 0; i < h.num_txns && i < MAX_TXNS; i++) {
        struct transaction t;
        char m[32], b[32], ts[32];
        struct tm tmv;
        if (db_read_txn(i, &t) < 0 || t.user_id != uid) continue;
        money(m, sizeof(m), t.amount);
        money(b, sizeof(b), t.balance_after);
        localtime_r(&t.ts, &tmv);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
        addf(out, cap, "%-4d %-10s %12s %14s  %s %s\n",
             t.id,
             t.type == TX_DEPOSIT ? "DEPOSIT" :
             t.type == TX_WITHDRAW ? "WITHDRAW" :
             t.type == TX_TRANSFER_OUT ? "TRANSFER" :
             t.type == TX_TRANSFER_IN ? "CREDIT" : "LOAN",
             m, b, ts, t.note);
    }
}

void do_transfer(int uid, struct request *r, struct response *resp) { /* Moves funds between two customers atomically. */
    struct bank b;
    struct user *src;
    struct user *dst;
    char m[32];
    char note[NOTE_LEN];
    if (r->amount <= 0) { set_error(resp, "Amount must be positive"); return; }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_id_bank(&b, uid, &src, NULL) < 0 || !src->active) { end_write(&b, 0); set_error(resp, "Account is not active"); return; }
    if (find_user_bank(&b, r->username, &dst, NULL) < 0) { end_write(&b, 0); set_error(resp, "Recipient not found"); return; }
    if (dst->id == src->id) { end_write(&b, 0); set_error(resp, "Cannot transfer to yourself"); return; }
    if (dst->role != ROLE_CUSTOMER || !dst->active) { end_write(&b, 0); set_error(resp, "Recipient is not an active customer"); return; }
    if (src->balance < r->amount) { end_write(&b, 0); set_error(resp, "Insufficient balance"); return; }
    src->balance -= r->amount;
    dst->balance += r->amount;
    snprintf(note, sizeof(note), "to %s", dst->name);
    if (add_txn(&b, src->id, TX_TRANSFER_OUT, r->amount,
                src->balance, note) < 0) {
        end_write(&b, 0);
        set_error(resp, "Transaction table is full");
        return;
    }
    snprintf(note, sizeof(note), "from %s", src->name);
    if (add_txn(&b, dst->id, TX_TRANSFER_IN, r->amount,
                dst->balance, note) < 0) {
        end_write(&b, 0);
        set_error(resp, "Transaction table is full");
        return;
    }
    money(m, sizeof(m), src->balance);
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save transfer"); return; }
    audit_log(uid, "transfer %lld paise to %s",
              (long long)r->amount, r->username);
    set_ok(resp, "Transfer successful. Balance: %s", m);
}

void do_history(int uid, struct response *resp) { /* Returns the caller's transaction history. */
    char buf[TEXT_MAX];
    list_history(uid, buf, sizeof(buf));
    set_ok(resp, "%s", buf);
}

void do_mini(int uid, struct response *resp) { /* Builds the customer mini statement. */
    struct header h;
    struct user u;
    long long deposits = 0, withdrawals = 0, transfers = 0;
    int loans = 0;
    char out[TEXT_MAX], m[32];
    char last[5][160];
    int nlast = 0;
    if (db_read_header(&h) < 0 || db_read_user(uid - 1, &u) < 0) { set_error(resp, "Could not read account"); return; }
    for (int i = 0; i < h.num_txns && i < MAX_TXNS; i++) {
        struct transaction t;
        if (db_read_txn(i, &t) < 0 || t.user_id != uid) continue;
        if (t.type == TX_DEPOSIT) deposits += t.amount;
        else if (t.type == TX_WITHDRAW)
            withdrawals += t.amount;
        else if (t.type == TX_TRANSFER_OUT)
            transfers += t.amount;
        {
            char mm[32], line[160];
            money(mm, sizeof(mm), t.amount);
            snprintf(line, sizeof(line), "  %-8s %12s  %s",
                     t.type == TX_DEPOSIT ? "DEPOSIT" :
                     t.type == TX_WITHDRAW ? "WITHDRAW" :
                     t.type == TX_TRANSFER_OUT ? "TRANSFER" :
                     t.type == TX_TRANSFER_IN ? "CREDIT" : "LOAN",
                     mm, t.note);
            if (nlast < 5) {
                snprintf(last[nlast], sizeof(last[nlast]), "%s", line);
                nlast++;
            } else {
                memmove(last[0], last[1], 4 * sizeof(last[0]));
                snprintf(last[4], sizeof(last[4]), "%s", line);
            }
        }
    }
    for (int i = 0; i < h.num_loans && i < MAX_LOANS; i++) {
        struct loan l;
        if (db_read_loan(i, &l) == 0 && l.customer_id == uid &&
            (l.status == LOAN_PENDING || l.status == LOAN_APPROVED))
            loans++;
    }
    money(m, sizeof(m), u.balance);
    snprintf(out, sizeof(out),
             "Account summary for %s\n"
             "Balance: %s\n"
             "Total deposits: ",
             u.name, m);
    money(m, sizeof(m), deposits);
    addf(out, sizeof(out), "%s\n", m);
    money(m, sizeof(m), withdrawals);
    addf(out, sizeof(out), "Total withdrawals: %s\n", m);
    money(m, sizeof(m), transfers);
    addf(out, sizeof(out), "Total transfers out: %s\n", m);
    addf(out, sizeof(out), "Active/approved loans: %d\n", loans);
    addf(out, sizeof(out), "Last transactions:\n");
    for (int i = 0; i < nlast; i++)
        addf(out, sizeof(out), "%s\n", last[i]);
    set_ok(resp, "%s", out);
}

void do_customer_tx(int uid, struct request *r, struct response *resp) { /* Returns an employee's customer passbook. */
    struct user u;
    struct header h;
    char out[TEXT_MAX], m[32], b[32];
    (void)uid;
    if (db_find_user(r->username, &u, NULL) < 0 || u.role != ROLE_CUSTOMER) { set_error(resp, "Customer not found"); return; }
    if (db_read_header(&h) < 0) { set_error(resp, "Could not read database"); return; }
    out[0] = '\0';
    addf(out, sizeof(out), "Transactions for %s:\n", u.username);
    for (int i = 0; i < h.num_txns && i < MAX_TXNS; i++) {
        struct transaction t;
        if (db_read_txn(i, &t) < 0 || t.user_id != u.id) continue;
        money(m, sizeof(m), t.amount);
        money(b, sizeof(b), t.balance_after);
        addf(out, sizeof(out), "%d: type %d, %s, balance %s, %s\n",
             t.id, t.type, m, b, t.note);
    }
    set_ok(resp, "%s", out);
}
