/**
 *  @file aesdcocket.c
 *  @brief A simple socket server application that listens on port 9000,
 *  accepts connections, receives data, appends it to a file, and sends 
 *  the file content back to the client.
 *
 *  This program demonstrates basic socket programming, signal handling, 
 *  and an optional daemon mode.
 */

// Standard library headers
#include <arpa/inet.h>   /**< @brief Provides definitions for internet operations (e.g., `inet_ntop()`). */
#include <stdbool.h>     /**< @brief Provides a boolean type and values (`true`, `false`). */
#include <errno.h>       /**< @brief Provides definitions for error numbers (e.g., `EINTR`). */
#include <signal.h>      /**< @brief Provides definitions for signal handling (e.g., `sigaction`, `SIGINT`, `SIGTERM`). */
#include <stdio.h>       /**< @brief Provides standard input/output functions (e.g. `printf()`, `fopen()`, `fclose()`). */
#include <stdlib.h>      /**< @brief Provides Provides general utility functions (e.g., `exit()`, `malloc()`, `free()`). */
#include <string.h>      /**< @brief Provides string manipulation functions (e.g., `strcmp()`, `memset()`, `memchr()`). */
#include <sys/socket.h>  /**< @brief Provides definitions for socket programming (e.g., `socket()`, `bind()`, `listen()`, `accept()`, `send()`, `recv()`). */
#include <syslog.h>      /**< @brief Provides functions for logging messages to the system logger (e.g., `openlog()`, `syslog()`, `closelog()`). */
#include <unistd.h>      /**< @brief Provides POSIX operating system API functions (e.g., `close()`, `fork()`, `setsid()`, `chdir()`, `unlink()`, `fsync()`). */
#include <fcntl.h>       /**< @brief Provides functions for file control options (e.g., `open()`, `O_RDWR`). */

// --- Macro Definitions ---
#define PORT 9000                               /**< @brief The port number on which the server will listen for incoming connections. */
#define BACKLOG 5                               /**< @brief The maximum length to which the queue of pending connections for sock_fd may grow. */
#define MAX_RECV_BUF_LEN 4096                   /**< @brief The maximum size (in bytes) of the in-memory buffer used to receive data from clients. */
#define DATA_FILE "/var/tmp/aesdsocketdata"     /**< @brief The path to the file where received data is ultimately stored. */

// --- Global Variables ---
/**
 * @var exit_flag
 * @brief A flag indicating whether teh server should terminate.
 * @details This flag is declared as `volatile sig_atomic_t` to ensure safe access 
 * from signal handlers and the main program loop. It is set to 1
 * when a `SIGINT` or `SIGTERM` signal is caught.
 */
volatile sig_atomic_t exit_flag = 0;

/**
 * @var daemon_flag
 * @brief A flag indicating whether the server should run in daemon mode.
 * @details This flag is set to `true` if the program is started with the "-d" command-line argument.
 */
bool daemon_flag = false;

/**
 * @var sock_fd
 * @brief The file descriptor for the listening socket.
 * @details This socket is used to accept incoming client connections.
 * Initialized to -1 to indicate it's not yet open.
 */
int sock_fd = -1;

/**
 * @var conn_fd
 * @brief The file descriptor for the connected client socket.
 * @details This socket is used for communication with an accepted client.
 * Initialized to -1 to indicate no active connection.
 */
int conn_fd = -1;

// --- Function Declarations ---
static void signal_handler(int sig);
static int sendall(int fd, const char *buf, size_t len);
static int send_file_back(FILE *fp, int client_fd); // Renamed `conn_fd` to `client_fd` for clarity in this function's scope

/**
 * @struct sigaction sa
 * @brief Structure used to specify the action to be associated with a specific signal
 * @details This instance `sa` is configured to use the `signal_handler` function 
 * for handling signals. The `sa_flags` is set to 0, meaning no special flags.
 */
struct sigaction sa = {
        .sa_handler = signal_handler,   /**< Pointer to the signal-handling function. */
        .sa_flags = 0                   /**< Special flags to affect behavior of a signal. */
};

/**
 * @brief Handles `SIGINT` and `SIGTERM` signals to allow graceful shutdown.
 * @param `sig` The signal number that was caught.
 * @details This function sets the global `exit_flag` to 1, logs the event,
 * and attempts to shut down the listening and connection sockets
 * to unblock any pending `accept()` or `recv()` calls.
 * @post The `exit_flag` is set to 1.
 * @post If `sock_fd` is valid, `shutdown(sock_fd, SHUT_RDWR)` is called.
 * @post If `conn_fd` is valid, `shutdown(conn_fd, SHUT_RDWR)` is called.
 */
