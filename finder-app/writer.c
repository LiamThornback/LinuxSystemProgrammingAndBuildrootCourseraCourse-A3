#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
        openlog("writer", LOG_CONS | LOG_PID, LOG_USER);

        if(argc < 3) {
                syslog(LOG_ERR, "Insufficient arguments");
                exit(EXIT_FAILURE);
        }

        const char* const text = argv[2];
        const char* const filename = argv[1];

        FILE* const fptr = fopen(filename, "w");
        if(fptr == NULL) {
                syslog(LOG_ERR, "Failed to open file %s: %m", filename);
                closelog();
                exit(EXIT_FAILURE);
        }

        if(fprintf(fptr, "%s", text) <= 0) {
                syslog(LOG_ERR, "Failed to write to file %s: %m", filename);
                fclose(fptr);
                closelog();
                exit(EXIT_FAILURE);
        }

        syslog(LOG_DEBUG, "Writing %s to %s", text, filename);

        if (fclose(fptr) != 0) {
                syslog(LOG_ERR, "Failed to close file %s: %m", filename);
                closelog();
                exit(EXIT_FAILURE);
        }

        closelog();

        return EXIT_SUCCESS;
}
