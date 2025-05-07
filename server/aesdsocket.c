#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>

#define PORT 9000
#define BACKLOG 5
#define MAX_RECV_BUF_LEN 4096

volatile sig_atomic_t exit_flag = 0;
bool daemon_flag = false;

int sock_fd = -1;
int conn_fd = -1;

static void signal_handler(int sig) {
        syslog(LOG_INFO, "%s", "Caught signal and setting exit flag");
        exit_flag = 1;

        // wake anything currently blocked in the `accept()` or `recv()` loops so the main loop can proceed and exit
        if(sock_fd != -1) shutdown(sock_fd, SHUT_RDWR);
        if(conn_fd != -1) shutdown(conn_fd, SHUT_RDWR);
}

struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = 0
};

static int sendall(int fd, const char *buf, size_t len) {
        size_t offset = 0;
        while (offset < len) {
                ssize_t n = send(fd, buf + offset, len - offset, 0);
                if (n < 0) return -1;
                if (n == 0) continue;
                offset += (size_t)n;
        }
        return 0;
}

static int send_file_back(FILE *fp, int conn_fd) {
        fflush(fp);
        fsync(fileno(fp));
        rewind(fp);
        // return the full content of /var/tmp/aesdsocketdata to the client as soon as the received data packet completes
        char send_buffer[MAX_RECV_BUF_LEN];
        size_t bytes_read;
        while ((bytes_read = fread(send_buffer, 1, MAX_RECV_BUF_LEN, fp)) > 0) {
            if (sendall(conn_fd, send_buffer, bytes_read)< 0) {
                return -1;
            }
        }
        fseek(fp, 0, SEEK_END);
        return 0;
}

int main(int argc, char* argv[]) {
        // --- Initialization ---
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        signal(SIGPIPE, SIG_IGN);
        openlog(NULL, LOG_CONS, LOG_USER);
        unlink("/var/tmp/aesdsocketdata");


        // handle daemon mode
        if(argc >= 2 && strcmp(argv[1], "-d") == 0) {
                daemon_flag = true;
        }


        // open a stream socket bound to port 9000, if any of the socket connection steps fail return -1
        sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_fd == -1) {
                perror("Error creating Berkley Stream Socket");
                exit(EXIT_FAILURE);
        }
        int opt = 1;
        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in serv_addr;

        memset(&serv_addr, 0, sizeof(serv_addr));

        serv_addr.sin_family = AF_INET;                 // IPv4
        serv_addr.sin_addr.s_addr = INADDR_ANY;         // accept connections on any interface
        serv_addr.sin_port = htons(PORT);               // `htons()` converts a 16-bit number from host byte order to network byte order

        if(bind(sock_fd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) != 0) {
                // ...
                perror("Error binding Berkley Stream Socket to port 9000");
                close(sock_fd);
                exit(EXIT_FAILURE);
        }

        // listen and accept connections
        listen(sock_fd, BACKLOG);                       // Socket is now actually enabled and passively listening for connections

        // handle daemon mode
        if (daemon_flag == true) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("Error forking");
                close(sock_fd);
                exit(EXIT_FAILURE);
            }
            if (pid > 0) { // Parent exits
                close(sock_fd);
                exit(EXIT_SUCCESS);
            }
            // Child continues
            setsid(); // New session
            chdir("/"); // Change working directory
            // Redirect I/O to /dev/null
            int fd = open("/dev/null", O_RDWR);
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        char ip_string[INET_ADDRSTRLEN];
        char log_string[64];
        char receive_buffer[MAX_RECV_BUF_LEN + 1];
        
        while(!exit_flag) {
                conn_fd = accept(sock_fd, (struct sockaddr*) &client_addr, &addr_size);
                if (conn_fd == -1) {
                        if (exit_flag && errno == EINTR) break;
                        perror("Error accepting connection");
                        continue; // move on to next connection if one is ready
                }

                // log "Accepted connection from xxx" message to syslog where XXX is the IP address of the connected client
                inet_ntop(AF_INET, &client_addr.sin_addr, ip_string, INET_ADDRSTRLEN);
                snprintf(log_string, 64, "Accepted connection from %s", ip_string);
                syslog(LOG_INFO, "%s", log_string);

                // receive data over the connection and append it to /var/temp/aesdsocketdata (if the file doesn't exist, create it)
                FILE *fp = fopen("/var/tmp/aesdsocketdata", "a+");
                if(!fp) {
                        perror("Error opening file");
                        close(conn_fd);
                        continue;
                }
                int msg_len = 0;
                int total_received = 0;
                while((msg_len = recv(conn_fd, receive_buffer + total_received, MAX_RECV_BUF_LEN - total_received, 0)) > 0) {
                        total_received += msg_len;
                        receive_buffer[total_received] = '\0';
                        char *nl;
                        while((nl = memchr(receive_buffer, '\n', total_received))) {
                                size_t line_len = nl - receive_buffer + 1;
                                fwrite(receive_buffer, 1, line_len, fp);
                                if (send_file_back(fp, conn_fd) < 0) {
                                        perror("send_file_back");
                                        break;
                                }
                                size_t remaining = total_received - line_len;
                                memmove(receive_buffer, nl + 1, remaining);
                                total_received = remaining;
                        }
                        if (total_received >= MAX_RECV_BUF_LEN) {
                                fwrite(receive_buffer, 1, total_received, fp);
                                total_received = 0;
                                fflush(fp);
                        }
                }
                if(total_received > 0) {
                        fwrite(receive_buffer, 1, total_received, fp);
                        if (send_file_back(fp, conn_fd) < 0) {
                                perror("send_file_back");
                                break;
                        }
                        total_received = 0;
                }
                if(msg_len < 0) { perror("Error receiving data"); }
                fsync(fileno(fp));
                syslog(LOG_INFO, "Closed connection from %s", ip_string);
                fclose(fp);
                close(conn_fd);
                conn_fd = -1;
        }

        if(conn_fd > 0) { close(conn_fd); }
        syslog(LOG_INFO, "Caught signal, exiting");
        unlink("/var/tmp/aesdsocketdata");
        close(sock_fd);
        closelog();
}
