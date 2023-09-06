/**
 * 현재 실행중인 프로세스들의 정보를 확인하고 관리할 수 있는 유저 프로그램
 * 실행 후 종료 명령어가 들어올 때까지 한 줄씩 명령어를 입력받아 동작
 *
 * list: 현재 실행중인 프로세스 정보 출력
 * kill: 특정 pid의 프로세스를 kill(using kill syscall) & 성공 여부 출력
 * execute <path> <stacksize>: path 경로에 위치한 프로그램을 stacksize 개수만큼의 스택용 페이지와 함께 실행
 * memlim <pid> <limit>:
 * exit: pmanager 종료
 */

#include "types.h"
#include "user.h"
#include "fcntl.h"

int getcmd(char *buf, int nbuf)
{
    printf(2, ">> ");
    memset(buf, 0, nbuf);
    gets(buf, nbuf);
    if (buf[0] == 0) // EOF
        return -1;
    return 0;
}

void panic(char *s)
{
    printf(2, "%s\n", s);
    exit();
}

int fork1(void)
{
    int pid;

    pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

int main(void)
{
    static char buf[100];
    int fd;

    // Ensure that three file descriptors are open.
    while ((fd = open("console", O_RDWR)) >= 0)
    {
        if (fd >= 3)
        {
            close(fd);
            break;
        }
    }

    // Read and run input commands.
    while (getcmd(buf, sizeof(buf)) >= 0)
    {
        // list
        if (buf[0] == 'l' && buf[1] == 'i' && buf[2] == 's' && buf[3] == 't' && (buf[4] == ' ' || buf[4] == '\n'))
        {
            showProcessList();
        }

        // kill
        else if (buf[0] == 'k' && buf[1] == 'i' && buf[2] == 'l' && buf[3] == 'l' && buf[4] == ' ')
        {
            // parse pid from buf
            char strPid[100];

            for(int i = 0; i < 100; i++) {
                strPid[i] = 0;
            }

            int pid = 0;
            for (int idx = 5; buf[idx] != ' ' && buf[idx] != '\n' && idx < 100; idx++)
            {
                if (buf[idx] < '0' || buf[idx] > '9')
                {
                    pid = -1;
                    break;
                }
                strPid[idx - 5] = buf[idx];
            }

            // 문자가 섞인 pid input(invalid)
            if (pid == -1)
            {
                printf(1, "kill [failed]: invalid pid input\n");
                continue;
            }

            // pid를 int형으로 변환
            pid = atoi(strPid);

            // kill process
            if (kill(pid) == 0)
            {
                printf(1, "kill [success]\n");
            }
            else
            {
                printf(1, "kill [failed]\n");
            }
        }

        // exit
        else if (buf[0] == 'e' && buf[1] == 'x' && buf[2] == 'i' && buf[3] == 't' && (buf[4] == ' ' || buf[4] == '\n'))
        {
            exit();
        }

        // memlim
        else if (buf[0] == 'm' && buf[1] == 'e' && buf[2] == 'm' && buf[3] == 'l' && buf[4] == 'i' && buf[5] == 'm' && buf[6] == ' ')
        {
            char strPid[100];
            char strLimit[100];

            for(int i = 0; i < 100; i++) {
                strPid[i] = 0;
                strLimit[i] = 0;
            }

            int pid = 0;
            int mlimit = 0;

            int idx = 7;
            for (; buf[idx] != ' ' && buf[idx] != '\n' && idx < 100; idx++)
            {
                if (buf[idx] < '0' || buf[idx] > '9')
                {
                    pid = -1;
                    break;
                }
                strPid[idx - 7] = buf[idx];
            }

            idx++;
            for (int idx2 = idx; buf[idx2] != ' ' && buf[idx2] != '\n' && idx2 < 100; idx2++)
            {
                if (buf[idx2] < '0' || buf[idx2] > '9')
                {
                    mlimit = -1;
                    break;
                }
                strLimit[idx2 - idx] = buf[idx2];
            }

            // 문자가 섞인 pid input(invalid)
            if (pid == -1 || mlimit == -1)
            {
                printf(1, "memlim [failed]: invalid arguments input\n");
                continue;
            }

            // pid, memory limit를 숫자로 변환
            pid = atoi(strPid);
            mlimit = atoi(strLimit);

            if (setmemorylimit(pid, mlimit) == 0)
            {
                printf(1, "memlim [success]\n");
            }
            else
            {
                printf(1, "memlim [failed]: system call failed\n");
            }
        }

        // execute
        else if (buf[0] == 'e' && buf[1] == 'x' && buf[2] == 'e' && buf[3] == 'c' && buf[4] == 'u' && buf[5] == 't' && buf[6] == 'e' && buf[7] == ' ')
        {
            // parse path and stacksize from buf
            char strPath[100];
            char strStackSize[100];

            for(int i = 0; i < 100; i++) {
                strPath[i] = 0;
                strStackSize[i] = 0;
            }

            int stacksize = 0;

            int idx = 8;
            for (; buf[idx] != ' ' && buf[idx] != '\n' && idx < 100; idx++)
            {
                strPath[idx - 8] = buf[idx];
            }
            strPath[idx] = 0;

            idx++;
            for (int idx2 = idx; buf[idx2] != ' ' && buf[idx2] != '\n' && idx2 < 100; idx2++)
            {
                if (buf[idx2] < '0' || buf[idx2] > '9')
                {
                    stacksize = -1;
                    break;
                }
                strStackSize[idx2 - idx] = buf[idx2];
            }

            // 문자가 섞인 stacksize input(invalid)
            if (stacksize == -1)
            {
                printf(1, "execute [failed]: invalid stacksize input\n");
                continue;
            }

            // stack size를 int형으로 변환
            stacksize = atoi(strStackSize);

            char *argv[2];
            argv[0] = strPath;
            argv[1] = 0;

            if (fork1() == 0)
            {
                // TODO: fork를 한번 더 해서, wait을 pmanager shell이 아니라 또 하나의 process가 하도록
                // 그런데 이렇게 하더라도 결국 pmanager가 처음 fork된 process를 기다리지 않으므로 zombie가 되는데...
                // piazza 참고(좀비가 되는게 맞음)

                exec2(strPath, argv, stacksize);
                printf(2, "execute %s failed\n", strPath);

                exit();
            }
        }
        else
        {
            printf(1, "error: command not found\n");
        }
        // wait(); // TODO: pmanager가 동시에 실행되어야하므로, wait할 필요는 없으며 ZOMBIE가 출력되는 것이 정상
        // 동시에 실행되도록 하기 위해 wait()를 주석처리함
    }
    exit();
}
