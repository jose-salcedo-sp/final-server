#include "chat_manager.h"
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
    char query[512];

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

    	message-> message_id = row[0] ? atoi(row[0]) : 0;
    	message-> sender_id = row[1] ? atoi(row[1]) : 0;

    	message-> sender_username = row[2] ? row[2] : "";

    	message-> content = row[3] ? row[3] : "";
    	message-> message_type = row[4] ? row[4] : "";
    	message-> created_at = row[5] ? row[5] : "";	
	}

    mysql_free_result(res);
    return messages_count;
}
