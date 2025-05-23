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
	int id;
	int is_admin;
} User;

int create_user(MYSQL *db_connection, User *newUser);
int validate_user(MYSQL *db_connection, char *key, char *password_hash);
int get_user_info(MYSQL *db_connection, char *key, User *user);
int is_user_admin(MYSQL *conn, int chat_id, int user_id);
int remove_from_chat(MYSQL *conn, int chat_id, int user_id);
int is_group_chat(MYSQL *conn, int chat_id);
int remove_from_chat(MYSQL *conn, int chat_id, int user_id);

#endif

