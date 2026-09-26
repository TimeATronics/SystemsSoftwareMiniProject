#include "bank.h"
#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int sock = -1;
static int uid = 0;
static int role = 0;
static int input_eof = 0;



static int rpc(struct request *req, struct response *resp) { /* Sends one request and receives one response. */
    memset(resp, 0, sizeof(*resp));
    if (send_all(sock, req, sizeof(*req)) < 0) return -1;
    if (recv_all(sock, resp, sizeof(*resp)) < 0) return -1;
    return 0;
}

static void show(struct response *resp) { /* Prints a response, prefixing errors. */
    if (resp->status < 0) printf("Error: %s\n", resp->text);
    else printf("%s\n", resp->text);
}

static void read_line(const char *prompt, char *buf, size_t cap) { /* Reads one line into a bounded buffer. */
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)cap, stdin)) { buf[0] = '\0'; input_eof = 1; return; }
    buf[strcspn(buf, "\r\n")] = '\0';
}

static int read_int(const char *prompt) { /* Reads and parses an integer choice. */
    char buf[32];
    int v = 0;
    read_line(prompt, buf, sizeof(buf));
    if (input_eof) return -1;
    if (sscanf(buf, "%d", &v) != 1) return 0;
    return v;
}

static long long read_money(const char *prompt) { /* Reads a decimal amount and returns paise. */
    char buf[64];
    double v;
    read_line(prompt, buf, sizeof(buf));
    if (sscanf(buf, "%lf", &v) != 1 || v < 0) return -1;
    return (long long)(v * 100.0 + 0.5);
}

static void wait_enter(void) { /* Waits for the user to press Enter. */
    char buf[8];
    printf("\nPress Enter to continue...");
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) input_eof = 1;
}

static int do_change_password(void) { /* Prompts for and submits a password change. */
    struct request req;
    struct response resp;
    memset(&req, 0, sizeof(req));
    req.op = OP_CHANGE_PW;
    read_line("Current password: ", req.password, sizeof(req.password));
    read_line("New password: ", req.text, sizeof(req.text));
    if (rpc(&req, &resp) < 0) return -1;
    show(&resp);
    return 0;
}

static int customer_menu(void) { /* Runs the customer menu loop. */
    for (;;) {
        struct request req;
        struct response resp;
        int choice;
        printf("\n--- Customer Menu (ID: %d) ---\n", uid);
        printf("1. View Account Balance\n");
        printf("2. Deposit Money\n");
        printf("3. Withdraw Money\n");
        printf("4. Transfer Funds\n");
        printf("5. Apply for a Loan\n");
        printf("6. Change Password\n");
        printf("7. View Transaction History\n");
        printf("8. Mini Statement\n");
        printf("9. Add Feedback\n");
        printf("10. View Feedback Replies\n");
        printf("11. Logout\n");
        printf("12. Exit\n");
        printf("0. Clear Screen\n");
        choice = read_int("Enter choice: ");
        if (choice < 0) return -1;
        memset(&req, 0, sizeof(req));
        switch (choice) {
        case 1:
            req.op = OP_BALANCE;
            break;
        case 2:
            req.op = OP_DEPOSIT;
            req.amount = read_money("Enter amount: ");
            break;
        case 3:
            req.op = OP_WITHDRAW;
            req.amount = read_money("Enter amount: ");
            break;
        case 4:
            req.op = OP_TRANSFER;
            read_line("Recipient username: ", req.username, sizeof(req.username));
            req.amount = read_money("Enter amount: ");
            break;
        case 5:
            req.op = OP_APPLY_LOAN;
            req.amount = read_money("Enter loan amount: ");
            req.months = read_int("Enter term in months: ");
            read_line("Purpose: ", req.text, sizeof(req.text));
            break;
        case 6:
            if (do_change_password() < 0) return -1;
            wait_enter();
            continue;
        case 7:
            req.op = OP_HISTORY;
            break;
        case 8:
            req.op = OP_MINI;
            break;
        case 9:
            req.op = OP_ADD_FEEDBACK;
            read_line("Feedback: ", req.text, sizeof(req.text));
            break;
        case 10:
            req.op = OP_MY_FEEDBACK;
            break;
        case 11:
            req.op = OP_LOGOUT;
            if (rpc(&req, &resp) == 0) show(&resp);
            uid = 0;
            return 0;
        case 12:
            req.op = OP_LOGOUT;
            (void)rpc(&req, &resp);
            return -1;
        case 0:
            printf("\033[H\033[2J");
            fflush(stdout);
            continue;
        default:
            printf("Invalid choice\n");
            continue;
        }
        if (req.op == OP_DEPOSIT || req.op == OP_WITHDRAW || req.op == OP_TRANSFER ||
            req.op == OP_APPLY_LOAN) {
            if (req.amount <= 0) { printf("Error: invalid amount\n"); continue; }
        }
        if (rpc(&req, &resp) < 0) { printf("Connection lost\n"); return -1; }
        show(&resp);
        wait_enter();
    }
}

