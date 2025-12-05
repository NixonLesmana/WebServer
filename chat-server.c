#include "http-server.h"
#include <string.h>

// Data structures
typedef struct {
    char username[16];
    char message[16];
} Reaction;

typedef struct {
    int id;
    char username[16];
    char message[256];
    char timestamp[20];  // Format: "YYYY-MM-DD HH:MM"
    Reaction reactions[100];
    int reaction_count;
} Chat;

// Global state
Chat chats[100000];
int chat_count = 0;

// HELPER FUNCTIONS
char* get_username(char* post_request);
char* get_message(char* post_request);
char* get_id(char* react_request);
char* url_decode(char* str);
void format_chat_line(uint32_t id, char* user, char* message, char* timestamp,
                     uint32_t num_reactions, char* buffer);

// MAIN FUNCTIONS
void request_handler(char* buffer, int client);
uint8_t add_chat(char* username, char* message);
uint8_t add_reaction(char* username, char* message, int id);
void respond_with_chats(int client);
void handle_post(char* path, int client);
void handle_react(char* path, int client);
void send_error(int client, int code, char* message);

int main(int argc, char* argv[]) {
    int port = 0; // 0 means auto-select
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    start_server(request_handler, port);
    return 0;
}

void request_handler(char* buffer, int client) {
    // Find "GET " in the request
    char* get_ptr = strstr(buffer, "GET ");
    if (get_ptr == NULL) {
        return;
    }
    
    // Move past "GET "
    char* path_start = get_ptr + 4;
    
    // Check which endpoint
    if (strncmp(path_start, "/chats", 6) == 0) {
        respond_with_chats(client);
    }
    else if (strncmp(path_start, "/post?", 6) == 0) {
        handle_post(path_start, client);
    }
    else if (strncmp(path_start, "/react?", 7) == 0) {
        handle_react(path_start, client);
    }
}

uint8_t add_chat(char* username, char* message) {
    if (chat_count >= 100000) {
        return 0; // Error: too many chats
    }
    
    Chat* new_chat = &chats[chat_count];
    new_chat->id = chat_count + 1;
    
    // Copy username and message
    strncpy(new_chat->username, username, 15);
    new_chat->username[15] = '\0';
    strncpy(new_chat->message, message, 255);
    new_chat->message[255] = '\0';
    
    // Get current timestamp
    time_t now = time(NULL);
    struct tm* local = localtime(&now);
    strftime(new_chat->timestamp, 20, "%Y-%m-%d %H:%M:%S", local);
    
    new_chat->reaction_count = 0;
    chat_count++;
    
    return 1; // Success
}

uint8_t add_reaction(char* username, char* message, int id) {
    // Check if id is valid
    if (id < 1 || id > chat_count) {
        return 0; // Error: invalid id
    }
    
    Chat* chat = &chats[id - 1];
    
    if (chat->reaction_count >= 100) {
        return 0; // Error: too many reactions
    }
    
    Reaction* new_reaction = &chat->reactions[chat->reaction_count];
    strncpy(new_reaction->username, username, 15);
    new_reaction->username[15] = '\0';
    strncpy(new_reaction->message, message, 15);
    new_reaction->message[15] = '\0';
    
    chat->reaction_count++;
    return 1; // Success
}

void respond_with_chats(int client) {
    const char* header = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n";
    
    send(client, header, strlen(header), 0);
    
    // Send each chat
    for (int i = 0; i < chat_count; i++) {
        char buffer[4096];
        Chat* chat = &chats[i];
        
        format_chat_line(chat->id, chat->username, chat->message, 
                        chat->timestamp, chat->reaction_count, buffer);
        
        send(client, buffer, strlen(buffer), 0);
        
        // Send reactions
        for (int j = 0; j < chat->reaction_count; j++) {
            char reaction_line[256];
            snprintf(reaction_line, sizeof(reaction_line), 
                    "                        (%s) %s\n",
                    chat->reactions[j].username, 
                    chat->reactions[j].message);
            send(client, reaction_line, strlen(reaction_line), 0);
        }
    }
}

