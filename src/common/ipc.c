#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

#define SEM_ID 0
#define LOG_STOP 2

struct logmsg {
    long mtype;
    int  uid;
    char text[256];
};

static int shmid = -1;
static int semid = -1;
static int msqid = -1;
static int audit_fd = -1;
static struct shared *shm;
static pthread_t logger_thread;
static int logger_started = 0;

static int sem_p(int id) { /* Acquires the session semaphore. */
    struct sembuf op = { (unsigned short)id, -1, SEM_UNDO };
    while (semop(semid, &op, 1) < 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

static int sem_v(int id) { /* Releases the session semaphore. */
    struct sembuf op = { (unsigned short)id, 1, SEM_UNDO };
    while (semop(semid, &op, 1) < 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

int session_add(int user_id, pid_t pid) { /* Adds one active session slot. */
    int free_slot = -1;
    if (sem_p(SEM_ID) < 0) return -2;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (shm->s[i].active && shm->s[i].user_id == user_id) { sem_v(SEM_ID); return -1; }
        if (!shm->s[i].active && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) { sem_v(SEM_ID); return -2; }
    shm->s[free_slot].user_id = user_id;
    shm->s[free_slot].pid = pid;
    shm->s[free_slot].active = 1;
    sem_v(SEM_ID);
    return 0;
}

void session_remove_user(int user_id) { /* Removes all sessions for a user. */
    if (sem_p(SEM_ID) < 0) return;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (shm->s[i].active && shm->s[i].user_id == user_id) memset(&shm->s[i], 0, sizeof(shm->s[i]));
    }
    sem_v(SEM_ID);
}

void session_remove_pid(pid_t pid) { /* Removes sessions for a handler pid. */
    if (sem_p(SEM_ID) < 0) return;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (shm->s[i].active && shm->s[i].pid == pid) memset(&shm->s[i], 0, sizeof(shm->s[i]));
    }
    sem_v(SEM_ID);
}

int session_has(int user_id, pid_t pid) { /* Checks for a matching user and pid session. */
    int found = 0;
    if (sem_p(SEM_ID) < 0) return 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (shm->s[i].active && shm->s[i].user_id == user_id &&
            shm->s[i].pid == pid) {
            found = 1;
            break;
        }
    }
    sem_v(SEM_ID);
    return found;
}

void audit_log(int uid, const char *fmt, ...) { /* Queues one formatted audit event. */
    struct logmsg m;
    va_list ap;
    memset(&m, 0, sizeof(m));
    m.mtype = 1;
    m.uid = uid;
    va_start(ap, fmt);
    vsnprintf(m.text, sizeof(m.text), fmt, ap);
    va_end(ap);
    if (msqid >= 0) msgsnd(msqid, &m, sizeof(m.text), IPC_NOWAIT);
}

static void *logger_main(void *arg) {
    (void)arg;
    for (;;) {
        struct logmsg m;
        char line[512];
        char stamp[32];
        time_t now;
        struct tm tmv;
        if (msgrcv(msqid, &m, sizeof(m.text), 0, 0) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (m.mtype == LOG_STOP) break;
        now = time(NULL);
        localtime_r(&now, &tmv);
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
        snprintf(line, sizeof(line), "%s user=%d %s\n", stamp, m.uid, m.text);
        if (audit_fd >= 0) {
            ssize_t ignored = write(audit_fd, line, strlen(line));
            (void)ignored;
        }
    }
    return NULL;
}

void start_logger(void) { /* Opens the audit file and starts the logger. */
    audit_fd = open(AUDIT_FILE, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (pthread_create(&logger_thread, NULL, logger_main, NULL) == 0) logger_started = 1;
}

void stop_logger(void) { /* Stops the logger and closes the audit file. */
    struct logmsg m;
    if (!logger_started) return;
    memset(&m, 0, sizeof(m));
    m.mtype = LOG_STOP;
    msgsnd(msqid, &m, sizeof(m.text), 0);
    pthread_join(logger_thread, NULL);
    logger_started = 0;
    if (audit_fd >= 0) close(audit_fd);
    audit_fd = -1;
}

static void remove_ipc(void) { /* Removes existing System V IPC objects. */
    int id;
    id = shmget(ftok(DATA_DIR, 'S'), 0, 0);
    if (id >= 0) shmctl(id, IPC_RMID, NULL);
    id = semget(ftok(DATA_DIR, 'M'), 0, 0);
    if (id >= 0) semctl(id, 0, IPC_RMID, 0);
    id = msgget(ftok(DATA_DIR, 'Q'), 0);
    if (id >= 0) msgctl(id, IPC_RMID, NULL);
}

static int create_ipc(void) { /* Creates the System V shared state. */
    union semun arg;
    key_t skey = ftok(DATA_DIR, 'S');
    key_t mkey = ftok(DATA_DIR, 'M');
    key_t qkey = ftok(DATA_DIR, 'Q');
    if (skey == (key_t)-1 || mkey == (key_t)-1 || qkey == (key_t)-1) return -1;
    shmid = shmget(skey, sizeof(struct shared), IPC_CREAT | 0600);
    if (shmid < 0) return -1;
    shm = shmat(shmid, NULL, 0);
    if (shm == (void *)-1) return -1;
    memset(shm, 0, sizeof(*shm));
    semid = semget(mkey, 1, IPC_CREAT | 0600);
    if (semid < 0) return -1;
    arg.val = 1;
    if (semctl(semid, 0, SETVAL, arg) < 0) return -1;
    msqid = msgget(qkey, IPC_CREAT | 0600);
    if (msqid < 0) return -1;
    return 0;
}

int ipc_start(void) { /* Resets and creates the IPC objects. */
    remove_ipc();
    return create_ipc();
}

void ipc_stop(void) { /* Detaches and removes the IPC objects. */
    if (shm) {
        shmdt(shm);
        shm = NULL;
    }
    remove_ipc();
}
