#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <signal.h>

#include "../lib/cjson/cJSON.h"
#include "user_manager.h"
#include "chat_manager.h"
#include "heartbeat_manager.h"

#define BUFFER_SIZE 4096

#define LB_IP "10.7.27.134"
#define LB_UDP_PORT 5002

#define LOCAL_IP "10.7.26.84"
#define LOCAL_UDP_PORT 5001
#define LOCAL_TCP_PORT 5000
#define UDP_HEARTBEAT_INTERVAL 1

void reset_database(MYSQL *conn);

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

enum ACTIONS{VALIDATE_USER = 0, CREATE_USER = 2, GET_USER_INFO = 3, CREATE_CHAT = 4, ADD_TO_GROUP_CHAT = 5, SEND_MESSAGE = 6, GET_CHATS = 7, GET_CHAT_MESSAGES = 8, GET_CHAT_INFO = 9, REMOVE_FROM_CHAT = 10, EXIT_CHAT = 11};

void handle_action(MYSQL *conn, cJSON* json, char* response_buffer){
	char response_text[1024];
	int action, response_code;

	cJSON *response_json = cJSON_CreateObject();

	action = cJSON_GetObjectItem(json, "action") -> valueint;

	switch (action) {

		case VALIDATE_USER:
			char password_hash[100];
			cJSON *keyItem = cJSON_GetObjectItemCaseSensitive(json, "key");

			if (keyItem && keyItem->valuestring){
				char *key = keyItem -> valuestring;
				if (validate_user(conn, key, password_hash) == 0){
					response_code = 200;
					sprintf(response_text, "password_hash found for user with the key: %s", key);

					cJSON_AddStringToObject(response_json, "password_hash", password_hash);
				} else {
					response_code = 400;
					sprintf(response_text, "error retrieving password_hash for key: %s", key);
				}
			}

			break;

		case CREATE_USER:

			User newUser;

			cJSON *usernameItem = cJSON_GetObjectItem(json, "username");
			cJSON *emailItem = cJSON_GetObjectItem(json, "email");
			cJSON *passwordItem = cJSON_GetObjectItem(json, "password");

			if (usernameItem && usernameItem->valuestring && emailItem && emailItem->valuestring && passwordItem && passwordItem->valuestring) {
				newUser.username = strdup(usernameItem->valuestring);
				newUser.email = strdup(emailItem->valuestring);
				newUser.hash_password = strdup(passwordItem->valuestring);
}
			if (create_user(conn, &newUser) == 0){
				response_code = 200;
				sprintf(response_text,"User %s with email %s has been stored in the database",newUser.username, newUser.email);
			} else {
				response_code = 400;
				strcpy(response_text,"Unable to generate user");
			}

			free(newUser.username);
			free(newUser.email);
			free(newUser.hash_password);


			break;

		case GET_USER_INFO:
			User user;
			cJSON *info_keyItem = cJSON_GetObjectItemCaseSensitive(json, "key");

			if (info_keyItem && info_keyItem->valuestring){
				char *key = info_keyItem -> valuestring;
				if (get_user_info(conn, key, &user) == 0){
					response_code = 200;
					sprintf(response_text, "User %s was found with the ID: %d", user.username, user.id);

					cJSON_AddNumberToObject(response_json, "user_id", user.id);
					cJSON_AddStringToObject(response_json, "username", user.username);
					cJSON_AddStringToObject(response_json, "email", user.email);

				} else {
					response_code = 400;
					sprintf(response_text, "Error retreiving user info for key: %s", key);
				}

				free(user.username);
				free(user.email);
			}
			
			break;

		case CREATE_CHAT:
			Chat chat;
			int participants[MAX_PARTICIPANTS];
			
			cJSON *is_groupItem = cJSON_GetObjectItem(json, "is_group");
			cJSON *chat_nameItem = cJSON_GetObjectItem(json, "chat_name");
			cJSON *created_byItem = cJSON_GetObjectItem(json, "created_by");
			cJSON *participant_idsItem = cJSON_GetObjectItem(json, "participant_ids");

			if (is_groupItem && cJSON_IsBool(is_groupItem) && chat_nameItem && chat_nameItem -> valuestring && created_byItem && created_byItem -> valueint && participant_idsItem && cJSON_IsArray(participant_idsItem)){
				int participant_count = cJSON_GetArraySize(participant_idsItem);
				chat.is_group = is_groupItem -> valueint;
				printf("participants in new gc %d\n", MAX_PARTICIPANTS - (chat.is_group ? 1 : 8));
				if (0 < participant_count && participant_count < MAX_PARTICIPANTS - (chat.is_group ? 1 : 8)){
					chat.chat_name = chat_nameItem -> valuestring;
					chat.created_by = created_byItem ->valueint;

					if (create_chat(conn, &chat) == 0){
						participants[0] = chat.created_by;

						for (int i = 0; i < participant_count+1; i++) {
        					cJSON *id = cJSON_GetArrayItem(participant_idsItem, i);
        					if (cJSON_IsNumber(id)) {
            					participants[i+1] = id->valueint;
							}
        				}
						
						int success_count = 0;
						for (int i = 0; i < participant_count + 1; i++){
							if (add_to_chat(conn, chat.id, participants[i], participants[i] == chat.created_by) == 0){
								printf("User %d added to chat %s\n", participants[i], chat.chat_name);
								success_count++;
							} else {
								printf("Failed to add user %d to chat %s\n", participants[i], chat.chat_name);
							}
						}

						sprintf(response_text, "Chat %s was succesfully created with %d users", chat.chat_name, success_count);
						response_code = 200;
					
					} else {
						strcpy(response_text, "Chat couldn't be created unsuccesful");
						response_code = 400;
					}
				} else {
						sprintf(response_text, "Number of participants invalid for a chat of type %s", chat.is_group ? "group" : "direct message");
						response_code = 400;
				}
			}

			break;

		case ADD_TO_GROUP_CHAT:
			cJSON *added_byItem = cJSON_GetObjectItemCaseSensitive(json, "added_by");
			cJSON *chat_idItem = cJSON_GetObjectItemCaseSensitive(json, "chat_id");
			cJSON *participant_idsItem_atgc = cJSON_GetObjectItemCaseSensitive(json, "participant_ids");
			
			int participants_atgc[MAX_PARTICIPANTS];

			if (chat_idItem && chat_idItem -> valueint && participant_idsItem_atgc && cJSON_IsArray(participant_idsItem_atgc)){
				if(cJSON_GetArraySize(participant_idsItem_atgc) > 0 && added_byItem && added_byItem -> valueint){
					int participant_count = cJSON_GetArraySize(participant_idsItem_atgc);
					int added_by = added_byItem -> valueint;
					int chat_id = chat_idItem -> valueint;
						
					for (int i = 0; i < participant_count; i++) {
						cJSON *id = cJSON_GetArrayItem(participant_idsItem_atgc, i);
						if (cJSON_IsNumber(id)) {
            				participants_atgc[i] = id->valueint;
						}
        			}

					int success_count = 0;
					for (int i = 0; i < participant_count; i++){
						if (add_to_chat(conn, chat_id, participants_atgc[i], 0) == 0){
							printf("User %d added to chat %d\n", participants_atgc[i], chat_id);
							success_count++;
						} else {								
							printf("Failed to add user %d to chat %d\n", participants_atgc[i], chat_id);
						}
					}

					if (success_count > 0 ){
						sprintf(response_text, "Chat %d has succesfully added %d users", chat_id, success_count);
						response_code = 200;

					} else {
						sprintf(response_text, "Unable to add users to chat %d", chat_id);
						response_code = 400;

					}

				}
			}	

			break;

		case SEND_MESSAGE:
			Message message;

			cJSON *Item_sm_chat_id = cJSON_GetObjectItem(json, "chat_id");
			cJSON *Item_sm_sender_id = cJSON_GetObjectItem(json, "sender_id");
			cJSON *Item_sm_content = cJSON_GetObjectItem(json, "content");
			cJSON *Item_sm_message_type = cJSON_GetObjectItem(json, "message_type");

			if (Item_sm_chat_id && Item_sm_chat_id -> valueint && Item_sm_sender_id && Item_sm_sender_id -> valueint && Item_sm_content && Item_sm_content -> valuestring && Item_sm_message_type && Item_sm_message_type -> valuestring){
				message.chat_id = Item_sm_chat_id -> valueint;
				message.sender_id = Item_sm_sender_id -> valueint;

				strncpy(message.content, Item_sm_content->valuestring, MAX_CONTENT_LENGTH - 1);
				message.content[MAX_CONTENT_LENGTH - 1] = '\0';
				strncpy(message.message_type, Item_sm_message_type->valuestring, MAX_TYPE_LENGTH - 1);
				message.message_type[MAX_TYPE_LENGTH - 1] = '\0';


				if(send_message(conn, &message) == 0){
					sprintf(response_text, "Message from %d was succesfully sent to chat %d", message.sender_id, message.chat_id);
					response_code = 200;
				
				} else {
					strcpy(response_text, "Message couldn't be sent unsuccesful");
					response_code = 400;
				}
			} else {
				strcpy(response_text, "Parameter format invalid");
				response_code = 400;	
			}
			break;

		case GET_CHATS:
			cJSON *Item_gc_user_id = cJSON_GetObjectItemCaseSensitive(json, "user_id");
			cJSON *Item_gc_last_update_timestamp = cJSON_GetObjectItemCaseSensitive(json, "last_update_timestamp")	;

			if (Item_gc_user_id && Item_gc_last_update_timestamp && cJSON_IsNumber(Item_gc_user_id) &&
			    (cJSON_IsString(Item_gc_last_update_timestamp) || cJSON_IsNull(Item_gc_last_update_timestamp))) {

  				Chat chats[MAX_CHATS];
				int user_id = Item_gc_user_id->valueint;

			    char *last_update_timestamp = NULL;
    			if (!cJSON_IsNull(Item_gc_last_update_timestamp)) {
    		    	last_update_timestamp = Item_gc_last_update_timestamp->valuestring;
    			}

			    int chat_count = get_chats(conn, user_id, last_update_timestamp, chats);
			    if (chat_count > -1){
					sprintf(response_text, "%d chats succesfully retreived", chat_count);
					response_code = 200;

					cJSON *chats_array = cJSON_CreateArray();

					for (int i = 0; i < chat_count; i++){
            			printf("Chat: %s | Last message from %s: %s\n",
						chats[i].chat_name,
						chats[i].last_message_by,
						chats[i].last_message_content);

						cJSON *chat_json = cJSON_CreateObject();
						
						cJSON_AddNumberToObject(chat_json, "chat_id", chats[i].id);
						cJSON_AddStringToObject(chat_json, "chat_name", chats[i].chat_name);
						cJSON_AddStringToObject(chat_json, "last_message_content", chats[i].last_message_content);
						cJSON_AddStringToObject(chat_json, "last_message_type", chats[i].last_message_type);
						cJSON_AddStringToObject(chat_json, "last_message_timestamp", chats[i].last_message_timestamp);
						cJSON_AddStringToObject(chat_json, "last_message_sender", chats[i].last_message_by);

						cJSON_AddItemToArray(chats_array, chat_json);
					}

					cJSON_AddItemToObject(response_json, "chats_array", chats_array);
				} else {
					strcpy(response_text, "Chats couldn't be retreived");
					response_code = 400;
				}
			} else {
				strcpy(response_text, "Wrong format for the parameters");
				response_code = 400;

			}
			
			break;

		case GET_CHAT_MESSAGES:
			cJSON *Item_gcm_chat_id = cJSON_GetObjectItemCaseSensitive(json, "chat_id");
			cJSON *Item_gcm_last_update_timestamp = cJSON_GetObjectItemCaseSensitive(json, "last_update_timestamp")	;

			if (Item_gcm_chat_id && Item_gcm_last_update_timestamp && cJSON_IsNumber(Item_gcm_chat_id) &&
			    (cJSON_IsString(Item_gcm_last_update_timestamp) || cJSON_IsNull(Item_gcm_last_update_timestamp))) {

  				Message messages[MAX_MESSAGES];
				int chat_id = Item_gcm_chat_id->valueint;

			    char *last_update_timestamp = NULL;
    			if (!cJSON_IsNull(Item_gcm_last_update_timestamp)) {
    		    	last_update_timestamp = Item_gcm_last_update_timestamp->valuestring;
    			}

			    int message_count = get_chat_messages(conn, chat_id, last_update_timestamp, messages);
			    if (message_count > -1){
					sprintf(response_text, "%d messages succesfully retreived", message_count);
					response_code = 200;

					cJSON *messages_array = cJSON_CreateArray();

					for (int i = 0; i < message_count; i++){
            			printf("Message from %s %s: %s | sent %s\n",
						messages[i].message_type,
						messages[i].sender_username,
						messages[i].content,
					  	messages[i].created_at
					  );

						cJSON *message_json = cJSON_CreateObject();	
						cJSON_AddNumberToObject(message_json, "message_id", messages[i].message_id);
						cJSON_AddNumberToObject(message_json, "sender_id", messages[i].sender_id);
						cJSON_AddStringToObject(message_json, "sender_username", messages[i].sender_username);
						cJSON_AddStringToObject(message_json, "content", messages[i].content);
						cJSON_AddStringToObject(message_json, "message_type", messages[i].message_type);
						cJSON_AddStringToObject(message_json, "created_at", messages[i].created_at);


						cJSON_AddItemToArray(messages_array, message_json);
					}

					cJSON_AddItemToObject(response_json, "messages_array", messages_array);
				} else {
					strcpy(response_text, "Messages couldn't be retreived");
					response_code = 400;
				}
			} else {
				strcpy(response_text, "Wrong format for the parameters");
				response_code = 400;

			}
			break;

		case GET_CHAT_INFO:{
    		cJSON *chat_id_item = cJSON_GetObjectItemCaseSensitive(json, "chat_id");

	    if (chat_id_item && cJSON_IsNumber(chat_id_item)) {
    	    int chat_id = chat_id_item->valueint;
        	Chat chat;
        	User participants[MAX_PARTICIPANTS];
        	int participant_count;

	        if (get_chat_info(conn, chat_id, &chat, participants, &participant_count) == 0) {
    	        response_code = 200;
        	    sprintf(response_text, "Chat info for ID %d retrieved successfully", chat_id);

            	cJSON_AddNumberToObject(response_json, "chat_id", chat.id);
            	cJSON_AddStringToObject(response_json, "chat_name", chat.chat_name);
				cJSON_AddNumberToObject(response_json, "is_group", chat.is_group);

            	cJSON *participants_array = cJSON_CreateArray();
            	for (int i = 0; i < participant_count; i++) {
                	cJSON *participant_json = cJSON_CreateObject();
                	cJSON_AddNumberToObject(participant_json, "user_id", participants[i].id);
                	cJSON_AddStringToObject(participant_json, "username", participants[i].username);
                	cJSON_AddNumberToObject(participant_json, "is_admin", participants[i].is_admin);
                	cJSON_AddItemToArray(participants_array, participant_json);
            	}

            	cJSON_AddItemToObject(response_json, "participants", participants_array);
        	} else {
            	response_code = 400;
            	sprintf(response_text, "Could not retrieve info for chat ID %d", chat_id);
        	}
    	} else {
        	response_code = 400;
        	strcpy(response_text, "Invalid or missing chat_id");
    	}
    		break;
		}

	case REMOVE_FROM_CHAT:{
    	cJSON *chat_idItem = cJSON_GetObjectItemCaseSensitive(json, "chat_id");
    	cJSON *removed_byItem = cJSON_GetObjectItemCaseSensitive(json, "removed_by");
    	cJSON *participant_idsItem = cJSON_GetObjectItemCaseSensitive(json, "participant_ids");

    	if (chat_idItem && removed_byItem && participant_idsItem && cJSON_IsArray(participant_idsItem)) {
        	int chat_id = chat_idItem->valueint;
        	int removed_by = removed_byItem->valueint;

        	if (!is_user_admin(conn, chat_id, removed_by)) {
            	strcpy(response_text, "Only admins can remove participants.");
            	response_code = 403;
            	break;
        	}

			if(is_group_chat(conn, chat_id) != 1){
            	strcpy(response_text, "Only participants from group chats can be removed.");
            	response_code = 403;
				break;
			}

        	int removed_count = 0;
        	int total_to_remove = cJSON_GetArraySize(participant_idsItem);

        	for (int i = 0; i < total_to_remove; i++) {
            	cJSON *idItem = cJSON_GetArrayItem(participant_idsItem, i);
            	if (cJSON_IsNumber(idItem)) {
                	int user_id = idItem->valueint;
					if (user_id != removed_by){
                		if (remove_from_chat(conn, chat_id, user_id) == 0) {
                    		removed_count++;
						}
                	}
            	}
        	}

        	sprintf(response_text, "Removed %d out of %d participants from chat %d", removed_count, total_to_remove, chat_id);
        	response_code = 200;
    	} else {
        	strcpy(response_text, "Invalid parameters for REMOVE_FROM_CHAT");
        	response_code = 400;
    	}
    	break;
	}

	case EXIT_CHAT:{
		cJSON *chat_idItem = cJSON_GetObjectItemCaseSensitive(json, "chat_id");
		cJSON *user_idItem = cJSON_GetObjectItemCaseSensitive(json, "user_id");

    	if (chat_idItem && user_idItem && cJSON_IsNumber(chat_idItem) && cJSON_IsNumber(user_idItem)) {
        	int chat_id = chat_idItem->valueint;
        	int user_id = user_idItem->valueint;

        	int is_admin = is_user_admin(conn, chat_id, user_id);
        	int participant_count = get_participant_count(conn, chat_id);

        	if (remove_from_chat(conn, chat_id, user_id) != 0) {
            	strcpy(response_text, "Failed to exit chat.");
            	response_code = 400;
            	break;
        	}

        	if (participant_count == 1) {
            	if (delete_chat(conn, chat_id) == 0) {
                	sprintf(response_text, "User %d left chat %d. Chat deleted as last participant.", user_id, chat_id);
                	response_code = 200;
            	} else {
                	strcpy(response_text, "User left, but chat deletion failed.");
                	response_code = 500;
            	}
            	break;
        	}

        	if (is_admin) {
            	int admin_count = get_admin_count(conn, chat_id);
            	if (admin_count == 0) {
                	if (promote_random_participant_to_admin(conn, chat_id) != 0) {
                    	strcpy(response_text, "User left, but failed to promote new admin.");
                    	response_code = 500;
                    	break;
                	}
            	}
        	}

        	sprintf(response_text, "User %d exited chat %d successfully", user_id, chat_id);
        	response_code = 200;

    	} else {
        	strcpy(response_text, "Invalid parameters for EXIT_CHAT");
        	response_code = 400;
    	}
    	break;
	}

		default:
			strcpy(response_text, "UNKNOWN COMMAND\n");
			response_code = 404;
			break;
	}
    
	cJSON_AddStringToObject(response_json, "response_text", response_text);
	cJSON_AddNumberToObject(response_json, "response_code", response_code);


	char *json_string = cJSON_PrintUnformatted(response_json);

    if (json_string == NULL) {
       	printf("Failed to print JSON.\n");
       	cJSON_Delete(response_json);
       	return;
    }

    // Copy JSON string to buffer
	strncpy(response_buffer, json_string, 4096 - 1);
	response_buffer[4096 - 1] = '\0';
	
}




