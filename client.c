#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_REQUEST_SIZE 2048
#define MAX_RESPONSE_SIZE 8192
#define DEFAULT_PORT 80

// Define DEBUG to enable debug prints
#define DEBUG 0

// Macro for conditional debug printing
#if DEBUG
    #define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[DEBUG] " fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) // No-operation
#endif

// Struct to encapsulate URL details
typedef struct {
    char host[256];
    char path[1024];
    int port;
} URLDetails;

// Function declarations
void parse_command_line(int argc, char *argv[], char **url, char *parameters);
void parse_url(const char *url, URLDetails *details);
void construct_request(const URLDetails *details, const char *parameters, char *request);
int connect_to_server(const URLDetails *details);
void send_request(int sock, const char *request);
void receive_response(int sock, char *response, int *response_size);
void handle_redirect(const char *response, char *redirect_url);
void print_usage_and_exit();

int main(int argc, char *argv[]) {
    DEBUG_PRINT("Starting client program.\n");

    char *url = NULL;
    char parameters[1024] = "";
    char request[MAX_REQUEST_SIZE], response[MAX_RESPONSE_SIZE];
    int response_size;

    // Parse command-line arguments
    parse_command_line(argc, argv, &url, parameters);

    // Parse URL
    URLDetails details;
    parse_url(url, &details);

    // Construct HTTP request
    construct_request(&details, parameters, request);
    DEBUG_PRINT("HTTP request =\n%s\nLEN = %ld\n", request, strlen(request));

    // Connect to server
    int socket = connect_to_server(&details);

    // Send HTTP request
    send_request(socket, request);

    // Receive HTTP response
    receive_response(socket, response, &response_size);
    printf("%s\n", response);
    printf("\n  Total received response bytes: %d\n", response_size);

    // Handle redirects if necessary
    if (strncmp(response, "HTTP/1.1 3", 10) == 0) {
        char redirect_url[1024];
        handle_redirect(response, redirect_url);
        if (strlen(redirect_url) > 0) {
            close(socket);
            DEBUG_PRINT("Redirecting to: %s\n", redirect_url);
            strcpy(argv[argc - 1], redirect_url);
            execv(argv[0], argv); // Recursively call the program with the new URL
        }
    }

    close(socket);
    DEBUG_PRINT("Client program finished.\n");
    return 0;
}

void parse_command_line(int argc, char *argv[], char **url, char *parameters) {
    DEBUG_PRINT("Parsing command-line arguments.\n");

    if (argc < 2) {
        DEBUG_PRINT("Insufficient arguments provided.\n");
        print_usage_and_exit();
    }

    int found_r = 0;  // Indicates if the "-r" option was found
    int param_count = 0; // Number of parameters specified after "-r"

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0) {
            found_r = 1;

            // Validate that a number follows "-r"
            if (i + 1 >= argc || !isdigit(argv[i + 1][0])) {
                DEBUG_PRINT("Has to be a number after -r.\n");
                print_usage_and_exit();
            }

            param_count = atoi(argv[++i]); // Get the number of parameters
            DEBUG_PRINT("Found -r option with %d parameters.\n", param_count);

            // Validate and collect parameters
            for (int j = 0; j < param_count; ++j) {
                if (i + 1 >= argc || strchr(argv[i + 1], '=') == NULL) {
                    DEBUG_PRINT("Parameter %d is not in the correct format: name=value.\n", j + 1);
                    print_usage_and_exit();
                }
                strcat(parameters, argv[++i]);
                if (j < param_count - 1) strcat(parameters, "&");
                DEBUG_PRINT("Added parameter: %s\n", parameters);
            }
        } else if (!found_r || i == argc - 1) {
            // The URL should be the last argument
            if (*url != NULL) {
                DEBUG_PRINT("Too many parameters or URL is misplaced.\n");
                print_usage_and_exit();
            }
            *url = argv[i];
            DEBUG_PRINT("Found URL: %s\n", *url);
        } else {
            DEBUG_PRINT("Unexpected argument: %s\n", argv[i]);
            print_usage_and_exit();
        }
    }

    if (!*url || strncmp(*url, "http://", 7) != 0) {
        DEBUG_PRINT("Invalid or missing URL: %s\n", *url ? *url : "(null)");
        print_usage_and_exit();
    }

    DEBUG_PRINT("Final URL: %s\n", *url);
    DEBUG_PRINT("Final Parameters: %s\n", parameters[0] ? parameters : "(none)");
}

void parse_url(const char *url, URLDetails *details) {
    DEBUG_PRINT("Parsing URL: %s\n", url);

    const char *start = url + 7; // Skip "http://"
    const char *colon = strchr(start, ':');
    const char *slash = strchr(start, '/');

    if (colon && (!slash || colon < slash)) {
        // Host with a port specified
        strncpy(details->host, start, colon - start);
        details->host[colon - start] = '\0';
        details->port = atoi(colon + 1);
        DEBUG_PRINT("Host: %s, Port: %d (custom)\n", details->host, details->port);
    } else {
        // Host without a port
        details->port = DEFAULT_PORT; // Default to port 80
        if (slash) {
            strncpy(details->host, start, slash - start);
            details->host[slash - start] = '\0';
        } else {
            strncpy(details->host, start, strlen(start));
            details->host[strlen(start)] = '\0';
        }
        DEBUG_PRINT("Host: %s, Port: %d (default)\n", details->host, details->port);
    }

    if (slash) {
        strcpy(details->path, slash);
    } else {
        strcpy(details->path, "/");
    }

    DEBUG_PRINT("Path: %s\n", details->path);
}

void construct_request(const URLDetails *details, const char *parameters, char *request) {
    DEBUG_PRINT("Constructing HTTP request.\n");

    if (strlen(parameters) > 0) {
        sprintf(request, "GET %s?%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                details->path, parameters, details->host);
    } else {
        sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                details->path, details->host);
    }

    DEBUG_PRINT("Constructed Request:\n%s\n", request);
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

void receive_response(int sock, char *response, int *response_size) {
    DEBUG_PRINT("Receiving HTTP response.\n");

    *response_size = recv(sock, response, MAX_RESPONSE_SIZE - 1, 0);
    if (*response_size < 0) {
        perror("recv");
        close(sock);
        exit(EXIT_FAILURE);
    }
    response[*response_size] = '\0';

    DEBUG_PRINT("Received response (%d bytes).\n", *response_size);
}

void handle_redirect(const char *response, char *redirect_url) {
    DEBUG_PRINT("Handling redirect in response.\n");

    const char *location = strstr(response, "Location: ");
    if (location) {
        location += 10; // Skip "Location: "
        const char *end = strchr(location, '\r');
        if (end) {
            strncpy(redirect_url, location, end - location);
            redirect_url[end - location] = '\0';
            DEBUG_PRINT("Redirect URL: %s\n", redirect_url);
        }
    } else {
        DEBUG_PRINT("No redirect location found.\n");
    }
}

void print_usage_and_exit() {
    DEBUG_PRINT("Printing usage and exiting.\n");
    fprintf(stderr, "Usage: client [-r n <pr1=value1 pr2=value2 ...>] <URL>\n");
    exit(EXIT_FAILURE);
}
