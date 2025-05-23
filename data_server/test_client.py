import socket
import json
import time

SERVER_IP = '127.0.0.1'
SERVER_PORT = 8080
BUFFER_SIZE = 4096

def send_request(request_data):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((SERVER_IP, SERVER_PORT))
            s.sendall(json.dumps(request_data).encode())
            response = s.recv(BUFFER_SIZE)
            print(f"→ Request: {request_data}")
            print(f"← Response: {json.loads(response.decode())}\n")
    except Exception as e:
        print(f"Error: {e}\n")

if __name__ == "__main__":
    test_cases = [
        {
            "action": 2,
            "username": "user1",
            "email": "user1@example.com",
            "password": "pass1"
        },
        {
            "action": 2,
            "username": "user2",
            "email": "user2@example.com",
            "password": "pass2"
        },
                {
            "action": 2,
            "username": "user3",
            "email": "user3@example.com",
            "password": "pass3"
        },
        {
            "action": 0,
            "key": "user1"
        },
        {
            "action": 3,
            "key": "user2"
        },
        {
            "action": 4,
            "is_group": False,
            "chat_name": "DM Chat",
            "created_by": 1,
            "participant_ids": [2]
        },
                {
            "action": 4,
            "is_group": True,
            "chat_name": "Group Chat :)",
            "created_by": 1,
            "participant_ids": [2, 3]
        },
        {
            "action": 6,
            "chat_id": 2,
            "sender_id": 1,
            "content": "Hello again",
            "message_type": "text"
        },
        {
            "action": 8,
            "chat_id": 2,
            "last_update_timestamp": None
        },
        {
            "action": 9,
            "chat_id": 2,
        },
        #{
         #   "action":10,
          #  "chat_id": 2,
           # "removed_by":1,
            #"participant_ids":[1,2,3]
        #},
        {
            "action":11,
            "chat_id": 2,
            "user_id":1,
        },
        {
            "action": 9,
            "chat_id": 2,
        },
    ]

    for request in test_cases:
        send_request(request)
        time.sleep(0.5)  # Optional: avoid overloading server
