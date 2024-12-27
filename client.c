#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define DEFAULT_PORT 80
#define CHUNK_SIZE 1024

// Define DEBUG to enable debug prints
#define DEBUG 1

// Macro for conditional debug printing
#if DEBUG
    #define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[DEBUG] " fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) // No-operation
#endif

typedef struct {
    char *host;
    char *path;
    int port;
} URLDetails;

void parse_command_line(int argc, char *argv[], char **url, char **parameters);
void parse_url(const char *url, URLDetails *details);
char *construct_request(const URLDetails *details, const char *parameters);
int connect_to_server(const URLDetails *details);
void send_request(int sock, const char *request);
char *receive_response(int sock, int *response_size);
void handle_redirect(const char *response, char **redirect_url);
void print_usage_and_exit();

int main(int argc, char *argv[]) {
    DEBUG_PRINT("Starting client program.\n");

    char *url = NULL;
    char *parameters = NULL;
    int response_size;

    parse_command_line(argc, argv, &url, &parameters);

    URLDetails details = {0};
    parse_url(url, &details);

    char *request = construct_request(&details, parameters);
    DEBUG_PRINT("HTTP request =\n%s\n", request);

    int socket = connect_to_server(&details);
    send_request(socket, request);

    char *response = receive_response(socket, &response_size);
    printf("%s\n", response);
    printf("\n  Total received response bytes: %d\n", response_size);

    if (strncmp(response, "HTTP/1.1 3", 10) == 0) {
        char *redirect_url = NULL;
        handle_redirect(response, &redirect_url);
        if (redirect_url) {
            close(socket);
            DEBUG_PRINT("Redirecting to: %s\n", redirect_url);
            free(response);
            free(request);
            free(details.host);
            free(details.path);
            argv[argc - 1] = redirect_url;
            execv(argv[0], argv);
        }
    }

    close(socket);
    free(response);
    free(request);
    free(details.host);
    free(details.path);
    free(parameters);

    DEBUG_PRINT("Client program finished.\n");
    return 0;
}

void parse_command_line(int argc, char *argv[], char **url, char **parameters) {
    DEBUG_PRINT("Parsing command-line arguments.\n");

    if (argc < 2) {
        print_usage_and_exit();
    }

    int found_r = 0, param_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0) {
            found_r = 1;
            if (i + 1 >= argc || !isdigit(argv[i + 1][0])) {
                print_usage_and_exit();
            }
            param_count = atoi(argv[++i]);

            size_t params_size = 1;
            *parameters = calloc(1, params_size);
            for (int j = 0; j < param_count; ++j) {
                if (i + 1 >= argc || !strchr(argv[i + 1], '=')) {
                    print_usage_and_exit();
                }
                size_t len = strlen(argv[++i]) + 1;
                *parameters = realloc(*parameters, params_size + len);
                strcat(*parameters, argv[i]);
                if (j < param_count - 1) strcat(*parameters, "&");
                params_size += len;
            }
        } else if (!found_r || i == argc - 1) {
            if (*url != NULL) {
                print_usage_and_exit();
            }
            *url = argv[i];
        } else {
            print_usage_and_exit();
        }
    }

    if (!*url || strncmp(*url, "http://", 7) != 0) {
        print_usage_and_exit();
    }

    DEBUG_PRINT("Final URL: %s\n", *url);
    DEBUG_PRINT("Final Parameters: %s\n", *parameters ? *parameters : "(none)");
}

void parse_url(const char *url, URLDetails *details) {
    DEBUG_PRINT("Parsing URL: %s\n", url);

    const char *start = url + 7;
    const char *colon = strchr(start, ':');
    const char *slash = strchr(start, '/');

    if (colon && (!slash || colon < slash)) {
        details->host = strndup(start, colon - start);
        details->port = atoi(colon + 1);
    } else {
        details->port = DEFAULT_PORT;
        if (slash) {
            details->host = strndup(start, slash - start);
        } else {
            details->host = strdup(start);
        }
    }

    if (slash) {
        details->path = strdup(slash);
    } else {
        details->path = strdup("/");
    }

    DEBUG_PRINT("Host: %s, Port: %d, Path: %s\n", details->host, details->port, details->path);
}

char *construct_request(const URLDetails *details, const char *parameters) {
    DEBUG_PRINT("Constructing HTTP request.\n");

    size_t request_size = strlen(details->path) + strlen(details->host) + 64;
    if (parameters) request_size += strlen(parameters) + 1;

    char *request = malloc(request_size);
    if (parameters) {
        snprintf(request, request_size, "GET %s?%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 details->path, parameters, details->host);
    } else {
        snprintf(request, request_size, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 details->path, details->host);
    }

    DEBUG_PRINT("Constructed Request:\n%s\n", request);
    return request;
}

int connect_to_server(const URLDetails *details) {
    DEBUG_PRINT("Connecting to server: %s:%d\n", details->host, details->port);

    int sock;
    struct sockaddr_in server_addr;
    struct hostent *server;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if ((server = gethostbyname(details->host)) == NULL) {
        herror("gethostbyname");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(details->port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }

    DEBUG_PRINT("Connected to server.\n");
    return sock;
}

void send_request(int sock, const char *request) {
    DEBUG_PRINT("Sending HTTP request.\n");

    if (send(sock, request, strlen(request), 0) < 0) {
        perror("send");
        close(sock);
        exit(EXIT_FAILURE);
    }

    DEBUG_PRINT("Request sent successfully.\n");
}


char *receive_response(int sock, int *response_size) {
    DEBUG_PRINT("Receiving HTTP response.\n");

    char chunk[CHUNK_SIZE];
    char *response = NULL;
    size_t total_size = 0;
    ssize_t received = 0;

    do {
        // null-terminate the chunk buffer
        memset(chunk, 0, sizeof(chunk));

        // read the response in chunks
        received = read(sock, chunk, CHUNK_SIZE);

        // if read failed, exit
        if (received < 0) {
            perror("Error receiving response");
            free(response);
            close(sock);
            return NULL;
        }

        // If received 0 bytes, the connection was closed
        if (received == 0) {
            DEBUG_PRINT("Response received successfully\n");
            break;
        }

        // reallocate memory for the response buffer
        response = realloc(response, total_size + received + 1);
        if (!response) {
            perror("Memory allocation failed");
            close(sock);
            return NULL;
        }

        // copy the chunk to the response buffer
        memcpy(response + total_size, chunk, received);
        total_size += received;

    } while (received > 0);

    // Null-terminate the response string
    if (response) {
        response[total_size] = '\0';
    }

    *response_size = total_size;
    DEBUG_PRINT("Received response (%ld bytes).\n", total_size);

    return response;
}

void handle_redirect(const char *response, char **redirect_url) {
    DEBUG_PRINT("Handling redirect in response.\n");

    const char *location = strstr(response, "Location: ");
    if (location) {
        location += 10;
        const char *end = strchr(location, '\r');
        if (end) {
            *redirect_url = strndup(location, end - location);
            DEBUG_PRINT("Redirect URL: %s\n", *redirect_url);
        }
    } else {
        *redirect_url = NULL;
    }
}

void print_usage_and_exit() {
    fprintf(stderr, "Usage: client [-r n <pr1=value1 pr2=value2 ...>] <URL>\n");
    exit(EXIT_FAILURE);
}
