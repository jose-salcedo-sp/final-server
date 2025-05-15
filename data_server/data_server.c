#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <string.h>

#include "../lib/cjson/cJSON.h"
#include "user_manager.h"
#include "chat_manager.h"

enum ACTIONS{VALIDATE_USER = 0, CREATE_USER = 2, GET_USER_INFO = 3, CREATE_CHAT = 4, ADD_TO_GROUP_CHAT = 5, SEND_MESSAGE = 6};

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

				if (0 < participant_count && participant_count < MAX_PARTICIPANTS - (chat.is_group ? 1 : 9)){
					chat.chat_name = chat_nameItem -> valuestring;
					chat.created_by = created_byItem ->valueint;

					if (create_chat(conn, &chat) == 0){
						participants[0] = chat.created_by;

						for (int i = 0; i < participant_count; i++) {
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
				message.content = Item_sm_content -> valuestring;
				message.message_type = Item_sm_message_type -> valuestring;


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
	//char receive_buffer[4096] = "{ \"action\": 2, \"username\": \"ehinojosa\", \"email\": \"ehinojosa@example.com\", \"password\": \"sdahsdiuh\" }";
	//char receive_buffer[4096] = "{ \"action\": 3, \"key\": \"exampleUser3\"}";
	//char receive_buffer[4096] = "{\"action\": 4, \"is_group\": true, \"chat_name\": \"New Chat\", \"created_by\": 7, \"participant_ids\": [4,6,9,10]}";
	//char receive_buffer[4096] = "{\"action\": 5, \"chat_id\": 1, \"added_by\": 7, \"participant_ids\": [12]}";
	char receive_buffer[4096] = "{\"action\": 6, \"chat_id\":1, \"sender_id\": 12, \"content\": \"Hello World!\", \"message_type\": \"user\"}";
	char response_buffer[4096];


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


	cJSON *json = cJSON_Parse(receive_buffer);


	printf("Flag\n");

	handle_action(conn, json, response_buffer);
	
	printf("\nResponse:\n%s\n",response_buffer);

	//mysql_free_result(res);
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
