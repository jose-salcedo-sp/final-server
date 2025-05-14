#include "user_manager.h"

void create_user(MYSQL *conn, User *newUser) {
    MYSQL_STMT *stmt;
    MYSQL_BIND bind[3];
    const char *query = "INSERT INTO users (username, email, password_hash) VALUES (?, ?, ?)";

    stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init() failed\n");
        exit(EXIT_FAILURE);
    }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare() failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        exit(EXIT_FAILURE);
    }

    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char *)newUser->username;
    bind[0].buffer_length = strlen(newUser->username);

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char *)newUser->email;
    bind[1].buffer_length = strlen(newUser->email);

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char *)newUser->hash_password;
    bind[2].buffer_length = strlen(newUser->hash_password);

    if (mysql_stmt_bind_param(stmt, bind)) {
        fprintf(stderr, "mysql_stmt_bind_param() failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        exit(EXIT_FAILURE);
    }

    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "mysql_stmt_execute() failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        exit(EXIT_FAILURE);
    }

    printf("User created successfully.\n");

    mysql_stmt_close(stmt);
}
