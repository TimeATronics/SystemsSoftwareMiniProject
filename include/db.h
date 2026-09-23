#ifndef DB_H
#define DB_H

#include "bank.h"

int  db_open(const char *dir);
void db_close(void);
int  db_load(struct bank *b);
int  db_save(struct bank *b);
int  db_lock_all(int write);
void db_unlock_all(void);
int  db_read_header(struct header *h);
int  db_read_user(int idx, struct user *u);
int  db_read_loan(int idx, struct loan *l);
int  db_read_txn(int idx, struct transaction *t);
int  db_read_feedback(int idx, struct feedback *f);
int  db_find_user(const char *username, struct user *u, int *idx);
int  db_find_user_id(int id, struct user *u, int *idx);
unsigned long long password_hash(const char *password, unsigned salt);
unsigned make_salt(void);

#endif
