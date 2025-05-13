#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>

int main() {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    const char *server = "172.21.16.1";
    const char *user = "admin";
    const char *password = "UP2025";
    const char *database = "MessengerDatabase";

    conn = mysql_init(NULL);

    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
        fprintf(stderr, "Connection failed: %s\n", mysql_error(conn));
        exit(1);
    }

    if (mysql_query(conn, "SHOW TABLES")) {
        fprintf(stderr, "Query failed: %s\n", mysql_error(conn));
        exit(1);
    }

    res = mysql_store_result(conn);

    printf("Tables:\n");
    while ((row = mysql_fetch_row(res)) != NULL) {
        printf("%s\n", row[0]);
    }

    mysql_free_result(res);
    mysql_close(conn);

    return 0;
}

