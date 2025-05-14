#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <stdbool.h>

typedef struct {
	char* username;
	char* email;
	char* hash_password;
} User;

int create_user(MYSQL *db_connection, User *newUser);
int validate_user(MYSQL *db_connection, char *key, char *password_hash);

#endif