int main() {
	int opt = 1;

	int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) error("socket failed");

	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

	server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(LOCAL_TCP_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) error("bind failed");

    if (listen(server_fd, 3) < 0) error("listen failed");

	signal(SIGCHLD, SIG_IGN);



    MYSQL *conn;

    const char *server = getenv("DB_HOST");
    const char *user = getenv("DB_USER");
    const char *password = getenv("DB_PASS");
    const char *database = getenv("DB_NAME");

    conn = mysql_init(NULL);

    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
        fprintf(stderr, "Connection failed: %s\n", mysql_error(conn));
        exit(1);
    }

	pthread_t udp_thread;
	if (pthread_create(&udp_thread, NULL, udp_daemon, NULL) != 0) {
		perror("No se pudo crear el hilo del daemon UDP");
	}


	while (1){
		if ((client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len)) < 0) {
			perror("accept");
			continue;
		}

		pid_t pid = fork();

		if (pid == 0) {
			close(server_fd);

			MYSQL *child_conn = mysql_init(NULL);
			if (!mysql_real_connect(child_conn, server, user, password, database, 0, NULL, 0)) {
				fprintf(stderr, "Child DB connection failed: %s\n", mysql_error(child_conn));
				exit(1);
			}

			printf("New Client Connection\n");

			char receive_buffer[BUFFER_SIZE];
			char response_buffer[BUFFER_SIZE];
			memset(receive_buffer, 0, BUFFER_SIZE);

			int valread = read(client_socket, receive_buffer, BUFFER_SIZE - 1);
			if (valread < 1){
				perror("read");
				printf("wtf\n");
				close(client_socket);
				mysql_close(child_conn);
				exit(1);
			}

			receive_buffer[valread] = '\0';

			printf("-> Received: %s\n\n", receive_buffer);
			cJSON *json = cJSON_Parse(receive_buffer);
			if (!json) {
				fprintf(stderr, "Invalid JSON received\n");
				close(client_socket);
				mysql_close(child_conn);
				exit(1);
			}

			handle_action(child_conn, json, response_buffer);
			cJSON_Delete(json);

			send(client_socket, response_buffer, strlen(response_buffer), 0);
			printf("<- Sent: %s\n\n", response_buffer);
			close(client_socket);
			mysql_close(child_conn);
			printf("Client Disconnected\n");
			exit(0);
		} else if (pid > 0) {
			close(client_socket);
		} else {
			perror("fork");
			close(client_socket);
		}
	}
    mysql_close(conn);

    return 0;
}


/*
    if (mysql_query(conn, "SHOW TABLES")) {
        fprintf(stderr, "Query failed: %s\n", mysql_error(conn));
        exit(1);
    }

    res = mysql_store_result(conn);

    printf("Tables:\n");
    while ((row = mysql_fetch_row(res)) != NULL) {
        printf("%s\n", row[0]);
    }
*/

void reset_database(MYSQL *conn) {
    const char *queries[] = {
        "SET FOREIGN_KEY_CHECKS = 0;",
        "TRUNCATE TABLE messages;",
        "TRUNCATE TABLE chats;",
        "TRUNCATE TABLE chat_participants;",
        "SET FOREIGN_KEY_CHECKS = 1;"
    };

    for (int i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
        if (mysql_query(conn, queries[i])) {
            fprintf(stderr, "Query failed: %s\nError: %s\n", queries[i], mysql_error(conn));
        }
    }
}

