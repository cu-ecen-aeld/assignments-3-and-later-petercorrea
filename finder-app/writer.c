#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// a program's arguments includes the program's name and the passed in arguments
// char is 1 byte and represents a character
// char* represents a string, which is an array of characters
// char** represents an array of strings
int main(int argc, char **argv)
{
    if (argc != 3)
        return 1;

    // opens a connection to the system logger
    openlog("writer_c", LOG_PID, LOG_USER);

    // grab the arguments
    char *writefile = argv[1];
    char *writestr = argv[2];

    //  allocates bytes equal to the length of writefile plus one for the null terminating character, each the size of a char.
    char *writefile_cpy = (char *)malloc(strlen(writefile + 1) * sizeof(char));
    strcpy(writefile_cpy, writefile);

    // grab the directory path from the writefile path
    char *dirpath = dirname(writefile_cpy);

    // The stat structure is typically used to hold file information
    struct stat st = {0};
    // the stat function returns information about a file, it takes a path to the file and a pointer to a stat structure
    if (stat(dirpath, &st) == -1)
        mkdir(dirpath, 0777);

    int pfd = open(writefile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (pfd == -1)
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