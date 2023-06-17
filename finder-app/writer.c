#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    if(argc != 3)
        return 1;
    
    openlog("writer_c", LOG_PID, LOG_USER);

    char *writefile = argv[1];
    char *writestr = argv[2];

    char *writefile_cpy = (char*) malloc(strlen(writefile+1) * sizeof(char));
    strcpy(writefile_cpy, writefile);

    char *dirpath = dirname(writefile_cpy);

    struct stat st = {0};
    if(stat(dirpath, &st) == -1)
        mkdir(dirpath, 0777);
    
    int pfd = open(writefile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(pfd == -1)
    {
        syslog(LOG_ERR, "file could not be created");
        return 1;
    }

    write(pfd, writestr, strlen(writestr));
    syslog(LOG_DEBUG, "Writing \"%s\" to \"%s\"", writestr, writefile);
    close(pfd);

    closelog();
    free(writefile_cpy);
    return 0;
}