static int employee_menu(void) { /* Runs the employee menu loop. */
    for (;;) {
        struct request req;
        struct response resp;
        int choice, decision;
        printf("\n--- Employee Menu (ID: %d) ---\n", uid);
        printf("1. Add New Customer\n");
        printf("2. Modify Customer Details\n");
        printf("3. Process Loan Applications\n");
        printf("4. View Customer Transactions\n");
        printf("5. View Assigned Loan Applications\n");
        printf("6. Close Customer Account\n");
        printf("7. Change Password\n");
        printf("8. Logout\n");
        printf("9. Exit\n");
        printf("0. Clear Screen\n");
        choice = read_int("Enter choice: ");
        if (choice < 0) return -1;
        memset(&req, 0, sizeof(req));
        switch (choice) {
        case 1:
            req.op = OP_ADD_CUSTOMER;
            read_line("Customer name: ", req.name, sizeof(req.name));
            read_line("Username: ", req.username, sizeof(req.username));
            read_line("Password: ", req.password, sizeof(req.password));
            req.amount = read_money("Opening balance: ");
            break;
        case 2:
            req.op = OP_MODIFY_CUSTOMER;
            read_line("Customer username: ", req.username, sizeof(req.username));
            read_line("New name (blank to keep): ", req.name, sizeof(req.name));
            read_line("New password (blank to keep): ", req.password, sizeof(req.password));
            break;
        case 3:
            req.op = OP_ASSIGNED_LOANS;
            if (rpc(&req, &resp) == 0) show(&resp);
            req.op = OP_PROCESS_LOAN;
            req.id = read_int("Enter loan ID: ");
            decision = read_int("1 = approve, 2 = reject: ");
            req.decision = decision == 1 ? LOAN_APPROVED : LOAN_REJECTED;
            read_line("Note: ", req.text, sizeof(req.text));
            break;
        case 4:
            req.op = OP_CUSTOMER_TX;
            read_line("Customer username: ", req.username, sizeof(req.username));
            break;
        case 5:
            req.op = OP_ASSIGNED_LOANS;
            break;
        case 6:
            req.op = OP_CLOSE_ACCOUNT;
            read_line("Customer username: ", req.username, sizeof(req.username));
            break;
        case 7:
            if (do_change_password() < 0) return -1;
            wait_enter();
            continue;
        case 8:
            req.op = OP_LOGOUT;
            if (rpc(&req, &resp) == 0) show(&resp);
            uid = 0;
            return 0;
        case 9:
            req.op = OP_LOGOUT;
            (void)rpc(&req, &resp);
            return -1;
        case 0:
            printf("\033[H\033[2J");
            fflush(stdout);
            continue;
        default:
            printf("Invalid choice\n");
            continue;
        }
        if (rpc(&req, &resp) < 0) { printf("Connection lost\n"); return -1; }
        show(&resp);
        wait_enter();
    }
}

static int manager_menu(void) { /* Runs the manager menu loop. */
    for (;;) {
        struct request req;
        struct response resp;
        int choice, status;
        printf("\n--- Manager Menu (ID: %d) ---\n", uid);
        printf("1. Activate/Deactivate Customer Account\n");
        printf("2. View Pending Loan Applications\n");
        printf("3. Assign Loan to Employee\n");
        printf("4. Review Customer Feedback\n");
        printf("5. Change Password\n");
        printf("6. Logout\n");
        printf("7. Exit\n");
        printf("0. Clear Screen\n");
        choice = read_int("Enter choice: ");
        if (choice < 0) return -1;
        memset(&req, 0, sizeof(req));
        switch (choice) {
        case 1:
            req.op = OP_SET_ACTIVE;
            read_line("Customer username: ", req.username, sizeof(req.username));
            status = read_int("1 = activate, 2 = deactivate: ");
            req.active = status == 1;
            break;
        case 2:
            req.op = OP_PENDING_LOANS;
            break;
        case 3:
            req.op = OP_PENDING_LOANS;
            if (rpc(&req, &resp) == 0) show(&resp);
            req.op = OP_ASSIGN_LOAN;
            req.id = read_int("Enter loan ID: ");
            read_line("Employee username: ", req.username, sizeof(req.username));
            break;
        case 4:
            req.op = OP_LIST_FEEDBACK;
            if (rpc(&req, &resp) == 0) show(&resp);
            req.op = OP_REVIEW_FEEDBACK;
            req.id = read_int("Enter feedback ID to review: ");
            read_line("Reply to customer: ", req.text, sizeof(req.text));
            break;
        case 5:
            if (do_change_password() < 0) return -1;
            wait_enter();
            continue;
        case 6:
            req.op = OP_LOGOUT;
            if (rpc(&req, &resp) == 0) show(&resp);
            uid = 0;
            return 0;
        case 7:
            req.op = OP_LOGOUT;
            (void)rpc(&req, &resp);
            return -1;
        case 0:
            printf("\033[H\033[2J");
            fflush(stdout);
            continue;
        default:
            printf("Invalid choice\n");
            continue;
        }
        if (rpc(&req, &resp) < 0) { printf("Connection lost\n"); return -1; }
        show(&resp);
        wait_enter();
    }
}

