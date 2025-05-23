#include "chat_manager.h"
#include "user_manager.h"
#include <mysql/mysql.h>
#include <mysql/mysql_com.h>
#include <stdio.h>
#include <string.h>

int create_chat(MYSQL *conn, Chat *chat){
	char query[512];

    snprintf(query, sizeof(query),
        "INSERT INTO chats (is_group, chat_name) VALUES ('%d', '%s')", chat -> is_group, chat -> chat_name);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Create chat failed: %s\n", mysql_error(conn));
        return -1;
    }

	chat->id = (int)mysql_insert_id(conn);

    printf("Chat created successfully. ID = %d\n", chat->id);

	return 0;
}

int add_to_chat(MYSQL *conn, int chat_id, int user_id, int is_admin){	
	char query[512];

    snprintf(query, sizeof(query),
        "INSERT INTO chat_participants (chat_id, user_id, is_admin) VALUES ('%d', '%d', '%d')", 
			 chat_id, user_id, is_admin);

	printf("%s\n", query);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Join failed: %s\n", mysql_error(conn));
        return -1;
    }

    printf("User joined succesfully successfully.\n");

	return 0;
}

int send_message(MYSQL *conn, Message *message) {
    char query[2048];


    // 1. Insert the new message
    snprintf(query, sizeof(query),
        "INSERT INTO messages (chat_id, sender_id, content, message_type) VALUES (%d, %d, '%s', '%s')",
        message->chat_id, message->sender_id, message->content, message->message_type);

    printf("%s\n", query);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Send message failed: %s\n", mysql_error(conn));
        return -1;
    }

    // 2. Get the last inserted message ID
    int message_id = (int) mysql_insert_id(conn);

    // 3. Update the last_message_id in the Chats table
    snprintf(query, sizeof(query),
        "UPDATE chats SET last_message_id = %d WHERE chat_id = %d",
        message_id, message->chat_id);

    printf("%s\n", query);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Update last_message_id failed: %s\n", mysql_error(conn));
        return -1;
    }

    printf("Message sent and last_message_id updated successfully\n");

    return 0;
}

int get_chats(MYSQL *conn, int user_id, char *last_update_timestamp, Chat chats[MAX_CHATS]) {
    char query[2048];
    MYSQL_RES *res;
    MYSQL_ROW row;
    int chat_count = 0;

    if (last_update_timestamp == NULL) {
		snprintf(query, sizeof(query), "SELECT c.chat_id, c.chat_name, c.is_group, m.content AS last_message_content, m.message_type AS last_message_type, m.created_at AS last_message_timestamp, u.username AS last_message_sender_username FROM chats c JOIN chat_participants cp ON cp.chat_id = c.chat_id LEFT JOIN messages m ON m.message_id = c.last_message_id LEFT JOIN users u ON u.user_id = m.sender_id WHERE cp.user_id = %d ORDER BY c.chat_id", user_id);
		
    } else {
        snprintf(query, sizeof(query), "SELECT c.chat_id, c.chat_name, c.is_group, m.content AS last_message_content, m.message_type AS last_message_type, m.created_at AS last_message_timestamp, u.username AS last_message_sender_username FROM chats c JOIN chat_participants cp ON cp.chat_id = c.chat_id LEFT JOIN messages m ON m.message_id = c.last_message_id LEFT JOIN users u ON u.user_id = m.sender_id WHERE cp.user_id = %d AND EXISTS (SELECT 1 FROM messages m2 WHERE m2.chat_id = c.chat_id AND m2.created_at > '%s' AND m2.is_deleted = 0) ORDER BY c.chat_id", user_id, last_update_timestamp);
    }

	printf("query: \n%s\n", query);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Query failed: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "Store result failed: %s\n", mysql_error(conn));
        return -1;
    }

 	while ((row = mysql_fetch_row(res)) != NULL) {
    	Chat *chat = &chats[chat_count++];

    	
    	chat->id = row[0] ? atoi(row[0]) : 0;
    	chat->chat_name = row[1] ? row[1] : "";
		
    	chat->is_group = row[2] ? atoi(row[2]) : 0;

    	chat->last_message_content = row[3] ? row[3] : "";
    	chat->last_message_type = row[4] ? row[4] : "";
    	chat->last_message_timestamp = row[5] ? row[5] : "";
    	chat->last_message_by = row[6] ? row[6] : "";
		
	}

    mysql_free_result(res);
    return chat_count;
}

