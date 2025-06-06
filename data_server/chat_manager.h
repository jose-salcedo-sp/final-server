#include<stdio.h>
#include<mysql/mysql.h>
#include "user_manager.h"

#define MAX_PARTICIPANTS 10
#define MAX_CHATS 100
#define MAX_STRING 256
#define MAX_MESSAGES 200

#define MAX_USERNAME_LENGTH 64
#define MAX_CONTENT_LENGTH 256
#define MAX_TYPE_LENGTH 32
#define MAX_TIMESTAMP_LENGTH 32

#define SYSTEM_USER 1

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
    int sender_id;
	int chat_id;
    char sender_username[MAX_USERNAME_LENGTH];
    char content[MAX_CONTENT_LENGTH];
    char message_type[MAX_TYPE_LENGTH];
    char created_at[MAX_TIMESTAMP_LENGTH];
} Message;

int create_chat(MYSQL *conn, Chat *chat);
int add_to_chat(MYSQL *conn, int chat_id, int user_id, int is_admin);
int send_message(MYSQL *conn, Message *message);
int get_chats(MYSQL *conn, int user_id, char *last_update_timestamp, Chat chats[MAX_CHATS]);
int get_chat_messages(MYSQL *conn, int chat_id, char *last_update_timestamp, Message messages[MAX_MESSAGES]);
int get_chat_info(MYSQL *conn, int chat_id, Chat *chat, User participants[], int *participant_count);

int get_participant_count(MYSQL *conn, int chat_id);
int get_admin_count(MYSQL *conn, int chat_id);
int promote_random_participant_to_admin(MYSQL *conn, int chat_id);
int delete_chat(MYSQL *conn, int chat_id);

