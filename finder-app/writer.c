#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>


int main(int argc, char *argv[]) {
 
    openlog("writer", LOG_PID, LOG_USER);
 
    if (argc != 3) {
        syslog(LOG_ERR, "Please provide input <file> and <string> for %s utility", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    /*Start Create file*/
    
    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Error while opening file %s: %s", writefile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    fprintf(file, "%s", writestr);
    if (ferror(file)) {
        syslog(LOG_ERR, "Error while writing to file %s: %s", writefile, strerror(errno));
        fclose(file);
        exit(EXIT_FAILURE);
    }

    fclose(file);
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    
    /* End create file */

    closelog();
    return EXIT_SUCCESS;
}


