#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <string.h>
#include <time.h>

void do_login(struct request *r, struct response *resp, int *uid, pid_t pid) { /* Verifies credentials and adds a session. */
    struct user u;
    if (db_find_user(r->username, &u, NULL) < 0) { set_error(resp, "Invalid username or password"); return; }
    if (!u.active) { set_error(resp, "Account is deactivated"); return; }
    if (u.pass_hash != password_hash(r->password, u.salt)) { set_error(resp, "Invalid username or password"); return; }
    if (session_add(u.id, pid) != 0) { set_error(resp, "User is already logged in"); return; }
    *uid = u.id;
    resp->user_id = u.id;
    resp->role = u.role;
    set_ok(resp, "Login successful. User ID: %d", u.id);
    audit_log(u.id, "login");
}

void do_logout(int uid, struct response *resp) { /* Removes the caller's session. */
    session_remove_user(uid);
    audit_log(uid, "logout");
    set_ok(resp, "Logged out");
}

void do_change_password(int uid, struct request *r, struct response *resp) { /* Verifies and replaces a password. */
    struct bank b;
    struct user *u;
    unsigned salt;
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (find_id_bank(&b, uid, &u, NULL) < 0) { end_write(&b, 0); set_error(resp, "User not found"); return; }
    if (u->pass_hash != password_hash(r->password, u->salt)) { end_write(&b, 0); set_error(resp, "Current password is wrong"); return; }
    salt = make_salt();
    u->salt = salt;
    u->pass_hash = password_hash(r->text, salt);
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save password"); return; }
    audit_log(uid, "changed password");
    set_ok(resp, "Password changed");
}
