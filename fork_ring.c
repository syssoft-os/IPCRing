#ifdef _WIN32
    #define WIN_PIPE
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#elif defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
    #define POSIX_PIPE
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#define new(_type) ((_type*)malloc(sizeof(_type)))
#define array(_type, _count) ((_type*)malloc(sizeof(_type) * (_count)))

typedef int32_t i32;
typedef uint32_t u32;

#ifdef WIN_PIPE

#include <io.h>
#include <fcntl.h>

typedef i32 pid_t;
#define write _write
#define read _read

pid_t fork() { errno = ENOSYS; return -1; }

#endif // WIN_PIPE
#ifdef POSIX_PIPE

#include <unistd.h>
#include <sys/types.h>

#endif // POSIX_PIPE

struct Pipe
{
    i32 HandleRead;
    i32 HandleWrite;
};
typedef struct Pipe Pipe;

static bool PipeInit(Pipe* inPipe, u32 size)
{
    i32 handles[2] = { -1, -1 };
#ifdef WIN_PIPE
    i32 status = _pipe(handles, size, _O_BINARY);
#endif
#ifdef POSIX_PIPE
    i32 status = pipe(handles);
#endif
    inPipe->HandleRead = handles[0];
    inPipe->HandleWrite = handles[1];
    return status >= 0;
}

static bool PipeRead(Pipe pipe, void* data, u32 size)
{
    return read(pipe.HandleRead, data, size) >= 0;
}

static bool PipeWrite(Pipe pipe, const void* data, u32 size)
{
    return write(pipe.HandleWrite, data, size) >= 0;
}

i32 main(i32 argc, char** argv)
{
    if (argc != 2)
    {
        printf(
            "Usage:\n"
            "  fork_ring <count>\n");
        exit(EXIT_SUCCESS);
    }

    char* forkCountEnd = NULL;
    i32 forkCount = strtol(argv[1], &forkCountEnd, 10);
    if (forkCountEnd == argv[1] || forkCount < 0)
    {
        perror("invalid count");
        exit(EXIT_FAILURE);
    }

    i32 pipesCount = forkCount + 1;
    Pipe* pipes = array(Pipe, pipesCount);
    if (!pipes)
    {
        perror("failed to allocate memory");
        exit(EXIT_FAILURE);
    }

    for (i32 i = 0; i < pipesCount; ++i)
    {
        if (!PipeInit(pipes + i, sizeof(u32)))
        {
            perror("pipe init failed");
            exit(EXIT_FAILURE);
        }
    }

    pid_t processId = 1;
    i32 processIndex = 0;
    for (i32 i = 0; i < forkCount; ++i)
    {
        if (processId != 0)
        {
            ++processIndex;
            processId = fork();
            if (processId < 0)
            {
                perror("fork failed");
                exit(EXIT_FAILURE);
            }
        }
        printf("forked %d\n", i);
    }

    if (processId != 0)
    {
        processIndex = 0;
        u32 value = 0;
        if (!PipeWrite(pipes[(processIndex + 1) % pipesCount], &value, sizeof(u32)))
        {
            perror("initial write failed");
            exit(EXIT_FAILURE);
        }
        printf("write: %d  process: %d\n", value, processIndex);
    }

    while (true)
    {
        u32 value = 0;
        if (!PipeRead(pipes[processIndex], &value, sizeof(u32)))
        {
            perror("read failed");
            exit(EXIT_FAILURE);
        }
        printf("read: %d  process: %d\n", value, processIndex);
        ++value;
        if (!PipeWrite(pipes[(processIndex + 1) % pipesCount], &value, sizeof(u32)))
        {
            perror("write failed");
            exit(EXIT_FAILURE);
        }
    }

    // kind of unnecessary but oh well
    free(pipes);
    return EXIT_SUCCESS;
}