static void signal_handler(int sig) {
        // Log that a signal was caught. The specific signal number `sig` could also be logged.
        syslog(LOG_INFO, "Caught signal %d, setting exit flag", sig);
        // Set the global exit flag to indicate the program should terminate.
        exit_flag = 1;

        // Attempt to gracefully shut down the sockets to unblock any blocking calls.
        // This helps the main loop to recognize the `exit_flag` and terminate cleanly.

        // Check if teh listening socket descriptor is valid.
        if(sock_fd != -1) {
                // Shut down the listening socet for both reading and writing.
                // This will cause any pending `accept()` calls to return with an error.
                shutdown(sock_fd, SHUT_RDWR);
        }
        // Check if the connection socket descriptor is valid.
        if(conn_fd != -1) {
                // Shut down the connection socket for both reading and writing.
                // This will cause any pending `recv()` calls on this connection to return.
                shutdown(conn_fd, SHUT_RDWR);
        }
}

/**
 * @brief Sends all data in the provided buffer over a socket.
 * @param `fd` The file descriptor of the socket to send data to.
 * @param `buf` A pointer to the buffer containing the data to send.
 * @paran `len` The number of bytes to send from the buffer.
 * @return 0 on success, -1 on failure.
 * @pre `fd` is a valid, open, and connected socket file descriptor.
 * @pre `buf` points to a valid memory region of at least `len` bytes.
 * @pre `len` is the correct number of bytes to send.
 * @post All `len` bytes from `buf` have been send to `fd`, or an error occurred.
 * @details This function handles partial sends by repeatedly calling `send()`
 * until all data is send or an error occurs.
 */
static int sendall(int fd, const char *buf, size_t len) {
        // Initialize an offset to keep track of how many bytes have been sent.
        size_t offset = 0;
        // Loop invariant: `offset` is the total number of bytes successfully sent so far.
        // Loop invariant: `offset <= len`.
        // Loop continues as long as not all bytes specified by `len` have been sent.
        while (offset < len) {
                // Attempt to send the remaining part of the buffer.
                // `buf + offset` points to the start of the unsent data.
                // `len - offset` is the number of bytes remaining to be sent.
                // The `0` flag indicates no special send options.
                ssize_t n = send(fd, buf + offset, len - offset, 0);
                // Check if the `send()` call resulted in an error.
                if (n < 0) {
                        // An error occured during send.
                        return -1; // Indicate failure.
                }
                // Check if `send()` returned 0, which can happen if the peer has closed the connection gracefully,
                // buf typically for blocking sockets, it means no data was sent without error.
                if (n == 0) {
                        // No bytes were sent, but no error.
                        // For simplicity here we just continue and busy loop
                        // A more robust solution might check for specific conditions or timeout.
                        continue;
                }
                // Add the number of bytes successfully sent (`n`) to the offset.
                offset += (size_t)n;
        }
        // All bytes have been sent succeessfully.
        return 0; // Indicate success.
}

/**
 * @brief Reads the entire content of a file and sends it over a socket.
 * @param `fp` A pointer to the `FILE` stream to read from. The file should be open for reading.
 * @param `client_fd` The file descriptor of the client socket to send data to.
 * @return 0 on success, -1 on failure.
 * @pre `fp` is a valid `FILE` pointer, opened for reading, and positioned correctly (this function rewinds it).
 * @pre `client_fd` is a valid, open, and connected socket file descriptor.
 * @post The entire content of the file pointed to by `fp` (from its beginning) has been send to `client_fd`,
 * or an error has occurred.
 * @post The file pointer `fp` is positioned at the end of the file.
 * @details This function first flushes any buffered data to the file, ensures it's written to disk,
 * then rewinds the file pointer to the beginning. It reads the file in chunks
 * and uses `sendall()` to transmit each chunk to the client.
 */
static int send_file_back(FILE *fp, int conn_fd) {
        // Ensure all buffered output for the stream `fp` is written to the underlying file.
        if(fflush(fp) !=0) {
                perror("fflush in `send_file_back()`"); // Log error if fflush fails.
                return -1; // Indicate failure.
        }
        // Ensure that all data for `fp` is physically written to teh storage device.
        // `fileno(fp)` gets the underlying file descriptor for the stream.
        if(fsync(fileno(fp)) != 0) {
                perror("fsync in `send_file_back()`"); // Log error if fsync fails.
                return -1; // Indicate failure.
        }
        // Reset the file position indicator for the stream `fp` to the beginning of teh file.
        // This is necessary to read the entire file content from the start.
        rewind(fp);

        // Define a buffer to hold chunks of data read from the file.
        // Its size is `MAX_RECV_BUF_LEN`
        char send_buffer[MAX_RECV_BUF_LEN];
        // Variable to store the number of bytes read by `fread()`.
        size_t bytes_read;

        // Loop to read the file in chunks and send each chunk.
        // Loop invariant: All data read from the file up to the current point has been attempted to be sent.
        // Loop continues as long as `fread()` successfully reads more than 0 bytes.
        while ((bytes_read = fread(send_buffer, 1, MAX_RECV_BUF_LEN, fp)) > 0) {
                // `fread()` reads `bytes_read` items of size 1 byte from `fp` into `send_buffer`.
                // If `bytes_read` is 0, it means EOF is reached or an error occurred.
                
                // Send the chunk of data read from the file using the `sendall()` helpfer function.
                if (sendall(client_fd, send_buffer, bytes_read)< 0) {
                        perror("`sendall()` in `send_file_back()` failed");
                        return -1; // Indicate failure.
                }
        }

        // After the loop, check if `fread()` exited due to an error (not just EOF).
        if (ferror(fp)) {
                perror("`fread()` in `send_file_back()` failed");
                clearerr(fp); // Clear the error indicator for the stream.
                return -1; // Indicate failure
        }

        // Move the file pointer to the end of teh file. This is useful if teh file
        // will be appended to later by the same `FILE* fp` without re-opening.
        if(fseek(fp, 0, SEEK_END) != 0) {
                perror("fseek to SEEK_END in `send_file_back()` failed");
                return -1; // Indicate failure
        }
        // All data from the file has been successfully sent.
        return 0; // Indicate success
}

