#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


#define ISPREFIX(prefix, str) \
    ((strncmp(prefix, (str), sizeof(prefix) - 1) == 0) ? sizeof(prefix) - 1 : 0)

int main(int argc, const char* argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <package>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    const uint64_t nproc = sysconf(_SC_NPROCESSORS_ONLN);

    char* mem = malloc(1 << 20);

    char* recipePath = mem;
    mem += 128;

    recipePath[0] = '\0';
    strcat(recipePath, "recipes/");
    strncat(recipePath, argv[1], 64);
    strcat(recipePath, "/spec");

    FILE* recipeFile = fopen(recipePath, "r");
    if (!recipeFile)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char* line = mem;
    uint64_t lineSize = 120;
    mem += lineSize;

    char* specSrc = mem;
    mem += 256;

    char* specSig = mem;
    mem += 256;

    char* specChecksum = mem;
    mem += 512;

    uint64_t nread = 0;
    while ((nread = getline(&line, &lineSize, recipeFile)) != -1)
    {
        uint32_t prefixLen;

        if ((prefixLen = ISPREFIX("# src=", line)))
        {
            strncpy(specSrc, line + prefixLen, nread - prefixLen - 1);
            continue;
        }

        if ((prefixLen = ISPREFIX("# sig=", line)))
        {
            strncpy(specSig, line + prefixLen, nread - prefixLen - 1);
            continue;
        }

        if ((prefixLen = ISPREFIX("# checksum=", line)))
        {
            strncpy(specChecksum, line + prefixLen, nread - prefixLen - 1);
            continue;
        }
    }
    fclose(recipeFile);

    printf("src: %s\n", specSrc);
    printf("sig: %s\n", specSig);
    printf("checksum: %s\n", specChecksum);

    exit(0);

    printf("Recipe path: %s\n", recipePath);

    char *recipeArgv[3];
    recipeArgv[0] = "-m";
    recipeArgv[1] = recipePath;
    recipeArgv[2] = '\0';

    pid_t pid = fork();
    switch (pid)
    {
        case -1:
        {
            perror("fork");
            break;
        }

        case 0:
        {
            putenv("PREFIX=/usr");
            putenv("MAKEFLAGS=-j");
            putenv("FORCE_UNSAFE_CONFIGURE=1");

            printf("Building %s\n", recipePath);
            execvp("bash", recipeArgv);
            exit(127);
        }

        default:
        {
            wait(NULL);
            break;
        }
    }

    return 0;
}