static int admin_menu(void) { /* Runs the administrator menu loop. */
    for (;;) {
        struct request req;
        struct response resp;
        int choice, r;
        printf("\n--- Administrator Menu (ID: %d) ---\n", uid);
        printf("1. Add New Employee/Manager\n");
        printf("2. Modify User Details\n");
        printf("3. Manage User Roles\n");
        printf("4. List Users\n");
        printf("5. View Activity Log\n");
        printf("6. Set Loan Interest Rate\n");
        printf("7. Change Password\n");
        printf("8. Logout\n");
        printf("9. Exit\n");
        printf("0. Clear Screen\n");
        choice = read_int("Enter choice: ");
        if (choice < 0) return -1;
        memset(&req, 0, sizeof(req));
        switch (choice) {
        case 1:
            req.op = OP_ADD_EMPLOYEE;
            read_line("Name: ", req.name, sizeof(req.name));
            read_line("Username: ", req.username, sizeof(req.username));
            read_line("Password: ", req.password, sizeof(req.password));
            r = read_int("1 = employee, 2 = manager: ");
            req.role = r == 2 ? ROLE_MANAGER : ROLE_EMPLOYEE;
            break;
        case 2:
            req.op = OP_MODIFY_USER;
            read_line("Username: ", req.username, sizeof(req.username));
            read_line("New name (blank to keep): ", req.name, sizeof(req.name));
            read_line("New password (blank to keep): ", req.password, sizeof(req.password));
            break;
        case 3:
            req.op = OP_SET_ROLE;
            read_line("Username: ", req.username, sizeof(req.username));
            r = read_int("1 = customer, 2 = employee, 3 = manager, 4 = admin: ");
            req.role = (r >= 1 && r <= 4) ? r : 0;
            break;
        case 4:
            req.op = OP_LIST_USERS;
            break;
        case 5:
            req.op = OP_VIEW_LOG;
            break;
        case 6:
            req.op = OP_SET_LOAN_RATE;
            req.amount = read_money("Annual interest rate (percent): ");
            r = read_int("1 = simple, 2 = compound: ");
            req.interest = (r == 2) ? LOAN_COMPOUND : LOAN_SIMPLE;
            break;
        case 7:
            if (do_change_password() < 0) return -1;
            wait_enter();
            continue;
        case 8:
            req.op = OP_LOGOUT;
            if (rpc(&req, &resp) == 0) show(&resp);
            uid = 0;
            return 0;
        case 9:
            req.op = OP_LOGOUT;
            (void)rpc(&req, &resp);
            return -1;
        case 0:
            printf("\033[H\033[2J");
            fflush(stdout);
            continue;
        default:
            printf("Invalid choice\n");
            continue;
        }
        if (rpc(&req, &resp) < 0) { printf("Connection lost\n"); return -1; }
        show(&resp);
        wait_enter();
    }
}

static int login_screen(void) { /* Prompts for role and credentials. */
    struct request req;
    struct response resp;
    int choice, want;
    printf("\n--- Banking Management System ---\n");
    printf("Select Role to Login:\n");
    printf("1. Customer\n");
    printf("2. Bank Employee\n");
    printf("3. Manager\n");
    printf("4. Administrator\n");
    printf("5. Exit\n");
    printf("0. Clear Screen\n");
    choice = read_int("Enter choice: ");
    if (choice < 0) return -1;
    if (choice == 5) return -1;
    if (choice == 0) { printf("\033[H\033[2J"); fflush(stdout); return 0; }
    if (choice < 1 || choice > 5) { printf("Invalid choice\n"); return 0; }
    want = choice == 1 ? ROLE_CUSTOMER :
           choice == 2 ? ROLE_EMPLOYEE :
           choice == 3 ? ROLE_MANAGER : ROLE_ADMIN;
    memset(&req, 0, sizeof(req));
    req.op = OP_LOGIN;
    read_line("Username: ", req.username, sizeof(req.username));
    read_line("Password: ", req.password, sizeof(req.password));
    if (rpc(&req, &resp) < 0) { printf("Connection lost\n"); return -1; }
    if (resp.status < 0) { show(&resp); return 0; }
    if (resp.role != want) {
        printf("Error: this account does not have the selected role\n");
        req.op = OP_LOGOUT;
        (void)rpc(&req, &resp);
        return 0;
    }
    uid = resp.user_id;
    role = resp.role;
    show(&resp);
    return 1;
}

int main(int argc, char *argv[]) { /* Connects to the server and runs the login loop. */
    int port = DEFAULT_PORT;
    int fd;
    struct sockaddr_in addr;
    if (argc > 1) port = atoi(argv[1]);
    if (port <= 0 || port > 65535) port = DEFAULT_PORT;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }
    sock = fd;
    for (;;) {
        int ok = login_screen();
        int rc;
        if (ok < 0) break;
        if (ok == 0) continue;
        if (role == ROLE_CUSTOMER) rc = customer_menu();
        else if (role == ROLE_EMPLOYEE)
            rc = employee_menu();
        else if (role == ROLE_MANAGER)
            rc = manager_menu();
        else rc = admin_menu();
        if (rc < 0) break;
    }
    close(sock);
    printf("Bye.\n");
    return 0;
}
