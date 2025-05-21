#include<stdio.h>
#include<mysql/mysql.h>

#define MAX_PARTICIPANTS 10
#define MAX_CHATS 100
#define MAX_STRING 256
#define MAX_MESSAGES 200

typedef struct {
	int id;
	char *chat_name;
	int is_group;
	int created_by;

	char *last_message_content;
	char *last_message_type;
	char *last_message_timestamp;
	char *last_message_by;
} Chat;

typedef struct {
	int message_id;
	int chat_id;
	int sender_id;

	char *created_at;
	char *sender_username;
	char *content;
	char *message_type;
} Message;

int create_chat(MYSQL *conn, Chat *chat);
int add_to_chat(MYSQL *conn, int chat_id, int user_id, int is_admin);
int send_message(MYSQL *conn, Message *message);
int get_chats(MYSQL *conn, int user_id, char *last_update_timestamp, Chat chats[MAX_CHATS]);
int get_chat_messages(MYSQL *conn, int chat_id, char *last_update_timestamp, Message messages[MAX_MESSAGES]);