/**
 * @brief Main function for the AESD socket server.
 * @param `argc` The number of command-line arguments.
 * @param `argv` An array of command-line argument strings.
 * @return EXIT_SUCCESS on successful completion, EXIT_FAILURE on error.
 * @details Initializes the server, sets up signal handling, binds to a port,
 * listens for connections, and enters a loop to accept and handle
 * cleint requests. If "-d" is passed as an argument, it runs as a daemon.
 */
int main(int argc, char* argv[]) {
        // --- Initialization ---
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        signal(SIGPIPE, SIG_IGN);
        openlog(NULL, LOG_CONS, LOG_USER);
        unlink(DATA_FILE);


        // --- Handle Daemon Mode Argument ---
        if(argc >= 2 && strcmp(argv[1], "-d") == 0) {
                daemon_flag = true;
                syslog(LOG_INFO, "Daemon mode requested."); // Log daemon mode activation.
        }


        // --- Socket Creation and Setup ---
        // open a stream socket bound to port 9000, if any of the socket connection steps fail return -1
        sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_fd == -1) {
                perror("Error creating Berkeley Stream Socket");
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
                perror("Error binding Berkeley Stream Socket to port 9000");
                close(sock_fd);
                exit(EXIT_FAILURE);
        }

        // --- Daemonization (if requested) ---
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

        // listen and accept connections
        listen(sock_fd, BACKLOG);                       // Socket is now actually enabled and passively listening for connections
        
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        char ip_string[INET_ADDRSTRLEN];
        char log_string[64];
        char receive_buffer[MAX_RECV_BUF_LEN + 1];
        
        // Loop invariant: `exit_flag` is 0.
        // Loop invariant: `sock_fd` is a valid listening socket descriptor.
        // The loop continues as long as the `exit_flag` is not set (i.e., no termination signal received).
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
                FILE *fp = fopen(DATA_FILE, "a+");
                if(!fp) {
                        perror("Error opening file");
                        close(conn_fd);
                        continue;
                }
                int msg_len = 0;
                int total_received = 0;

                // Loop to receive data from client
                // Loop invariant: `total_received` is the number of bytes currently in `receive_buffer` 
                //                  that have not been processed (written to file or part of a complete packet).
                // Loop invariant: `receive_buffer` contains data from `receive_buffer[0]` to `receive_buffer[total_received - 1]`
                // Loop continues as long as `recv()` returns a positive value (bytes received).
                // `recv()` attemps to read up to `MAX_RECV_BUF_LEN - total_received` bytes into `receive_buffer + total_received`.
                // The `0` flag means no special receive options.
                while((msg_len = recv(conn_fd, receive_buffer + total_received, MAX_RECV_BUF_LEN - total_received, 0)) > 0) {
                        total_received += msg_len;
                        receive_buffer[total_received] = '\0';
                        char *nl;
                        // Process complete lines (packets) ending with a newline character.
                        // Loop invariant: All complete lines before the current `receive_buffer` content have been processes.
                        // Loop continues as long as `memchr` finds a newline in the `total_received` bytes of `receive_buffer`.
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

        // --- Shut down Phase ---
        // This part is reached when the `exit_flag` is set.

        // If a client connection was active when the loop exited (e.g., due to signal during processing), close it.
        // This is a safeguard, as `conn_fd` should ideally be -1 if the loop completed a client session normally.
        if(conn_fd != -1) { 
                syslog(LOG_INFO, "Closing active connection (fd: %d) during shutdown.", conn_fd);
                close(conn_fd);  // Close the client connection socket
                conn_fd = -1; // Mark as closed.
        }

        // Log that the server is exiting due to a caught signal.
        syslog(LOG_INFO, "Caught signal, exiting");
        // Delete the data file
        if(unlink(DATA_FILE) == -1) {
                syslog(LOG_WARNING, "Error unlinking %s on exit: %m", DATA_FILE);
        } else {
                syslog(LOG_DEBUG, "Successfully unlinked %s on exit.", DATA_FILE);
        }

        close(sock_fd);
        
        // Close the connection to the system logger.
        closelog();

        // Exit the program successfully.
        return EXIT_SUCCESS;
}
