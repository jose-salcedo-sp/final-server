#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <string.h>

#include "../lib/cjson/cJSON.h"
#include "user_manager.h"

enum ACTIONS{CREATE_USER = 1};

void handle_action(MYSQL *conn, cJSON* json, char* response_buffer){
	char response_text[1024];
	int action, response_code;

	cJSON *response_json = cJSON_CreateObject();

	action = cJSON_GetObjectItem(json, "action") -> valueint;

	switch (action) {
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
			create_user(conn, &newUser);
			response_code = 200;
			
			sprintf(response_text, "User %s with email %s has been stored in the database",newUser.username, newUser.email);

    		cJSON_AddStringToObject(response_json, "response_text", response_text);
			cJSON_AddNumberToObject(response_json, "response_code", response_code);
		break;

		

		default:
			strcpy(response_text, "UNKNOWN COMMAND\n");
			response_code = -1;
		break;
	}


		char *json_string = cJSON_PrintUnformatted(response_json);  // or cJSON_Print for pretty output

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
	char receive_buffer[4096] = "{ \"action\": 1, \"username\": \"exampleUser3\", \"email\": \"user3@example.com\", \"password\": \"securePassword123\" }";
	char response_buffer[4096];


    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

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
