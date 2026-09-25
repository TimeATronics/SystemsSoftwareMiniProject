#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void do_add_customer(int uid, struct request *r, struct response *resp) { /* Creates a customer with an opening balance. */
    struct bank b;
    struct user *u;
    unsigned salt;
    char m[32];
    if (r->username[0] == '\0' || r->password[0] == '\0') { set_error(resp, "Username and password are required"); return; }
    if (r->amount < 0) { set_error(resp, "Opening balance cannot be negative"); return; }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_user_bank(&b, r->username, NULL, NULL) == 0) { end_write(&b, 0); set_error(resp, "Username already exists"); return; }
    if (b.h.next_user > MAX_USERS) { end_write(&b, 0); set_error(resp, "User table is full"); return; }
    u = &b.users[b.h.next_user - 1];
    memset(u, 0, sizeof(*u));
    u->id = b.h.next_user++;
    snprintf(u->username, sizeof(u->username), "%s", r->username);
    snprintf(u->name, sizeof(u->name), "%s", r->name);
    salt = make_salt();
    u->salt = salt;
    u->pass_hash = password_hash(r->password, salt);
    u->role = ROLE_CUSTOMER;
    u->active = 1;
    u->balance = r->amount;
    u->created_at = time(NULL);
    b.h.num_users++;
    if (r->amount > 0) add_txn(&b, u->id, TX_DEPOSIT, r->amount, r->amount, "opening deposit");
    money(m, sizeof(m), u->balance);
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not create customer"); return; }
    audit_log(uid, "created customer %s", r->username);
    set_ok(resp, "Customer created. ID: %d, balance: %s", u->id, m);
}

void do_modify_customer(int uid, struct request *r, struct response *resp) { /* Changes a customer's details. */
    struct bank b;
    struct user *u;
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_user_bank(&b, r->username, &u, NULL) < 0 ||
        u->role != ROLE_CUSTOMER) {
        end_write(&b, 0);
        set_error(resp, "Customer not found");
        return;
    }
    if (r->name[0]) snprintf(u->name, sizeof(u->name), "%s", r->name);
    if (r->password[0]) { u->salt = make_salt(); u->pass_hash = password_hash(r->password, u->salt); }
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save details"); return; }
    audit_log(uid, "modified customer %s", r->username);
    set_ok(resp, "Customer details updated");
}
