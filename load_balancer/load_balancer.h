#include "../dbg.h"
#include "../lib/cjson/cJSON.h"
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 4096
#define UUIDv7_SIZE 32
#define MAX_CONCURRENT_CHATS 100
#define MAX_USERS_PER_CHAT 10

typedef char UUID[UUIDv7_SIZE];

typedef struct {
  UUID id;
  pid_t users[MAX_USERS_PER_CHAT]; // subscribed user pids
  int user_count; // how many users are currently subscribed to the chat
} Chat;

int subscribe_user_to_chat(pid_t pid, UUID chat_id);
int unsubscribe_chat(UUID chat_id);

typedef enum {
  LOGIN = 0,
  LOGOUT = 1,
  REGISTER = 2,
  MSGSEND = 3,
  CHATJOIN = 5
} ACTIONS;
