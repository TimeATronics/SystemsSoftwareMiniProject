#include "handlers.h"
#include "db.h"
#include "ipc.h"
#include "server.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static long long loan_interest_amount(long long principal, int months,
                                    int rate_bps, int type) { /* Computes simple or compound interest. */
    if (type == LOAN_SIMPLE)
        return (principal * rate_bps * months) / (10000 * 12);
    long long balance = principal;
    for (int i = 0; i < months; i++) {
        long long part = (balance * rate_bps) / (10000 * 12);
        balance += part;
    }
    return balance - principal;
}

void do_apply_loan(int uid, struct request *r, struct response *resp) { /* Creates a pending loan with computed interest. */
    struct bank b;
    struct loan *l;
    char principal[32], rate[32], interest[32], total[32];
    long long extra;
    if (r->amount <= 0 || r->months <= 0 || r->months > 360 ||
        r->amount > MAX_LOAN_PAISE) {
        set_error(resp, "Invalid loan amount or term");
        return;
    }
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (b.h.next_loan > MAX_LOANS) { end_write(&b, 0); set_error(resp, "Loan table is full"); return; }
    l = &b.loans[b.h.next_loan - 1];
    memset(l, 0, sizeof(*l));
    l->id = b.h.next_loan++;
    l->customer_id = uid;
    l->amount = r->amount;
    l->months = r->months;
    l->interest_type = b.h.loan_interest_type;
    l->rate_bps = b.h.loan_rate_bps;
    extra = loan_interest_amount(l->amount, l->months,
                                 l->rate_bps, l->interest_type);
    l->interest_amount = extra;
    l->total_payable = l->amount + extra;
    l->status = LOAN_PENDING;
    l->applied_at = time(NULL);
    {
        size_t plen = strlen(r->text);
        if (plen >= sizeof(l->purpose)) plen = sizeof(l->purpose) - 1;
        memcpy(l->purpose, r->text, plen);
        l->purpose[plen] = '\0';
    }
    b.h.num_loans++;
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save loan"); return; }
    money(principal, sizeof(principal), l->amount);
    money(rate, sizeof(rate), l->rate_bps);
    money(interest, sizeof(interest), l->interest_amount);
    money(total, sizeof(total), l->total_payable);
    audit_log(uid, "loan application %d", l->id);
    set_ok(resp, "Loan %d submitted. Principal %s, rate %s%% %s, interest %s, total payable %s",
           l->id, principal, rate,
           l->interest_type == LOAN_SIMPLE ? "simple" : "compound",
           interest, total);
}

void do_assigned_loans(int uid, struct response *resp) { /* Lists loans assigned to the employee. */
    struct header h;
    char out[TEXT_MAX], m[32], r[32], i[32], t[32];
    if (db_read_header(&h) < 0) { set_error(resp, "Could not read database"); return; }
    out[0] = '\0';
    addf(out, sizeof(out), "Assigned loans:\n");
    for (int j = 0; j < h.num_loans && j < MAX_LOANS; j++) {
        struct loan l;
        if (db_read_loan(j, &l) < 0 || l.employee_id != uid) continue;
        money(m, sizeof(m), l.amount);
        money(r, sizeof(r), l.rate_bps);
        money(i, sizeof(i), l.interest_amount);
        money(t, sizeof(t), l.total_payable);
        addf(out, sizeof(out),
             "Loan %d: customer %d, principal %s, %d months, %s%% %s, interest %s, total %s, status %d, %s\n",
             l.id, l.customer_id, m, l.months, r,
             l.interest_type == LOAN_SIMPLE ? "simple" : "compound",
             i, t, l.status, l.purpose);
    }
    set_ok(resp, "%s", out);
}

