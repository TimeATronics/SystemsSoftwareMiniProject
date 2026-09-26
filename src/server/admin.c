#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

void do_add_employee(int uid, struct request *r, struct response *resp) { /* Creates an employee or manager account. */
    struct bank b;
    struct user *u;
    unsigned salt;
    if (r->role != ROLE_EMPLOYEE && r->role != ROLE_MANAGER) { set_error(resp, "Role must be employee or manager"); return; }
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
    u->role = r->role;
    u->active = 1;
    u->created_at = time(NULL);
    b.h.num_users++;
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not create employee"); return; }
    audit_log(uid, "created %s %s",
              r->role == ROLE_EMPLOYEE ? "employee" : "manager", r->username);
    set_ok(resp, "User created. ID: %d", u->id);
}

void do_modify_user(int uid, struct request *r, struct response *resp) { /* Changes a user's name or password. */
    struct bank b;
    struct user *u;
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_user_bank(&b, r->username, &u, NULL) < 0) { end_write(&b, 0); set_error(resp, "User not found"); return; }
    if (r->name[0]) snprintf(u->name, sizeof(u->name), "%s", r->name);
    if (r->password[0]) { u->salt = make_salt(); u->pass_hash = password_hash(r->password, u->salt); }
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save details"); return; }
    audit_log(uid, "modified user %s", r->username);
    set_ok(resp, "User details updated");
}

void do_set_role(int uid, struct request *r, struct response *resp) { /* Changes a user's role. */
    struct bank b;
    struct user *u;
    if (r->role < ROLE_CUSTOMER || r->role > ROLE_ADMIN) { set_error(resp, "Role must be 1, 2, 3 or 4"); return; }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_user_bank(&b, r->username, &u, NULL) < 0) { end_write(&b, 0); set_error(resp, "User not found"); return; }
    if (u->id == uid) { end_write(&b, 0); set_error(resp, "You cannot change your own role"); return; }
    u->role = r->role;
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save role"); return; }
    session_remove_user(u->id);
    audit_log(uid, "changed role of %s", r->username);
    set_ok(resp, "Role updated");
}

void do_list_users(int uid, struct response *resp) { /* Lists all user records. */
    struct header h;
    char out[TEXT_MAX], m[32];
    (void)uid;
    if (db_read_header(&h) < 0) { set_error(resp, "Could not read database"); return; }
    out[0] = '\0';
    addf(out, sizeof(out), "Users:\n");
    for (int i = 0; i < h.num_users && i < MAX_USERS; i++) {
        struct user u;
        if (db_read_user(i, &u) < 0) continue;
        if (u.role == ROLE_CUSTOMER) money(m, sizeof(m), u.balance);
        else snprintf(m, sizeof(m), "-");
        addf(out, sizeof(out), "ID %d  %-12s  %-20s  role %d  %s  balance %s\n",
             u.id, u.username, u.name, u.role,
             u.active ? "active" : "inactive", m);
    }
    set_ok(resp, "%s", out);
}

void do_set_loan_rate(int uid, struct request *r, struct response *resp) { /* Stores the bank-wide loan interest policy. */
    struct bank b;
    char m[32];
    if (r->amount < 0 || r->amount > 5000) {
        set_error(resp, "Rate must be between 0.00 and 50.00 percent");
        return;
    }
    if (r->interest != LOAN_SIMPLE && r->interest != LOAN_COMPOUND) {
        set_error(resp, "Interest type must be 1 (simple) or 2 (compound)");
        return;
    }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    b.h.loan_rate_bps = (int)r->amount;
    b.h.loan_interest_type = r->interest;
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save loan rate"); return; }
    money(m, sizeof(m), r->amount);
    audit_log(uid, "set loan rate %s%% type %d", m, r->interest);
    set_ok(resp, "Loan interest set to %s%% per year, %s",
           m, r->interest == LOAN_SIMPLE ? "simple" : "compound");
}

void do_view_log(int uid, struct response *resp) { /* Reads the recent audit log. */
    int fd;
    struct stat st;
    off_t start;
    char buf[5000];
    ssize_t n;
    (void)uid;
    fd = open(AUDIT_FILE, O_RDONLY);
    if (fd < 0) { set_ok(resp, "No activity yet."); return; }
    if (fstat(fd, &st) < 0 || st.st_size == 0) { close(fd); set_ok(resp, "No activity yet."); return; }
    start = st.st_size > 4000 ? st.st_size - 4000 : 0;
    lseek(fd, start, SEEK_SET);
    n = read(fd, buf, 4096);
    close(fd);
    if (n < 0) { set_error(resp, "Could not read log"); return; }
    buf[n] = '\0';
    set_ok(resp, "%s", buf);
}