int get_chat_messages(MYSQL *conn, int chat_id, char *last_update_timestamp, Message messages[MAX_MESSAGES]) {
    char query[2048];
    MYSQL_RES *res;
    MYSQL_ROW row;
    int messages_count = 0;

    if (last_update_timestamp == NULL) {
		snprintf(query, sizeof(query), "SELECT m.message_id, m.sender_id, u.username AS sender_username, m.content, m.message_type, m.created_at FROM messages m JOIN users u ON u.user_id = m.sender_id WHERE m.chat_id = %d AND m.is_deleted = 0", chat_id);
		
    } else {
		snprintf(query, sizeof(query), "SELECT m.message_id, m.sender_id, u.username AS sender_username, m.content, m.message_type, m.created_at FROM messages m JOIN users u ON u.user_id = m.sender_id WHERE m.chat_id = %d AND m.is_deleted = 0 AND (m.created_at > %s)", chat_id, last_update_timestamp);
    }

	printf("query: \n%s\n", query);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Query failed: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "Store result failed: %s\n", mysql_error(conn));
        return -1;
    }

	while ((row = mysql_fetch_row(res)) != NULL) {
    Message *message = &messages[messages_count++];

    message->message_id = row[0] ? atoi(row[0]) : 0;
    message->sender_id = row[1] ? atoi(row[1]) : 0;

    strncpy(message->sender_username, row[2] ? row[2] : "", MAX_USERNAME_LENGTH - 1);
    message->sender_username[MAX_USERNAME_LENGTH - 1] = '\0';

    strncpy(message->content, row[3] ? row[3] : "", MAX_CONTENT_LENGTH - 1);
    message->content[MAX_CONTENT_LENGTH - 1] = '\0';

    strncpy(message->message_type, row[4] ? row[4] : "", MAX_TYPE_LENGTH - 1);
    message->message_type[MAX_TYPE_LENGTH - 1] = '\0';

    strncpy(message->created_at, row[5] ? row[5] : "", MAX_TIMESTAMP_LENGTH - 1);
    message->created_at[MAX_TIMESTAMP_LENGTH - 1] = '\0';
}

    mysql_free_result(res);
	printf("Query done\n");
    return messages_count;
}

int get_chat_info(MYSQL *conn, int chat_id, Chat *chat, User participants[], int *participant_count) {
    MYSQL_RES *res;
    MYSQL_ROW row;

    // Obtener información del chat
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT chat_id, chat_name, is_group FROM chats WHERE chat_id = %d", chat_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Query failed: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    if ((row = mysql_fetch_row(res)) == NULL) {
        mysql_free_result(res);
        return -1; // Chat no encontrado
    }

    chat->id = atoi(row[0]);
    chat->chat_name = strdup(row[1]);
    chat->is_group = atoi(row[2]);

    mysql_free_result(res);

    // Obtener participantes del chat
    snprintf(query, sizeof(query),
             "SELECT u.user_id, u.username, u.email, u.password_hash, cp.is_admin "
             "FROM chat_participants cp "
             "JOIN users u ON cp.user_id = u.user_id "
             "WHERE cp.chat_id = %d", chat_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Query failed: %s\n", mysql_error(conn));
        return -1;
    }

    res = mysql_store_result(conn);
    int count = 0;

    while ((row = mysql_fetch_row(res)) != NULL && count < MAX_PARTICIPANTS) {
        User *user = &participants[count];
        user->id = atoi(row[0]);
        user->username = strdup(row[1]);
        user->email = strdup(row[2]);
        user->hash_password = strdup(row[3]);
        user->is_admin = atoi(row[4]);
        count++;
    }

    *participant_count = count;
    mysql_free_result(res);

    return 0;
}

int is_user_admin(MYSQL *conn, int chat_id, int user_id) {
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT is_admin FROM chat_participants WHERE chat_id = %d AND user_id = %d",
             chat_id, user_id);

    if (mysql_query(conn, query)) return 0;

    MYSQL_RES *res = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    int result = (row && atoi(row[0]) == 1) ? 1 : 0;

    mysql_free_result(res);
    return result;
}

int is_group_chat(MYSQL *conn, int chat_id) {
    char query[256];
    snprintf(query, sizeof(query), "SELECT is_group FROM chats WHERE chat_id = %d", chat_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "MySQL query failed: %s\n", mysql_error(conn));
        return -1;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "mysql_store_result() failed\n");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    bool result = false;
    if (row && row[0]) {
        result = atoi(row[0]) == 1;
    }

    mysql_free_result(res);
    return result;
}


int remove_from_chat(MYSQL *conn, int chat_id, int user_id) {
    char query[256];
    snprintf(query, sizeof(query),
             "DELETE FROM chat_participants WHERE chat_id = %d AND user_id = %d",
             chat_id, user_id);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Remove query failed: %s\n", mysql_error(conn));
        return -1;
    }

    return mysql_affected_rows(conn) > 0 ? 0 : -1;
}