void do_process_loan(int uid, struct request *r, struct response *resp) { /* Approves or rejects an assigned loan. */
    struct bank b;
    struct loan *l;
    struct user *customer;
    char m[32], t[32];
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (r->id <= 0 || r->id > MAX_LOANS ||
        b.loans[r->id - 1].id != r->id) {
        end_write(&b, 0);
        set_error(resp, "Loan not found");
        return;
    }
    l = &b.loans[r->id - 1];
    if (l->employee_id != uid) { end_write(&b, 0); set_error(resp, "This loan is not assigned to you"); return; }
    if (l->status != LOAN_PENDING) { end_write(&b, 0); set_error(resp, "Loan is already processed"); return; }
    l->decided_at = time(NULL);
    if (r->decision == LOAN_APPROVED) {
        if (find_id_bank(&b, l->customer_id, &customer, NULL) < 0 ||
            !customer->active) {
            end_write(&b, 0);
            set_error(resp, "Customer account is not active");
            return;
        }
        customer->balance += l->amount;
        if (add_txn(&b, customer->id, TX_LOAN, l->amount,
                    customer->balance, "loan approved") < 0) {
            end_write(&b, 0);
            set_error(resp, "Transaction table is full");
            return;
        }
        l->status = LOAN_APPROVED;
    } else {
        l->status = LOAN_REJECTED;
    }
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not save loan decision"); return; }
    money(m, sizeof(m), l->amount);
    money(t, sizeof(t), l->total_payable);
    audit_log(uid, "loan %d %s", l->id,
              l->status == LOAN_APPROVED ? "approved" : "rejected");
    if (l->status == LOAN_APPROVED)
        set_ok(resp, "Loan %d approved, principal %s, total payable %s",
               l->id, m, t);
    else
        set_ok(resp, "Loan %d rejected", l->id);
}

void do_pending_loans(int uid, struct response *resp) { /* Lists pending loan applications. */
    struct header h;
    char out[TEXT_MAX], m[32], r[32], i[32], t[32];
    (void)uid;
    if (db_read_header(&h) < 0) { set_error(resp, "Could not read database"); return; }
    out[0] = '\0';
    addf(out, sizeof(out), "Pending loans:\n");
    for (int j = 0; j < h.num_loans && j < MAX_LOANS; j++) {
        struct loan l;
        if (db_read_loan(j, &l) < 0 || l.status != LOAN_PENDING) continue;
        money(m, sizeof(m), l.amount);
        money(r, sizeof(r), l.rate_bps);
        money(i, sizeof(i), l.interest_amount);
        money(t, sizeof(t), l.total_payable);
        addf(out, sizeof(out),
             "Loan %d: customer %d, principal %s, %d months, %s%% %s, interest %s, total %s, %s\n",
             l.id, l.customer_id, m, l.months, r,
             l.interest_type == LOAN_SIMPLE ? "simple" : "compound",
             i, t, l.purpose);
    }
    set_ok(resp, "%s", out);
}

void do_assign_loan(int uid, struct request *r, struct response *resp) { /* Assigns a pending loan to an employee. */
    struct bank b;
    struct loan *l;
    struct user *emp;
    if (begin_write(&b) < 0) { set_error(resp, "Database busy"); return; }
    if (r->id <= 0 || r->id > MAX_LOANS || b.loans[r->id - 1].id != r->id) { end_write(&b, 0); set_error(resp, "Loan not found"); return; }
    l = &b.loans[r->id - 1];
    if (l->status != LOAN_PENDING) { end_write(&b, 0); set_error(resp, "Only pending loans can be assigned"); return; }
    if (find_user_bank(&b, r->username, &emp, NULL) < 0 ||
        emp->role != ROLE_EMPLOYEE || !emp->active) {
        end_write(&b, 0);
        set_error(resp, "Employee not found or inactive");
        return;
    }
    l->employee_id = emp->id;
    if (end_write(&b, 1) < 0) { set_error(resp, "Could not assign loan"); return; }
    audit_log(uid, "assigned loan %d to %s", l->id, r->username);
    set_ok(resp, "Loan %d assigned to %s", l->id, r->username);
}
