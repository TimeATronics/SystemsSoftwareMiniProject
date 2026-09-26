#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <string.h>

void handle_request(struct request *r, struct response *resp,
                           int *uid, pid_t pid) { /* Checks the session and role, then dispatches. */
    struct user me;
    resp->status = 0;
    resp->text[0] = '\0';
    resp->user_id = *uid;
    resp->role = 0;
    if (r->op == OP_LOGIN) { do_login(r, resp, uid, pid); return; }
    if (*uid <= 0) { set_error(resp, "Please log in first"); return; }
    if (!session_has(*uid, pid) ||
        db_find_user_id(*uid, &me, NULL) < 0 || !me.active) {
        session_remove_user(*uid);
        *uid = 0;
        set_error(resp, "Account is inactive. Please log in again.");
        return;
    }
    resp->user_id = me.id;
    resp->role = me.role;
    if (r->op == OP_LOGOUT) { do_logout(*uid, resp); *uid = 0; return; }
    if (r->op == OP_CHANGE_PW) { do_change_password(*uid, r, resp); return; }
    switch (r->op) {
    case OP_BALANCE:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_balance(*uid, resp);
        break;
    case OP_DEPOSIT:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_deposit(*uid, r, resp);
        break;
    case OP_WITHDRAW:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_withdraw(*uid, r, resp);
        break;
    case OP_TRANSFER:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_transfer(*uid, r, resp);
        break;
    case OP_HISTORY:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_history(*uid, resp);
        break;
    case OP_MINI:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_mini(*uid, resp);
        break;
    case OP_ADD_FEEDBACK:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_add_feedback(*uid, r, resp);
        break;
    case OP_MY_FEEDBACK:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_my_feedback(*uid, resp);
        break;
    case OP_APPLY_LOAN:
        if (me.role != ROLE_CUSTOMER) set_error(resp, "Customer operation");
        else do_apply_loan(*uid, r, resp);
        break;
    case OP_ADD_CUSTOMER:
        if (me.role != ROLE_EMPLOYEE) set_error(resp, "Employee operation");
        else do_add_customer(*uid, r, resp);
        break;
    case OP_MODIFY_CUSTOMER:
        if (me.role != ROLE_EMPLOYEE) set_error(resp, "Employee operation");
        else do_modify_customer(*uid, r, resp);
        break;
    case OP_CLOSE_ACCOUNT:
        if (me.role != ROLE_EMPLOYEE) set_error(resp, "Employee operation");
        else do_close_account(*uid, r, resp);
        break;
    case OP_ASSIGNED_LOANS:
        if (me.role != ROLE_EMPLOYEE) set_error(resp, "Employee operation");
        else do_assigned_loans(*uid, resp);
        break;
    case OP_PROCESS_LOAN:
        if (me.role != ROLE_EMPLOYEE) set_error(resp, "Employee operation");
        else do_process_loan(*uid, r, resp);
        break;
    case OP_CUSTOMER_TX:
        if (me.role != ROLE_EMPLOYEE) set_error(resp, "Employee operation");
        else do_customer_tx(*uid, r, resp);
        break;
    case OP_SET_ACTIVE:
        if (me.role != ROLE_MANAGER) set_error(resp, "Manager operation");
        else do_set_active(*uid, r, resp);
        break;
    case OP_PENDING_LOANS:
        if (me.role != ROLE_MANAGER) set_error(resp, "Manager operation");
        else do_pending_loans(*uid, resp);
        break;
    case OP_ASSIGN_LOAN:
        if (me.role != ROLE_MANAGER) set_error(resp, "Manager operation");
        else do_assign_loan(*uid, r, resp);
        break;
    case OP_LIST_FEEDBACK:
        if (me.role != ROLE_MANAGER) set_error(resp, "Manager operation");
        else do_list_feedback(*uid, resp);
        break;
    case OP_REVIEW_FEEDBACK:
        if (me.role != ROLE_MANAGER) set_error(resp, "Manager operation");
        else do_review_feedback(*uid, r, resp);
        break;
    case OP_ADD_EMPLOYEE:
        if (me.role != ROLE_ADMIN) set_error(resp, "Administrator operation");
        else do_add_employee(*uid, r, resp);
        break;
    case OP_MODIFY_USER:
        if (me.role != ROLE_ADMIN) set_error(resp, "Administrator operation");
        else do_modify_user(*uid, r, resp);
        break;
    case OP_SET_ROLE:
        if (me.role != ROLE_ADMIN) set_error(resp, "Administrator operation");
        else do_set_role(*uid, r, resp);
        break;
    case OP_LIST_USERS:
        if (me.role != ROLE_ADMIN) set_error(resp, "Administrator operation");
        else do_list_users(*uid, resp);
        break;
    case OP_VIEW_LOG:
        if (me.role != ROLE_ADMIN) set_error(resp, "Administrator operation");
        else do_view_log(*uid, resp);
        break;
    case OP_SET_LOAN_RATE:
        if (me.role != ROLE_ADMIN) set_error(resp, "Administrator operation");
        else do_set_loan_rate(*uid, r, resp);
        break;
    default:
        set_error(resp, "Unknown operation");
        break;
    }
}
