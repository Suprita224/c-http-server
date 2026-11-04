#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8080
#define QUEUE_SIZE 64
#define DEFAULT_THREADS 4
#include <time.h>

FILE *log_file;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

// Writes one line to access.log (safe to call from many threads)
void log_request(const char *method, const char *path, int status) {
    char ts[32];
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    pthread_mutex_lock(&log_lock);
    fprintf(log_file, "%s %s %s %d\n", ts, method, path, status);
    fflush(log_file);
    pthread_mutex_unlock(&log_lock);
}
// ---------- Sending responses ----------

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
    const char *ext = strrchr(path, '.');
    if (ext == NULL) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0)  return "image/jpeg";
    if (strcmp(ext, ".txt") == 0)  return "text/plain";
    return "application/octet-stream";
}

// ---------- Handling ONE visitor ----------

void handle_client(int client_fd) {
    char request[4096];
    int n = read(client_fd, request, sizeof(request) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    request[n] = '\0';

    char method[16], path[512];
    if (sscanf(request, "%15s %511s", method, path) != 2) {
        close(client_fd);
        return;
    }
    printf("Request: %s %s\n", method, path);

    // Only GET is supported
    if (strcmp(method, "GET") != 0) {
        const char *msg = "<h1>405 Method Not Allowed</h1>";
        send_response(client_fd, "405 Method Not Allowed", "text/html", msg, strlen(msg));
        log_request(method, path, 405);
        close(client_fd);
        return;
    }

    // Security: block attempts to escape the www folder
    if (strstr(path, "..") != NULL) {
        const char *msg = "<h1>403 Forbidden</h1>";
        send_response(client_fd, "403 Forbidden", "text/html", msg, strlen(msg));
        log_request(method, path, 403);
        close(client_fd);
        return;
    }

    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    char filepath[600];
    snprintf(filepath, sizeof(filepath), "www%s", path);

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        const char *msg = "<h1>404 Not Found</h1>";
        send_response(client_fd, "404 Not Found", "text/html", msg, strlen(msg));
        log_request(method, path, 404);
        close(client_fd);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    send_response(client_fd, "200 OK", get_mime_type(filepath), data, size);
    log_request(method, path, 200);
    free(data);
    close(client_fd);
}

// ---------- The waiting list (queue) ----------

int queue[QUEUE_SIZE];
int q_head = 0, q_tail = 0, q_count = 0;

pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t q_not_full = PTHREAD_COND_INITIALIZER;

// The main thread puts a visitor on the list
void queue_push(int fd) {
    pthread_mutex_lock(&q_lock);
    while (q_count == QUEUE_SIZE) {
        pthread_cond_wait(&q_not_full, &q_lock);
    }
    queue[q_tail] = fd;
    q_tail = (q_tail + 1) % QUEUE_SIZE;
    q_count++;
    pthread_cond_signal(&q_not_empty);
    pthread_mutex_unlock(&q_lock);
}

// A worker takes the next visitor from the list
int queue_pop(void) {
    pthread_mutex_lock(&q_lock);
    while (q_count == 0) {
        pthread_cond_wait(&q_not_empty, &q_lock);
    }
    int fd = queue[q_head];
    q_head = (q_head + 1) % QUEUE_SIZE;
    q_count--;
    pthread_cond_signal(&q_not_full);
    pthread_mutex_unlock(&q_lock);
    return fd;
}

// Every worker thread runs this forever
void *worker(void *arg) {
    (void)arg;
    while (1) {
        int fd = queue_pop();
        handle_client(fd);
    }
    return NULL;
}

// ---------- main ----------

int main(int argc, char **argv) {
    int num_threads = DEFAULT_THREADS;

    int opt;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        if (opt == 't') num_threads = atoi(optarg);
    }
    if (num_threads < 1) num_threads = 1;

    signal(SIGPIPE, SIG_IGN);
    
    log_file = fopen("access.log", "a");
    if (log_file == NULL) { perror("log file"); exit(1); }

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

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    printf("Server running at http://localhost:%d with %d threads\n", PORT, num_threads);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { perror("accept failed"); continue; }
        queue_push(client_fd);
    }

    return 0;
}


    
       
