#include<stdio.h>
#include<mysql/mysql.h>

#define MAX_PARTICIPANTS 10

typedef struct {
	int id;
	char *chat_name;
	int is_group;
	int created_by;
} Chat;

typedef struct {
	int chat_id;
	int sender_id;
	char *content;
	char *message_type;
} Message;

int create_chat(MYSQL *conn, Chat *chat);
int add_to_chat(MYSQL *conn, int chat_id, int user_id, int is_admin);
int send_message(MYSQL *conn, Message *message);
