#include "user_manager.h"
#include <mysql/mysql.h>
#include <string.h>

int create_user(MYSQL *conn, User *newUser) {
    MYSQL_STMT *stmt;
    MYSQL_BIND bind[3];
    const char *query = "INSERT INTO users (username, email, password_hash) VALUES (?, ?, ?)";

    stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init() failed\n");
        return -1;
    }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare() failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
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
        return -1;
    }

    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "mysql_stmt_execute() failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    printf("User created successfully.\n");

    mysql_stmt_close(stmt);
	return 0;
}


int validate_user(MYSQL *conn, char *key, char *password_hash) {
    const char *query = "SELECT password_hash FROM users WHERE username = ? OR email = ?";
    MYSQL_STMT *stmt;
    MYSQL_BIND param[2], result[1];
    unsigned long key_length = strlen(key);
    bool is_null[1];
    unsigned long length[1];
    char db_password_hash[65]; // Assuming SHA-256 hash (64 chars + null terminator)
    int status;

    stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "Failed to initialize statement: %s\n", mysql_error(conn));
        return -1;
    }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "Failed to prepare statement: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    memset(param, 0, sizeof(param));

    param[0].buffer_type = MYSQL_TYPE_STRING;
    param[0].buffer = key;
    param[0].buffer_length = key_length;
    param[0].length = &key_length;
    param[0].is_null = 0;

    param[1].buffer_type = MYSQL_TYPE_STRING;
    param[1].buffer = key;
    param[1].buffer_length = key_length;
    param[1].length = &key_length;
    param[1].is_null = 0;

    if (mysql_stmt_bind_param(stmt, param)) {
        fprintf(stderr, "Failed to bind parameters: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }
 
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Failed to execute statement: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }


    memset(result, 0, sizeof(result));
    memset(db_password_hash, 0, sizeof(db_password_hash));

    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = db_password_hash;
    result[0].buffer_length = sizeof(db_password_hash) - 1;
    result[0].length = &length[0];
    result[0].is_null = &is_null[0];

    if (mysql_stmt_bind_result(stmt, result)) {
        fprintf(stderr, "Failed to bind result: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }

    status = mysql_stmt_fetch(stmt);

    if (status == 0 && strcmp(db_password_hash, "") != 0) {
        strcpy(password_hash, db_password_hash); 
        printf("User found\npss: {%s}\n",db_password_hash);
    	mysql_stmt_close(stmt);
		return 0;

    } else if (status == MYSQL_NO_DATA) {
        printf("User not found\n");
    } else {
        fprintf(stderr, "Failed to fetch result: %s\n", mysql_stmt_error(stmt));
    }
    mysql_stmt_close(stmt);
	return -1;
}
