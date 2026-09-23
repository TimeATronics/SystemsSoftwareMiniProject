#include "db.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

enum {
    TBL_HEADER = 0,
    TBL_USERS,
    TBL_LOANS,
    TBL_TXNS,
    TBL_FEEDBACK
};

static int lock_fd = -1;
static char data_dir[256] = DATA_DIR;

static int write_all(int fd, const void *buf, size_t len) { /* Writes a full buffer, retrying short writes. */
    const char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len) { /* Reads a full buffer, retrying short reads. */
    char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        done += (size_t)n;
    }
    return 0;
}
static int lock_range(int fd, short type, off_t start, off_t len) { /* Acquires a blocking fcntl lock on a byte range. */
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = type;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    while (fcntl(fd, F_SETLKW, &fl) < 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

static int unlock_range(int fd, off_t start, off_t len) { /* Releases a byte-range lock. */
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    return fcntl(fd, F_SETLK, &fl);
}

static off_t table_offset(int table) { /* Returns the byte offset of one table. */
    switch (table) {
    case TBL_USERS:    return (off_t)offsetof(struct bank, users);
    case TBL_LOANS:    return (off_t)offsetof(struct bank, loans);
    case TBL_TXNS:     return (off_t)offsetof(struct bank, txns);
    case TBL_FEEDBACK: return (off_t)offsetof(struct bank, feedback);
    default:           return 0;
    }
}

static size_t table_size(int table) { /* Returns the size of one table record. */
    switch (table) {
    case TBL_USERS:    return sizeof(struct user);
    case TBL_LOANS:    return sizeof(struct loan);
    case TBL_TXNS:     return sizeof(struct transaction);
    case TBL_FEEDBACK: return sizeof(struct feedback);
    default:           return 0;
    }
}

static off_t record_lock_offset(int table, int index) { /* Returns the lock-file byte for a record. */
    return 1 + (off_t)table * 1000000 + index;
}

static int read_record(int table, int index, void *out) { /* Reads one record under a shared lock. */
    int fd;
    off_t off;
    ssize_t n;
    int rc;
    if (lock_range(lock_fd, F_RDLCK, record_lock_offset(table, index), 1) < 0) return -1;
    fd = open(BANK_FILE, O_RDONLY);
    if (fd < 0) { unlock_range(lock_fd, record_lock_offset(table, index), 1); return -1; }
    off = table_offset(table) + (off_t)index * (off_t)table_size(table);
    n = pread(fd, out, table_size(table), off);
    close(fd);
    rc = (n == (ssize_t)table_size(table)) ? 0 : -1;
    unlock_range(lock_fd, record_lock_offset(table, index), 1);
    return rc;
}

unsigned make_salt(void) { /* Creates a salt from time and process id. */
    return (unsigned)time(NULL) ^ ((unsigned)getpid() << 16) ^ 0x9E3779B9u;
}

unsigned long long password_hash(const char *password, unsigned salt) { /* Hashes a password with a salt. */
    unsigned long long h = 1469598103934665603ULL ^ salt;
    while (*password) {
        h ^= (unsigned char)*password++;
        h *= 1099511628211ULL;
    }
    h ^= salt;
    h *= 1099511628211ULL;
    return h;
}

static void seed_admin(struct bank *b) { /* Creates the first administrator account. */
    struct user *u = &b->users[0];
    memset(u, 0, sizeof(*u));
    u->id = 1;
    strcpy(u->username, "admin");
    strcpy(u->name, "Bank Administrator");
    u->salt = make_salt();
    u->pass_hash = password_hash("admin123", u->salt);
    u->role = ROLE_ADMIN;
    u->active = 1;
    u->created_at = time(NULL);
    b->h.next_user = 2;
    b->h.num_users = 1;
}

int db_open(const char *dir) { /* Opens or creates and initializes the database. */
    char path[256];
    int fd;
    struct stat st;
    struct header h;
    struct bank b;
    snprintf(data_dir, sizeof(data_dir), "%s", dir);
    mkdir(data_dir, 0700);
    snprintf(path, sizeof(path), "%s", LOCK_FILE);
    lock_fd = open(path, O_RDWR | O_CREAT, 0600);
    if (lock_fd < 0) return -1;
    fd = open(BANK_FILE, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) < 0) { close(fd); return -1; }
    if (st.st_size < (off_t)sizeof(struct bank)) {
        if (ftruncate(fd, sizeof(struct bank)) < 0) { close(fd); return -1; }
    }
    memset(&h, 0, sizeof(h));
    if (pread(fd, &h, sizeof(h), 0) == (ssize_t)sizeof(h) &&
        h.identifier == BANK_FILE_IDENTIFIER) {
        close(fd);
        unlink(TMP_FILE);
        return 0;
    }
    memset(&b, 0, sizeof(b));
    b.h.identifier = BANK_FILE_IDENTIFIER;
    b.h.next_user = 1;
    b.h.next_loan = 1;
    b.h.next_txn = 1;
    b.h.next_feedback = 1;
    b.h.loan_rate_bps = DEFAULT_LOAN_RATE_BPS;
    b.h.loan_interest_type = LOAN_SIMPLE;
    seed_admin(&b);
    if (pwrite(fd, &b, sizeof(b), 0) != (ssize_t)sizeof(b)) { close(fd); return -1; }
    fsync(fd);
    close(fd);
    unlink(TMP_FILE);
    printf("First run: default administrator is admin / admin123\n");
    return 0;
}

void db_close(void) { /* Closes the database lock file. */
    if (lock_fd >= 0) close(lock_fd);
    lock_fd = -1;
}

int db_lock_all(int write) { /* Locks the whole database for reading or writing. */
    return lock_range(lock_fd, write ? F_WRLCK : F_RDLCK, 0, 0);
}

void db_unlock_all(void) { /* Releases the whole-file database lock. */
    unlock_range(lock_fd, 0, 0);
}

int db_load(struct bank *b) { /* Reads the complete database image. */
    int fd;
    fd = open(BANK_FILE, O_RDONLY);
    if (fd < 0) return -1;
    if (read_all(fd, b, sizeof(*b)) < 0) { close(fd); return -1; }
    close(fd);
    return 0;
}

int db_save(struct bank *b) { /* Writes, flushes, and renames a new database image. */
    int fd;
    int dfd;
    fd = open(TMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    if (write_all(fd, b, sizeof(*b)) < 0) { close(fd); return -1; }
    if (fsync(fd) < 0) { close(fd); return -1; }
    close(fd);
    if (rename(TMP_FILE, BANK_FILE) < 0) return -1;
    dfd = open(data_dir, O_RDONLY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }
    return 0;
}

int db_read_header(struct header *h) { /* Reads the header under a shared lock. */
    int fd;
    ssize_t n;
    if (lock_range(lock_fd, F_RDLCK, 0, 1) < 0) return -1;
    fd = open(BANK_FILE, O_RDONLY);
    if (fd < 0) { unlock_range(lock_fd, 0, 1); return -1; }
    n = pread(fd, h, sizeof(*h), 0);
    close(fd);
    unlock_range(lock_fd, 0, 1);
    return (n == (ssize_t)sizeof(*h)) ? 0 : -1;
}

int db_read_user(int idx, struct user *u) { /* Reads one user record. */
    if (idx < 0 || idx >= MAX_USERS) return -1;
    return read_record(TBL_USERS, idx, u);
}

int db_read_loan(int idx, struct loan *l) { /* Reads one loan record. */
    if (idx < 0 || idx >= MAX_LOANS) return -1;
    return read_record(TBL_LOANS, idx, l);
}

int db_read_txn(int idx, struct transaction *t) { /* Reads one transaction record. */
    if (idx < 0 || idx >= MAX_TXNS) return -1;
    return read_record(TBL_TXNS, idx, t);
}

int db_read_feedback(int idx, struct feedback *f) { /* Reads one feedback record. */
    if (idx < 0 || idx >= MAX_FEEDBACK) return -1;
    return read_record(TBL_FEEDBACK, idx, f);
}

int db_find_user(const char *username, struct user *out, int *out_idx) { /* Scans users for a username. */
    struct header h;
    if (db_read_header(&h) < 0) return -1;
    for (int i = 0; i < h.num_users && i < MAX_USERS; i++) {
        struct user u;
        if (db_read_user(i, &u) < 0) continue;
        if (u.id != 0 && strcmp(u.username, username) == 0) {
            if (out) *out = u;
            if (out_idx) *out_idx = i;
            return 0;
        }
    }
    return -1;
}

int db_find_user_id(int id, struct user *out, int *out_idx) { /* Finds a user by id. */
    struct user u;
    if (id <= 0 || id > MAX_USERS) return -1;
    if (db_read_user(id - 1, &u) < 0 || u.id != id) return -1;
    if (out) *out = u;
    if (out_idx) *out_idx = id - 1;
    return 0;
}
