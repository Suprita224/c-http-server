#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8080

// Sends a full HTTP response: header first, then the body
void send_response(int client_fd, const char *status,
                   const char *content_type, const char *body, long body_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);

    write(client_fd, header, header_len);
    write(client_fd, body, body_len);
}
// Picks the Content-Type based on the file extension
const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');   // finds the last '.' in the name
    if (ext == NULL) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0)  return "image/jpeg";
    if (strcmp(ext, ".txt") == 0)  return "text/plain";
    return "application/octet-stream";
}
// Handles ONE visitor from start to finish
void handle_client(int client_fd) {
    // A: read the request
    char request[4096];
    int n = read(client_fd, request, sizeof(request) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    request[n] = '\0';

    // B: pull the method and path out of the first line
    char method[16], path[512];
    if (sscanf(request, "%15s %511s", method, path) != 2) {
        close(client_fd);
        return;
    }
    printf("Request: %s %s\n", method, path);
        // We only support GET for now
    if (strcmp(method, "GET") != 0) {
        const char *msg = "<h1>405 Method Not Allowed</h1>";
        send_response(client_fd, "405 Method Not Allowed", "text/html", msg, strlen(msg));
        close(client_fd);
        return;
    }
        // Security: block attempts to escape the www folder
    if (strstr(path, "..") != NULL) {
        const char *msg = "<h1>403 Forbidden</h1>";
        send_response(client_fd, "403 Forbidden", "text/html", msg, strlen(msg));
        close(client_fd);
        return;
    }

    // C: "/" means the home page
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    // D: build the file location, e.g. "www" + "/index.html"
    char filepath[600];
    snprintf(filepath, sizeof(filepath), "www%s", path);

    // E: try to open the file
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        const char *msg = "<h1>404 Not Found</h1>";
        send_response(client_fd, "404 Not Found", "text/html", msg, strlen(msg));
        close(client_fd);
        return;
    }

    // F: find the file size, then read the whole file into memory
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    // G: send it, then clean up
    send_response(client_fd, "200 OK",get_mime_type(filepath), data, size);
    free(data);
    close(client_fd);
}

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket failed"); exit(1); }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); exit(1);
    }
    if (listen(server_fd, 128) < 0) {
        perror("listen failed"); exit(1);
    }

    printf("Server running at http://localhost:%d\n", PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { perror("accept failed"); continue; }
        handle_client(client_fd);
    }

    return 0;
}
