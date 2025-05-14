#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>

typedef struct {
	char* username;
	char* email;
	char* hash_password;
} User;

void create_user(MYSQL *db_connection, User *newUser);

#endif

