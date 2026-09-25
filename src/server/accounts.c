#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void do_balance(int uid, struct response *resp) { /* Returns the caller's balance. */
    struct user u;
    char m[32];
    if (db_find_user_id(uid, &u, NULL) < 0) { set_error(resp, "User not found"); return; }
    money(m, sizeof(m), u.balance);
    set_ok(resp, "Balance: %s", m);
}

void do_deposit(int uid, struct request *r, struct response *resp) { /* Adds a deposit and transaction row. */
    struct bank b;
    struct user *u;
    char m[32];
    if (r->amount <= 0) { set_error(resp, "Amount must be positive"); return; }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_id_bank(&b, uid, &u, NULL) < 0 || !u->active) { end_write(&b, 0); set_error(resp, "Account is not active"); return; }
    u->balance += r->amount;
    if (add_txn(&b, uid, TX_DEPOSIT, r->amount, u->balance, "deposit") < 0) {
        end_write(&b, 0);
        set_error(resp, "Transaction table is full");
        return;
    }
    money(m, sizeof(m), u->balance);
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save deposit"); return; }
    audit_log(uid, "deposit %lld paise", (long long)r->amount);
    set_ok(resp, "Deposit successful. Balance: %s", m);
}

void do_withdraw(int uid, struct request *r, struct response *resp) { /* Checks and records a withdrawal. */
    struct bank b;
    struct user *u;
    char m[32];
    if (r->amount <= 0) { set_error(resp, "Amount must be positive"); return; }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_id_bank(&b, uid, &u, NULL) < 0 || !u->active) { end_write(&b, 0); set_error(resp, "Account is not active"); return; }
    if (u->balance < r->amount) { end_write(&b, 0); set_error(resp, "Insufficient balance"); return; }
    u->balance -= r->amount;
    if (add_txn(&b, uid, TX_WITHDRAW, r->amount, u->balance, "withdrawal") < 0) {
        end_write(&b, 0);
        set_error(resp, "Transaction table is full");
        return;
    }
    money(m, sizeof(m), u->balance);
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save withdrawal"); return; }
    audit_log(uid, "withdraw %lld paise", (long long)r->amount);
    set_ok(resp, "Withdrawal successful. Balance: %s", m);
}

void do_close_account(int uid, struct request *r, struct response *resp) { /* Closes an eligible customer account. */
    struct bank b;
    struct user *u;
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_user_bank(&b, r->username, &u, NULL) < 0 ||
        u->role != ROLE_CUSTOMER) {
        end_write(&b, 0);
        set_error(resp, "Customer not found");
        return;
    }
    if (u->balance != 0) { end_write(&b, 0); set_error(resp, "Balance must be zero before closing"); return; }
    for (int i = 0; i < b.h.num_loans && i < MAX_LOANS; i++) {
        if (b.loans[i].id && b.loans[i].customer_id == u->id &&
            (b.loans[i].status == LOAN_PENDING ||
             b.loans[i].status == LOAN_APPROVED)) {
            end_write(&b, 0);
            set_error(resp, "Customer has an outstanding loan");
            return;
        }
    }
    u->active = 0;
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not close account"); return; }
    session_remove_user(u->id);
    audit_log(uid, "closed account %s", r->username);
    set_ok(resp, "Account closed");
}

void do_set_active(int uid, struct request *r, struct response *resp) { /* Activates or deactivates a customer. */
    struct bank b;
    struct user *u;
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_user_bank(&b, r->username, &u, NULL) < 0 ||
        u->role != ROLE_CUSTOMER) {
        end_write(&b, 0);
        set_error(resp, "Customer not found");
        return;
    }
    u->active = r->active ? 1 : 0;
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save status"); return; }
    if (!u->active) session_remove_user(u->id);
    audit_log(uid, "%s account %s",
              u->active ? "activated" : "deactivated", r->username);
    set_ok(resp, "Account %s", u->active ? "activated" : "deactivated");
}
