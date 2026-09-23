#ifndef HANDLERS_H
#define HANDLERS_H

#include "bank.h"

void do_login(struct request *r, struct response *resp, int *uid, pid_t pid);
void do_logout(int uid, struct response *resp);
void do_change_password(int uid, struct request *r, struct response *resp);

void do_balance(int uid, struct response *resp);
void do_deposit(int uid, struct request *r, struct response *resp);
void do_withdraw(int uid, struct request *r, struct response *resp);
void do_close_account(int uid, struct request *r, struct response *resp);
void do_set_active(int uid, struct request *r, struct response *resp);

void do_transfer(int uid, struct request *r, struct response *resp);
void do_history(int uid, struct response *resp);
void do_mini(int uid, struct response *resp);
void do_customer_tx(int uid, struct request *r, struct response *resp);

void do_add_customer(int uid, struct request *r, struct response *resp);
void do_modify_customer(int uid, struct request *r, struct response *resp);

void do_apply_loan(int uid, struct request *r, struct response *resp);
void do_assigned_loans(int uid, struct response *resp);
void do_process_loan(int uid, struct request *r, struct response *resp);
void do_pending_loans(int uid, struct response *resp);
void do_assign_loan(int uid, struct request *r, struct response *resp);

void do_add_feedback(int uid, struct request *r, struct response *resp);
void do_my_feedback(int uid, struct response *resp);
void do_list_feedback(int uid, struct response *resp);
void do_review_feedback(int uid, struct request *r, struct response *resp);

void do_add_employee(int uid, struct request *r, struct response *resp);
void do_modify_user(int uid, struct request *r, struct response *resp);
void do_set_role(int uid, struct request *r, struct response *resp);
void do_list_users(int uid, struct response *resp);
void do_view_log(int uid, struct response *resp);
void do_set_loan_rate(int uid, struct request *r, struct response *resp);

#endif
