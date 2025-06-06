#include "user_manager.h"

int create_user(MYSQL *conn, User *newUser) {
    char query[512];

    snprintf(query, sizeof(query),
        "INSERT INTO users (username, email, password_hash) VALUES ('%s', '%s', '%s')",
        newUser->username, newUser->email, newUser->hash_password);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Create user failed: %s\n", mysql_error(conn));
        return -1;
    }

    printf("User created successfully.\n");
    return 0;
}

int validate_user(MYSQL *conn, char *key, char *password_hash) {
    char query[512];
    MYSQL_RES *res;
    MYSQL_ROW row;

    snprintf(query, sizeof(query),
        "SELECT password_hash, user_id FROM users WHERE username = '%s' OR email = '%s'", key, key);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Validate user query failed: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "mysql_store_result() failed: %s\n", mysql_error(conn));
        return -1;
    }

    row = mysql_fetch_row(res);
    if (row && row[0] && atoi(row[1]) != 1) {
        strncpy(password_hash, row[0], 64);
        password_hash[64] = '\0'; // Ensure null termination
        printf("User found\npss: {%s}\n", password_hash);
        mysql_free_result(res);
        return 0;
    } else {
        printf("User not found\n");
    }

    mysql_free_result(res);
    return -1;
}

int get_user_info(MYSQL *conn, char *key, User *user) {
    char query[512];
    MYSQL_RES *res;
    MYSQL_ROW row;

    snprintf(query, sizeof(query),
        "SELECT username, email, user_id FROM users WHERE username = '%s' OR email = '%s'", key, key);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Validate user query failed: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "mysql_store_result() failed: %s\n", mysql_error(conn));
        return -1;
    }

    row = mysql_fetch_row(res);
    if (row && row[0] && row[1] && row[2]) {
		user->username = strdup(row[0]);
		user->email = strdup(row[1]);
		user->id = atoi(row[2]);
		
        mysql_free_result(res);
        return 0;
    } else {
        printf("User not found\n");
        mysql_free_result(res);
        return -1;
    }
}
