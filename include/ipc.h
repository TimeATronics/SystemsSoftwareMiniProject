#ifndef IPC_H
#define IPC_H

#include "bank.h"

int  ipc_start(void);
void ipc_stop(void);
int  session_add(int user_id, pid_t pid);
void session_remove_user(int user_id);
void session_remove_pid(pid_t pid);
int  session_has(int user_id, pid_t pid);
void audit_log(int uid, const char *fmt, ...);
void start_logger(void);
void stop_logger(void);

#endif