void handle_post(char* path, int client) {
    // Extract username
    char* username = get_username(path);
    if (username == NULL) {
        send_error(client, 400, "Missing user parameter");
        return;
    }
    
    // Decode username
    username = url_decode(username);
    
    // Check username length
    if (strlen(username) > 15) {
        send_error(client, 400, "Username too long");
        return;
    }
    
    // Extract message
    char* message = get_message(path);
    if (message == NULL) {
        send_error(client, 400, "Missing message parameter");
        return;
    }
    
    // Decode message
    message = url_decode(message);
    
    // Check message length
    if (strlen(message) > 255) {
        send_error(client, 400, "Message too long");
        return;
    }
    
    // Add chat
    if (!add_chat(username, message)) {
        send_error(client, 500, "Failed to add chat");
        return;
    }
    
    // Respond with all chats
    respond_with_chats(client);
}

void handle_react(char* path, int client) {
    // Extract username
    char* username = get_username(path);
    if (username == NULL) {
        send_error(client, 400, "Missing user parameter");
        return;
    }
    
    username = url_decode(username);
    
    if (strlen(username) > 15) {
        send_error(client, 400, "Username too long");
        return;
    }
    
    // Extract message
    char* message = get_message(path);
    if (message == NULL) {
        send_error(client, 400, "Missing message parameter");
        return;
    }
    
    message = url_decode(message);
    
    if (strlen(message) > 15) {
        send_error(client, 400, "Reaction message too long");
        return;
    }
    
    // Extract id
    char* id_str = get_id(path);
    if (id_str == NULL) {
        send_error(client, 400, "Missing id parameter");
        return;
    }
    
    int id = atoi(id_str);
    
    // Add reaction
    if (!add_reaction(username, message, id)) {
        send_error(client, 400, "Failed to add reaction");
        return;
    }
    
    // Respond with all chats
    respond_with_chats(client);
}

void send_error(int client, int code, char* message) {
    char response[256];
    snprintf(response, sizeof(response),
            "HTTP/1.1 %d Error\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "%s\n", code, message);
    send(client, response, strlen(response), 0);
}

// Helper functions from psets

char* get_username(char* post_request) {
    char* user_ptr = strstr(post_request, "user=");
    if (user_ptr == NULL) {
        return NULL;
    }
    
    user_ptr += 5;  // Move past "user="
    
    char* end_ptr = strchr(user_ptr, '&');
    if (end_ptr == NULL) {
        end_ptr = strchr(user_ptr, ' ');
    }
    
	static char username[256];
	size_t len = end_ptr ? (size_t)(end_ptr - user_ptr) : strlen(user_ptr);
	strncpy(username, user_ptr, len);
	username[len] = '\0';

    return username;
}

char* get_message(char* post_request) {
    char* msg_ptr = strstr(post_request, "message=");
    if (msg_ptr == NULL) {
        return NULL;
    }
    
    msg_ptr += 8;  // Move past "message="
    
    char* end_ptr = strchr(msg_ptr, '&');
    if (end_ptr == NULL) {
        end_ptr = strchr(msg_ptr, ' ');
    } 
    static char message[512];
	size_t len = end_ptr ? (size_t)(end_ptr - msg_ptr) : strlen(msg_ptr);
	strncpy(message, msg_ptr, len);
	message[len] = '\0';

    return message;
}

char* get_id(char* react_request) {
    char* id_ptr = strstr(react_request, "id=");
    if (id_ptr == NULL) {
        return NULL;
    }
    
    id_ptr += 3;  // Move past "id="
    
    char* end_ptr = id_ptr;
    while (*end_ptr != '&' && *end_ptr != ' ' && *end_ptr != '\0') {
        end_ptr++;
    }
    
	static char id[32];
	size_t len = (size_t)(end_ptr - id_ptr);
	strncpy(id, id_ptr, len);
	id[len] = '\0';

    return id;
}

char* url_decode(char* str) {
    char* src = str;
    char* dst = str;
    
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            char* endptr;
            long int val = strtol(hex, &endptr, 16);
            
            if (endptr == hex + 2) {
                *dst++ = (char)val;
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    
    *dst = '\0';
    return str;
}

void format_chat_line(uint32_t id, char* user, char* message, char* timestamp,
                     uint32_t num_reactions, char* buffer) {

	(void)num_reactions;
    sprintf(buffer, "[#%u %s] %15s: %s\n", id, timestamp, user, message);
}
