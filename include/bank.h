#ifndef BANK_H
#define BANK_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#define DATA_DIR      "data"
#define BANK_FILE     DATA_DIR "/bank"
#define TMP_FILE      DATA_DIR "/newbank"
#define LOCK_FILE     DATA_DIR "/lock"
#define AUDIT_FILE    DATA_DIR "/audit"

#define MAX_USERS     200
#define MAX_LOANS     200
#define MAX_TXNS      2000
#define MAX_FEEDBACK  200
#define MAX_SESSIONS  64

#define USERNAME_LEN  32
#define NAME_LEN      64
#define PASSWORD_LEN  64
#define PURPOSE_LEN   96
#define NOTE_LEN      96
#define MESSAGE_LEN   128
#define REPLY_LEN     128
#define TEXT_MAX      8192

#define BANK_FILE_IDENTIFIER 0x42414E4Bu   /* "BANK" */
#define DEFAULT_PORT  9099

#define LOAN_SIMPLE          1
#define LOAN_COMPOUND        2
#define DEFAULT_LOAN_RATE_BPS 0      /* set by the administrator */
#define MAX_LOAN_PAISE       1000000000LL

#define ROLE_CUSTOMER 1
#define ROLE_EMPLOYEE 2
#define ROLE_MANAGER  3
#define ROLE_ADMIN    4

#define LOAN_PENDING  1
#define LOAN_APPROVED 2
#define LOAN_REJECTED 3

#define TX_DEPOSIT    1
#define TX_WITHDRAW   2
#define TX_TRANSFER_OUT 3
#define TX_TRANSFER_IN  4
#define TX_LOAN       5

#define FEEDBACK_NEW      0
#define FEEDBACK_REVIEWED 1

/* protocol operations */
enum {
    OP_LOGIN = 1,
    OP_LOGOUT,
    OP_CHANGE_PW,
    OP_BALANCE,
    OP_DEPOSIT,
    OP_WITHDRAW,
    OP_TRANSFER,
    OP_HISTORY,
    OP_MINI,
    OP_ADD_FEEDBACK,
    OP_MY_FEEDBACK,
    OP_APPLY_LOAN,
    OP_ADD_CUSTOMER,
    OP_MODIFY_CUSTOMER,
    OP_CLOSE_ACCOUNT,
    OP_ASSIGNED_LOANS,
    OP_PROCESS_LOAN,
    OP_CUSTOMER_TX,
    OP_SET_ACTIVE,
    OP_PENDING_LOANS,
    OP_ASSIGN_LOAN,
    OP_LIST_FEEDBACK,
    OP_REVIEW_FEEDBACK,
    OP_ADD_EMPLOYEE,
    OP_MODIFY_USER,
    OP_SET_ROLE,
    OP_LIST_USERS,
    OP_VIEW_LOG,
    OP_SET_LOAN_RATE
};

struct header {
    unsigned identifier;
    int  next_user;
    int  next_loan;
    int  next_txn;
    int  next_feedback;
    int  num_users;
    int  num_loans;
    int  num_txns;
    int  num_feedback;
    int  loan_rate_bps;
    int  loan_interest_type;
};

struct user {
    int  id;
    char     username[USERNAME_LEN];
    char     name[NAME_LEN];
    unsigned long long pass_hash;
    unsigned salt;
    int  role;
    int  active;
    long long  balance;          /* paise, customers only */
    time_t     created_at;
};

struct loan {
    int  id;
    int  customer_id;
    int  employee_id;      /* 0 until assigned */
    int  status;
    long long  amount;           /* principal in paise */
    int  months;
    int  interest_type;    /* simple or compound */
    int  rate_bps;         /* annual rate snapshot */
    long long  interest_amount;  /* paise */
    long long  total_payable;    /* principal plus interest */
    time_t     applied_at;
    time_t     decided_at;
    char     purpose[PURPOSE_LEN];
};

struct transaction {
    int  id;
    int  user_id;
    int  type;
    int  pad;
    long long  amount;
    long long  balance_after;
    time_t     ts;
    char     note[NOTE_LEN];
};

struct feedback {
    int  id;
    int  user_id;
    int  status;
    int  pad;
    time_t     ts;
    char     message[MESSAGE_LEN];
    char     reply[REPLY_LEN];
};

struct bank {
    struct header      h;
    struct user        users[MAX_USERS];
    struct loan        loans[MAX_LOANS];
    struct transaction txns[MAX_TXNS];
    struct feedback    feedback[MAX_FEEDBACK];
};

struct session {
    int user_id;
    pid_t   pid;
    int active;
};

struct shared {
    struct session s[MAX_SESSIONS];
};

struct request {
    int op;
    int id;
    int role;
    int active;
    int months;
    int decision;
    int interest;
    long long amount;
    char    username[USERNAME_LEN];
    char    password[PASSWORD_LEN];
    char    name[NAME_LEN];
    char    text[MESSAGE_LEN];
};

struct response {
    int status;            /* 0 = ok, -1 = error */
    int user_id;
    int role;
    char    text[TEXT_MAX];
};

#endif
