#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

#define PROGRAME_NAME "find"

char *
fmtname(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    *(buf + strlen(p)) = '\0';
    return buf;
}

void find(char *path, char *target)
{
    // printf("in %s\n", path);
    char buf[512], *p;
    int fd;
    struct stat st;

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "%s: cannot open %s\n", PROGRAME_NAME, path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "%s: cannot stat %s\n", PROGRAME_NAME, path);
        close(fd);
        return;
    }

    // 1. if path is a file and filename is target
    if (st.type == T_FILE)
    {
        // printf("%s %s\n", fmtname(path), target);
        if (!strcmp(fmtname(path), target))
            printf("%s\n", path);
    }
    // 2. if path is a directory, read every entry and recursive
    else if (st.type == T_DIR)
    {
        struct dirent de;

        strcpy(buf, path);
        int len = strlen(path);
        p = buf + len;
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0 || !strcmp(de.name, ".") 
                || !strcmp(de.name, ".."))
                continue;
            len = strlen(de.name);
            memmove(p, de.name, len);
            p[len] = 0;
            find(buf, target);
        }
    }

    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(2, "usage: find <dir> <target>\n");
        exit(1);
    }
    find(argv[1], argv[2]);

    exit(0);
}
