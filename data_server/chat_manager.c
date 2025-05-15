#include "chat_manager.h"

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

int send_message(MYSQL *conn, Message *message){
	char query[512];

    snprintf(query, sizeof(query),
        "INSERT INTO messages (chat_id, sender_id, content, message_type) VALUES ('%d', '%d', '%s', '%s')", 
			 message -> chat_id, message -> sender_id, message -> content, message -> message_type);

	printf("%s\n", query);


    if (mysql_query(conn, query)) {
        fprintf(stderr, "Create chat failed: %s\n", mysql_error(conn));
        return -1;
    }

    printf("Message sent successfully\n");

	return 0;
}
