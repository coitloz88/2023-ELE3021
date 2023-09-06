#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

void printFileData(int fd, const char *filename)
{
    struct stat st;
    fstat(fd, &st);
    printf(1, "%s %d %d %d\n", filename, st.type, st.ino, st.size);
}

int main(int argc, char *argv[])
{
    int fd1, r1;
	// int fd2, r2;

    const char *filename1 = "SyncTestFile";
    // const char *filename2 = "SyncFile";

    char data[512] = "Hello world!\n";

    printf(1, "synctest: starting..\n");

    fd1 = open(filename1, O_CREATE | O_RDWR);
    // fd2 = open(filename2, O_CREATE | O_RDWR);

    printFileData(fd1, filename1);
    // printFileData(fd2, filename2);

    if ((r1 = write(fd1, data, sizeof(data))) < -1)
    {
        printf(2, "write failed\n");
        exit();
    }
/*
    if ((r2 = write(fd2, data, sizeof(data))) < -1)
    {
        printf(2, "write failed\n");
        exit();
    }
*/
    printf(1, "\nAfter filewrite\n");
    printFileData(fd1, filename1);
  //  printFileData(fd2, filename2);
	int flushCnt = sync();
	printf(1, "flushCnt: %d\n", flushCnt);
    close(fd1);
    //close(fd2);

    printf(1, "synctest: test over..\n");

    exit();
}

