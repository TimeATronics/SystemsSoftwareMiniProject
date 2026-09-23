#ifndef SERVER_H
#define SERVER_H

#include "bank.h"

void set_error(struct response *r, const char *fmt, ...);
void set_ok(struct response *r, const char *fmt, ...);
void addf(char *buf, size_t cap, const char *fmt, ...);
void money(char *out, size_t cap, long long paise);
int  find_user_bank(struct bank *b, const char *username,
                    struct user **out, int *out_idx);
int  find_id_bank(struct bank *b, int id, struct user **out, int *out_idx);
int  add_txn(struct bank *b, int uid, int type, long long amount,
             long long balance, const char *note);
int  add_feedback(struct bank *b, int uid, const char *message);
int  begin_write(struct bank *b);
int  end_write(struct bank *b, int save);

void handle_request(struct request *r, struct response *resp,
                    int *uid, pid_t pid);

#endif
