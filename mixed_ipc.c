#ifdef _WIN32
    #define SYS_WIN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#elif defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
    #define SYS_POSIX
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#if ZMQ_AVAILABLE
#include <zmq.h>
#endif

#ifdef SYS_WIN

#include <io.h>

typedef int pid_t;
typedef int mode_t;
typedef int key_t;
typedef int64_t ssize_t;
typedef size_t off_t;
typedef int sem_t;
typedef int sa_family_t;
typedef int socklen_t;

#define write _write
#define read _read
#define open _open
#define unlink _unlink
#define close _close

#define O_NONBLOCK 2048
#define F_GETFD 1
#define F_SETFD 2

#define SEM_FAILED NULL
#define PROT_READ 1
#define PROT_WRITE 2

#define MAP_SHARED 1
#define MAP_FAILED ((void*)-1)

#define AF_UNIX 1
#define SOCK_SEQPACKET 5

struct sockaddr_un
{
    sa_family_t sun_family;
    char        sun_path[108];
};

struct sockaddr
{
    sa_family_t sa_family;
    char        sa_data[14];
};

int fork() { errno = ENOSYS; return -1; }
int getpid() { errno = ENOSYS; return -1; }
int mkfifo(const char* pathname, mode_t mode) { errno = ENOSYS; return -1; }
int fcntl(int fd, int cmd, ... /* arg */) { errno = ENOSYS; return -1; }

int shm_open(const char* name, int oflag, mode_t mode) { errno = ENOSYS; return -1; }
int shm_unlink(const char* name) { errno = ENOSYS; return -1; }

sem_t* sem_open(const char* name, int oflag, mode_t mode, unsigned int value) { errno = ENOSYS; return SEM_FAILED; }
int sem_close(sem_t* sem) { errno = ENOSYS; return -1; }
int sem_unlink(const char* name) { errno = ENOSYS; return -1; }
int sem_wait(sem_t* sem) { errno = ENOSYS; return -1; }
int sem_post(sem_t* sem) { errno = ENOSYS; return -1; }
int sem_getvalue(sem_t* sem, int* sval) { errno = ENOSYS; return -1; }

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) { errno = ENOSYS; return MAP_FAILED; }
int munmap(void* addr, size_t length) { errno = ENOSYS; return -1; }
int ftruncate(int fd, off_t length) { errno = ENOSYS; return -1; }

int socket(int domain, int type, int protocol) { errno = ENOSYS; return -1; }
int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) { errno = ENOSYS; return -1; }
int listen(int sockfd, int backlog) { errno = ENOSYS; return -1; }
int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) { errno = ENOSYS; return -1; }
int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) { errno = ENOSYS; return -1; }

#endif // SYS_WIN
#ifdef SYS_POSIX

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <semaphore.h>

#endif // SYS_POSIX

#if !ZMQ_AVAILABLE

void* zmq_ctx_new() { errno = ENOSYS; return NULL; }
int zmq_ctx_destroy(void* context) { errno = ENOSYS; return -1; }
void* zmq_socket(void* context, int type) { errno = ENOSYS; return NULL; }
int zmq_close(void* socket) { errno = ENOSYS; return -1; }
int zmq_bind(void* socket, const char* endpoint) { errno = ENOSYS; return -1; }
int zmq_send(void* socket, void* buf, size_t len, int flags) { errno = ENOSYS; return -1; }
int zmq_recv(void *socket, void *buf, size_t len, int flags) { errno = ENOSYS; return -1; }

#endif // !ZMQ_AVAILABLE

typedef int32_t i32;
typedef uint32_t u32;

#define new(_type) ((_type*)malloc(sizeof(_type)))
#define array(_type, _count) ((_type*)malloc(sizeof(_type) * (_count)))

static char* AppendPidSuffix(const char* str)
{
    pid_t pid = getpid();
    size_t strLength = strlen(str);
    size_t pidLength = snprintf(NULL, 0, "_%x", pid);

    char* result = array(char, strLength + pidLength + 1);
    if (!result)
    {
        perror("failed to allocate memory");
        return NULL;
    }
    strcpy(result, str);
    sprintf(result + strLength, "_%x", pid);

    return result;
}

struct ComAction
{
    void* Context;
    bool (*Action)(u32*, void*);
    bool (*Kill)(u32*, void*);
    bool (*Destructor)(void*);
};
typedef struct ComAction ComAction;

static ComAction ComActionEmpty()
{
    ComAction result;
    memset(&result, 0, sizeof(ComAction));
    return result;
}

static bool ComActionRelease(ComAction* comAction)
{
    bool success = true;
    if (comAction->Destructor && comAction->Context)
        success = (*comAction->Destructor)(comAction->Context);
    free(comAction->Context);
    memset(comAction, 0, sizeof(ComAction));
    return success;
}

static bool ComActionExec(ComAction* comAction, u32* valuePtr)
{
    return comAction->Action ? (*comAction->Action)(valuePtr, comAction->Context) : true;
}

static bool ComActionKill(ComAction* comAction, u32 killValue)
{
    return comAction->Kill ? (*comAction->Kill)(&killValue, comAction->Context) : true;
}

//------------------- Pipe -------------------//

struct Pipe
{
    i32 HandleRead;
    i32 HandleWrite;
};
typedef struct Pipe Pipe;

static bool PipeInit(Pipe* inPipe, u32 size)
{
    i32 handles[2] = { -1, -1 };
#ifdef SYS_WIN
    i32 status = _pipe(handles, size, _O_BINARY);
#endif
#ifdef SYS_POSIX
    i32 status = pipe(handles);
#endif
    inPipe->HandleRead = handles[0];
    inPipe->HandleWrite = handles[1];
    if (status == -1)
    {
        perror("failed to create unnamed pipe");
        return false;
    }
    return true;
}

static bool PipeRead(Pipe pipe, void* data, u32 size)
{
    return read(pipe.HandleRead, data, size) != -1;
}

static bool PipeWrite(Pipe pipe, const void* data, u32 size)
{
    return write(pipe.HandleWrite, data, size) != -1;
}

//------------------- Named Pipe -------------------//

struct NamedPipe
{
    char* Name;
    i32 PipeDesc;
    bool IsOwner;
    bool IsWrite;
};
typedef struct NamedPipe NamedPipe;

static bool NamedPipeInit(NamedPipe* namedPipe, const char* name, bool isWrite, bool createNew)
{
    memset(namedPipe, 0, sizeof(NamedPipe));
    namedPipe->PipeDesc = -1;
    namedPipe->IsOwner = createNew;
    namedPipe->IsWrite = isWrite;

    namedPipe->Name = array(char, strlen(name) + 1);
    if (!namedPipe->Name)
    {
        perror("failed to allocate memory");
        return false;
    }
    strcpy(namedPipe->Name, name);

    if (createNew)
    {
        if (mkfifo(namedPipe->Name, 0666) == -1)
        {
            fprintf(stderr, "failed to create pipe '%s': ", namedPipe->Name);
            perror("");
            return false;
        }
    }

    return true;
}

static bool NamedPipeRelease(NamedPipe* namedPipe)
{
    printf("destroying named pipe\n");
    bool success = true;

    if (namedPipe->PipeDesc != -1 && close(namedPipe->PipeDesc) == -1)
    {
        fprintf(stderr, "failed to close pipe '%s': ", namedPipe->Name);
        perror("");
        success = false;
    }
    if (namedPipe->IsOwner)
    {
        if (namedPipe->Name && unlink(namedPipe->Name) == -1)
        {
            fprintf(stderr, "failed to unlink pipe '%s': ", namedPipe->Name);
            perror("");
            success = false;
        }
    }

    memset(namedPipe, 0, sizeof(NamedPipe));
    namedPipe->PipeDesc = -1;
    return success;
}

static bool NamedPipeRead(NamedPipe* namedPipe, void* outBuffer, size_t bufferSize)
{
    if (namedPipe->PipeDesc == -1)
        namedPipe->PipeDesc = open(namedPipe->Name, O_RDONLY);
    if (namedPipe->PipeDesc == -1)
    {
        fprintf(stderr, "failed to open pipe '%s': ", namedPipe->Name);
        perror("");
        return false;
    }

    i32 bytesRead = read(namedPipe->PipeDesc, outBuffer, bufferSize);
    if (bytesRead != bufferSize)
    {
        if (bytesRead == 0)
            printf("eof\n");
        else
        {
            fprintf(stderr, "failed to read from pipe '%s': ", namedPipe->Name);
            perror("");
        }
        return false;
    }

    return true;
}

static bool NamedPipeWrite(NamedPipe* namedPipe, const void* inBuffer, size_t bufferSize, bool nonBlocking)
{
    if (namedPipe->PipeDesc == -1)
        namedPipe->PipeDesc = open(namedPipe->Name, O_WRONLY | (nonBlocking ? O_NONBLOCK : 0));
    if (namedPipe->PipeDesc == -1)
    {
        fprintf(stderr, "failed to open pipe '%s': ", namedPipe->Name);
        perror("");
        return false;
    }

    if (nonBlocking)
    {
        i32 flags = fcntl(namedPipe->PipeDesc, F_GETFD);
        if (flags == -1)
        {
            fprintf(stderr, "failed to read flags of pipe '%s': ", namedPipe->Name);
            perror("");
            return false;
        }

        flags |= O_NONBLOCK;
        if (fcntl(namedPipe->PipeDesc, F_SETFD, flags) == -1)
        {
            fprintf(stderr, "failed to set flags of pipe '%s': ", namedPipe->Name);
            perror("");
            return false;
        }
    }

    bool success = true;
    i32 bytesWritten = write(namedPipe->PipeDesc, inBuffer, bufferSize);
    if (bytesWritten != bufferSize)
    {
        fprintf(stderr, "failed to write to pipe '%s': ", namedPipe->Name);
        perror("");
        success = false;
    }

    if (nonBlocking)
    {
        i32 flags = fcntl(namedPipe->PipeDesc, F_GETFD);
        if (flags == -1)
        {
            fprintf(stderr, "failed to read flags of pipe '%s': ", namedPipe->Name);
            perror("");
            return false;
        }

        flags &= ~O_NONBLOCK;
        if (fcntl(namedPipe->PipeDesc, F_SETFD, flags) == -1)
        {
            fprintf(stderr, "failed to set flags of pipe '%s': ", namedPipe->Name);
            perror("");
            return false;
        }
    }

    return success;
}

static bool ActionNamedPipeRead(u32* valuePtr, void* context)
{
    return NamedPipeRead((NamedPipe*)context, valuePtr, sizeof(u32));
}

static bool ActionNamedPipeWrite(u32* valuePtr, void* context)
{
    return NamedPipeWrite((NamedPipe*)context, valuePtr, sizeof(u32), false);
}

static bool KillNamedPipe(u32* killValuePtr, void* context)
{
    return NamedPipeWrite((NamedPipe*)context, killValuePtr, sizeof(u32), true);
}

static bool DestructorNamedPipe(void* context)
{
    return NamedPipeRelease((NamedPipe*)context);
}

static bool ComActionInitNamedPipe(ComAction* comAction, const char* pipeName, bool isWrite)
{
    comAction->Context = new(NamedPipe);
    if (!comAction->Context)
    {
        perror("failed to allocate memory");
        return false;
    }

    char* tempPipeName = NULL;
    bool createNew = !pipeName;
    if (createNew)
    {
        tempPipeName = AppendPidSuffix(isWrite ? "ipc_pipe_out" : "ipc_pipe_in");
        if (!tempPipeName)
            return false;
    }

    bool success = NamedPipeInit((NamedPipe*)comAction->Context, createNew ? tempPipeName : pipeName, isWrite, createNew);
    free(tempPipeName);

    comAction->Action = isWrite ? &ActionNamedPipeWrite : &ActionNamedPipeRead;
    comAction->Kill = isWrite ? &KillNamedPipe : NULL;
    comAction->Destructor = &DestructorNamedPipe;

    if (createNew)
        printf(isWrite ? "out pipe: %s\n" : "in pipe: %s\n", ((NamedPipe*)comAction->Context)->Name);
    return success;
}

//------------------- Shared Memory -------------------//

struct MemoryBlock
{
    u32* Memory;
    char* BlockName;
    i32 BlockDesc;
    char* SemaphoreReadName;
    char* SemaphoreWriteName;
    sem_t* SemaphoreRead;
    sem_t* SemaphoreWrite;
    bool IsOwner;
};
typedef struct MemoryBlock MemoryBlock;

static bool MemoryBlockInit(MemoryBlock* memBlock, const char* blockName, const char* semReadName, const char* semWriteName, bool createNew)
{
    memset(memBlock, 0, sizeof(MemoryBlock));
    memBlock->BlockDesc = -1;
    memBlock->SemaphoreRead = SEM_FAILED;
    memBlock->SemaphoreWrite = SEM_FAILED;
    memBlock->Memory = MAP_FAILED;

    memBlock->BlockName = array(char, strlen(blockName) + 1);
    if (!memBlock->BlockName)
    {
        perror("failed to allocate memory");
        return false;
    }
    strcpy(memBlock->BlockName, blockName);

    memBlock->SemaphoreReadName = array(char, strlen(semReadName) + 1);
    if (!memBlock->SemaphoreReadName)
    {
        perror("failed to allocate memory");
        return false;
    }
    strcpy(memBlock->SemaphoreReadName, semReadName);
    
    memBlock->SemaphoreWriteName = array(char, strlen(semWriteName) + 1);
    if (!memBlock->SemaphoreWriteName)
    {
        perror("failed to allocate memory");
        return false;
    }
    strcpy(memBlock->SemaphoreWriteName, semWriteName);

    memBlock->IsOwner = createNew;

    memBlock->BlockDesc = shm_open(memBlock->BlockName, (memBlock->IsOwner ? O_CREAT : 0) | O_RDWR, 0666);
    if (memBlock->BlockDesc == -1)
    {
        fprintf(stderr, memBlock->IsOwner ? "failed to create shared memory '%s': " : "failed to open shared memory '%s': ", memBlock->BlockName);
        perror("");
        return false;
    }

    if (memBlock->IsOwner)
    {
        if (ftruncate(memBlock->BlockDesc, sizeof(u32)) == -1)
        {
            fprintf(stderr, "failed to resize shared memory '%s': ", memBlock->BlockName);
            perror("");
            goto ShmCreateFail;
        }
    }

    memBlock->Memory = (u32*)mmap(NULL, sizeof(u32), PROT_READ | PROT_WRITE, MAP_SHARED, memBlock->BlockDesc, 0);
    if (memBlock->Memory == MAP_FAILED)
    {
        fprintf(stderr, "failed to map shared memory '%s': ", memBlock->BlockName);
        perror("");
        goto ShmCreateFail;
    }

    memBlock->SemaphoreRead = sem_open(memBlock->SemaphoreReadName, O_CREAT, 0666, 0);
    if (memBlock->SemaphoreRead == SEM_FAILED)
    {
        fprintf(stderr, "failed to open semaphore '%s': ", memBlock->SemaphoreReadName);
        perror("");
        goto ShmCreateFail;
    }

    memBlock->SemaphoreWrite = sem_open(memBlock->SemaphoreWriteName, O_CREAT, 0666, 0);
    if (memBlock->SemaphoreWrite == SEM_FAILED)
    {
        fprintf(stderr, "failed to open semaphore '%s': ", memBlock->SemaphoreWriteName);
        perror("");
        goto ShmCreateFail;
    }

    return true;

ShmCreateFail:
    close(memBlock->BlockDesc);
    if (memBlock->IsOwner)
        shm_unlink(memBlock->BlockName);
    sem_close(memBlock->SemaphoreRead);
    if (memBlock->IsOwner)
        sem_unlink(memBlock->SemaphoreReadName);
    return false;
}

static bool MemoryBlockRelease(MemoryBlock* memBlock)
{
    bool success = true;
    printf("destroying memory block\n");

    if (memBlock->SemaphoreRead != SEM_FAILED && sem_close(memBlock->SemaphoreRead) == -1)
    {
        fprintf(stderr, "failed to close semaphore '%s': ", memBlock->SemaphoreReadName);
        perror("");
        success = false;
    }
    if (memBlock->SemaphoreWrite != SEM_FAILED && sem_close(memBlock->SemaphoreWrite) == -1)
    {
        fprintf(stderr, "failed to close semaphore '%s': ", memBlock->SemaphoreWriteName);
        perror("");
        success = false;
    }
    if (memBlock->IsOwner)
    {
        if (memBlock->SemaphoreReadName && sem_unlink(memBlock->SemaphoreReadName) == -1)
        {
            fprintf(stderr, "failed to unlink semaphore '%s': ", memBlock->SemaphoreReadName);
            perror("");
            success = false;
        }
        if (memBlock->SemaphoreReadName && sem_unlink(memBlock->SemaphoreWriteName) == -1)
        {
            fprintf(stderr, "failed to unlink semaphore '%s': ", memBlock->SemaphoreWriteName);
            perror("");
            success = false;
        }
    }

    if (memBlock->Memory != MAP_FAILED && munmap(memBlock->Memory, sizeof(u32)) == -1)
    {
        fprintf(stderr, "failed to unmap shared memory '%s': ", memBlock->BlockName);
        perror("");
        success = false;
    }

    if (memBlock->BlockDesc != -1 && close(memBlock->BlockDesc) == -1)
    {
        fprintf(stderr, "failed to close shared memory '%s': ", memBlock->BlockName);
        perror("");
        success = false;
    }

    if (memBlock->IsOwner)
    {
        if (shm_unlink(memBlock->BlockName) == -1)
        {
            fprintf(stderr, "failed to unlink shared memory '%s': ", memBlock->BlockName);
            perror("");
            success = false;
        }
    }
    
    free(memBlock->BlockName);
    free(memBlock->SemaphoreReadName);
    free(memBlock->SemaphoreWriteName);

    memset(memBlock, 0, sizeof(MemoryBlock));
    memBlock->BlockDesc = -1;
    memBlock->SemaphoreRead = SEM_FAILED;
    memBlock->SemaphoreWrite = SEM_FAILED;
    memBlock->Memory = MAP_FAILED;

    return success;
}

static bool MemoryBlockRead(MemoryBlock* memBlock, u32* outValue)
{
    if (sem_post(memBlock->SemaphoreWrite) == -1)
    {
        fprintf(stderr, "failed to post to semaphore '%s': ", memBlock->SemaphoreWriteName);
        perror("");
        return false;
    }
    if (sem_wait(memBlock->SemaphoreRead) == -1)
    {
        fprintf(stderr, "failed to wait on semaphore '%s': ", memBlock->SemaphoreReadName);
        perror("");
        return false;
    }

    *outValue = *memBlock->Memory;
    return true;
}

static bool MemoryBlockWrite(MemoryBlock* memBlock, u32 value, bool nonBlocking)
{
    if (nonBlocking)
    {
        i32 sumValue = 0;
        if (sem_getvalue(memBlock->SemaphoreWrite, &sumValue) == -1)
        {
            fprintf(stderr, "failed to get value of semaphore '%s': ", memBlock->SemaphoreWriteName);
            perror("");
            return false;
        }
        if (sumValue <= 0)
            return false;
    }
    if (sem_wait(memBlock->SemaphoreWrite) == -1)
    {
        fprintf(stderr, "failed to wait on semaphore '%s': ", memBlock->SemaphoreWriteName);
        perror("");
        return false;
    }

    *memBlock->Memory = value;

    if (sem_post(memBlock->SemaphoreRead) == -1)
    {
        fprintf(stderr, "failed to post to semaphore '%s': ", memBlock->SemaphoreReadName);
        perror("");
        return false;
    }
    return true;
}

static bool ActionMemoryBlockRead(u32* valuePtr, void* context)
{
    return MemoryBlockRead((MemoryBlock*)context, valuePtr);
}

static bool ActionMemoryBlockWrite(u32* valuePtr, void* context)
{
    return MemoryBlockWrite((MemoryBlock*)context, *valuePtr, false);
}

static bool KillMemoryBlock(u32* killValuePtr, void* context)
{
    return MemoryBlockWrite((MemoryBlock*)context, *killValuePtr, true);
}

static bool DestructorMemoryBlock(void* context)
{
    return MemoryBlockRelease((MemoryBlock*)context);
}

static bool ComActionInitMemoryBlock(ComAction* comAction, const char* blockName, bool isWrite)
{
    comAction->Context = new(MemoryBlock);
    if (!comAction->Context)
    {
        perror("failed to allocate memory");
        return false;
    }
    char* tempBlockName = NULL;
    bool createNew = !blockName;
    if (createNew)
    {
        tempBlockName = AppendPidSuffix(isWrite ? "/ipc_memory_out" : "/ipc_memory_in");
        if (!tempBlockName)
            return false;
    }

    const char* validBlockName = createNew ? tempBlockName : blockName;

    size_t blockNameLen = strlen(validBlockName);
    char* tempSemReadName = array(char, blockNameLen + 7);
    if (!tempSemReadName)
    {
        perror("failed to allocate memory");
        return false;
    }
    strcpy(tempSemReadName, validBlockName);
    sprintf(tempSemReadName + blockNameLen, "_sem_r");

    char* tempSemWriteName = array(char, blockNameLen + 7);
    if (!tempSemWriteName)
    {
        perror("failed to allocate memory");
        return false;
    }
    strcpy(tempSemWriteName, validBlockName);
    sprintf(tempSemWriteName + blockNameLen, "_sem_w");

    bool success = MemoryBlockInit((MemoryBlock*)comAction->Context, validBlockName, tempSemReadName, tempSemWriteName, createNew);

    free(tempSemReadName);
    free(tempSemWriteName);
    free(tempBlockName);

    comAction->Action = isWrite ? &ActionMemoryBlockWrite : &ActionMemoryBlockRead;
    comAction->Kill = isWrite ? &KillMemoryBlock : NULL;
    comAction->Destructor = &DestructorMemoryBlock;

    if (createNew)
        printf(isWrite ? "out memory: %s\n" : "in memory: %s\n", ((MemoryBlock*)comAction->Context)->BlockName);
    return success;
}

//------------------- Sockets -------------------//

struct Socket
{
    i32 ConnenctionSocket;
    i32 DataSocket;
    bool IsOwner;
    struct sockaddr_un Name;
};
typedef struct Socket Socket;

static bool SocketInit(Socket* sock, const char* name, bool createNew)
{
    memset(sock, 0, sizeof(Socket));
    sock->IsOwner = createNew;
    sock->ConnenctionSocket = -1;
    sock->DataSocket = -1;

    if (createNew)
    {
        sock->ConnenctionSocket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (sock->ConnenctionSocket == -1)
        {
            fprintf(stderr, "Failed to create socket '%s': ", name);
            perror("");
            return false;
        }

        sock->Name.sun_family = AF_UNIX;
        strncpy(sock->Name.sun_path, name, sizeof(sock->Name.sun_path) - 1);

        unlink(sock->Name.sun_path);
        if (bind(sock->ConnenctionSocket, (const struct sockaddr*)&sock->Name, sizeof(sock->Name)) == -1)
        {
            fprintf(stderr, "Failed to bind socket '%s': ", sock->Name.sun_path);
            perror("");
            close(sock->ConnenctionSocket);
            return false;
        }
        if (listen(sock->ConnenctionSocket, 1) == -1)
        {
            fprintf(stderr, "Failed to listen to socket '%s': ", sock->Name.sun_path);
            perror("");
            close(sock->ConnenctionSocket);
            unlink(sock->Name.sun_path);
            return false;
        }
    }
    else
    {
        sock->DataSocket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (sock->DataSocket == -1)
        {
            fprintf(stderr, "Failed to create socket '%s': ", name);
            perror("");
            return false;
        }

        sock->Name.sun_family = AF_UNIX;
        strncpy(sock->Name.sun_path, name, sizeof(sock->Name.sun_path) - 1);

        if (connect(sock->DataSocket, (const struct sockaddr*)&sock->Name, sizeof(sock->Name)) == -1)
        {
            fprintf(stderr, "Failed to connect to socket '%s': ", sock->Name.sun_path);
            perror("");
            close(sock->DataSocket);
            return false;
        }
    }

    return true;
}

static bool SocketRelease(Socket* sock)
{
    bool success = true;
    printf("destroying socket\n");

    if (sock->DataSocket != -1 && close(sock->DataSocket) == -1)
    {
        fprintf(stderr, "Failed to close data connect on socket '%s': ", sock->Name.sun_path);
        perror("");
        success = false;
    }

    if (sock->IsOwner)
    {
        if (sock->ConnenctionSocket != -1 && close(sock->ConnenctionSocket) == -1)
        {
            fprintf(stderr, "Failed to close socket '%s': ", sock->Name.sun_path);
            perror("");
            success = false;
        }
        if (sock->Name.sun_path[0] != 0 && unlink(sock->Name.sun_path) == -1)
        {
            fprintf(stderr, "Failed to unlink socket '%s': ", sock->Name.sun_path);
            perror("");
            success = false;
        }
    }

    memset(sock, 0, sizeof(Socket));
    sock->ConnenctionSocket = -1;
    sock->DataSocket = -1;

    return success;
}

static bool SocketRead(Socket* sock, void* buffer, size_t size)
{
    if (sock->IsOwner && sock->DataSocket == -1)
        sock->DataSocket = accept(sock->ConnenctionSocket, NULL, NULL);
    if (sock->DataSocket == -1)
    {
        fprintf(stderr, "Failed to accept on socket '%s': ", sock->Name.sun_path);
        perror("");
        return false;
    }

    if (read(sock->DataSocket, buffer, size) == -1)
    {
        fprintf(stderr, "Failed to write from socket '%s': ", sock->Name.sun_path);
        perror("");
        return false;
    }
    return true;
}

static bool SocketWrite(Socket* sock, void* data, size_t size)
{
    if (sock->IsOwner && sock->DataSocket == -1)
        sock->DataSocket = accept(sock->ConnenctionSocket, NULL, NULL);
    if (sock->DataSocket == -1)
    {
        fprintf(stderr, "Failed to accept on socket '%s': ", sock->Name.sun_path);
        perror("");
        return false;
    }

    if (write(sock->DataSocket, data, size) == -1)
    {
        fprintf(stderr, "Failed to write to socket '%s': ", sock->Name.sun_path);
        perror("");
        return false;
    }
    return true;
}

static bool ActionSocketRead(u32* valuePtr, void* context)
{
    return SocketRead((Socket*)context, valuePtr, sizeof(u32));
}

static bool ActionSocketWrite(u32* valuePtr, void* context)
{
    return SocketWrite((Socket*)context, valuePtr, sizeof(u32));
}

static bool DestructorSocket(void* context)
{
    return SocketRelease((Socket*)context);
}

static bool ComActionInitSocket(ComAction* comAction, const char* socketName, bool isWrite)
{
    comAction->Context = new(Socket);
    if (!comAction->Context)
    {
        perror("failed to allocate memory");
        return false;
    }
    char* tempSocketName = NULL;
    bool createNew = !socketName;
    if (createNew)
    {
        tempSocketName = AppendPidSuffix(isWrite ? "/tmp/ipc_socket_out" : "/tmp/ipc_socket_in");
        if (!tempSocketName)
            return false;
    }

    bool success = SocketInit((Socket*)comAction->Context, createNew ? tempSocketName : socketName, createNew);
    free(tempSocketName);

    comAction->Action = isWrite ? &ActionSocketWrite : &ActionSocketRead;
    comAction->Kill = isWrite ? &ActionSocketWrite : NULL;
    comAction->Destructor = &DestructorSocket;

    if (!socketName)
        printf(isWrite ? "out socket: %s\n" : "in socket: %s\n", ((Socket*)comAction->Context)->Name.sun_path);
    return success;
}

//------------------- ZMQ -------------------//

//TODO: add zmq support

static inline bool IsOption(const char* arg)
{
    return arg[0] == '-';
}

#define TYPE_INVALID -1
#define TYPE_NAMEDPIPE 0
#define TYPE_SHAREDMEM 1
#define TYPE_SOCKET 2
#define KILL_SIGNAL UINT32_MAX

ComAction g_ComActionIn;
ComAction g_ComActionOut;
bool g_ShouldKill;

#define CLEANUP_ON_ERROR(_func) if (!_func) Shutdown(-2)

static void Shutdown(i32 signum)
{
    printf("\nCleaning up:\n");
    ComActionRelease(&g_ComActionIn);
    if (g_ShouldKill && ComActionKill(&g_ComActionOut, KILL_SIGNAL))
        printf("sent: kill\n");
    ComActionRelease(&g_ComActionOut);

    exit(signum != -1 ? EXIT_FAILURE : EXIT_SUCCESS);
}

i32 main(i32 argc, char** argv)
{
    i32 inType = TYPE_INVALID;
    i32 outType = TYPE_INVALID;
    const char* inName = NULL;
    const char* outName = NULL;

    bool shouldStart = false;
    g_ShouldKill = false;
    bool shouldStop = false;
    u32 stopValue = KILL_SIGNAL;

    if (argc <= 1)
    {
        printf(
            "Usage:\n"
            "  mixed_ipc -it (np|shm|so) [-in <in_name>] -ot (np|shm|so) [-on <out_name>] [-s] [-k [<kill_value>]]\n");
        exit(EXIT_SUCCESS);
    }

    for (i32 i = 1; i < argc; ++i)
    {
        bool isInTypeOption = strcmp("-it", argv[i]) == 0;
        bool isOutTypeOption = strcmp("-ot", argv[i]) == 0;
        if (isInTypeOption || isOutTypeOption)
        {
            if (isInTypeOption && inType != TYPE_INVALID)
            {
                fprintf(stderr, "repeat argument -it\n");
                exit(EXIT_FAILURE);
            }
            if (isOutTypeOption && outType != TYPE_INVALID)
            {
                fprintf(stderr, "repeat argument -ot\n");
                exit(EXIT_FAILURE);
            }

            i32* typePtr = isInTypeOption ? &inType : &outType;
            ++i;
            if (i >= argc || IsOption(argv[i]))
            {
                fprintf(stderr, isInTypeOption ? "<in_type> is missing\n" : "<out_type> is missing\n");
                exit(EXIT_FAILURE);
            }

            if (strcmp("np", argv[i]) == 0)
                *typePtr = TYPE_NAMEDPIPE;
            else if (strcmp("shm", argv[i]) == 0)
                *typePtr = TYPE_SHAREDMEM;
            else if (strcmp("so", argv[i]) == 0)
                *typePtr = TYPE_SOCKET;
            else
            {
                fprintf(stderr, isInTypeOption ? "invalid <in_type>\n" : "invalid <out_type>\n");
                exit(EXIT_FAILURE);
            }

            continue;
        }

        bool isInNameOption = strcmp("-in", argv[i]) == 0;
        bool isOutNameOption = strcmp("-on", argv[i]) == 0;
        if (isInNameOption || isOutNameOption)
        {
            if (isInNameOption && inName)
            {
                fprintf(stderr, "repeat argument -in\n");
                exit(EXIT_FAILURE);
            }
            if (isOutNameOption && outName)
            {
                fprintf(stderr, "repeat argument -on\n");
                exit(EXIT_FAILURE);
            }

            const char** namePtr = isInNameOption ? &inName : &outName;
            ++i;
            if (i >= argc || IsOption(argv[i]))
            {
                fprintf(stderr, isInNameOption ? "<in_name> is missing\n" : "<out_name> is missing\n");
                exit(EXIT_FAILURE);
            }

            *namePtr = argv[i];
            continue;
        }

        if (strcmp("-s", argv[i]) == 0)
        {
            if (shouldStart)
            {
                fprintf(stderr, "repeat argument -s\n");
                exit(EXIT_FAILURE);
            }
            shouldStart = true;
            continue;
        }

        if (strcmp("-k", argv[i]) == 0)
        {
            if (g_ShouldKill)
            {
                fprintf(stderr, "repeat argument -k\n");
                exit(EXIT_FAILURE);
            }
            g_ShouldKill = true;

            if (i + 1 >= argc || IsOption(argv[i + 1]))
                continue;
            ++i;

            char* killValueEnd = NULL;
            stopValue = strtol(argv[i], &killValueEnd, 10);
            if (killValueEnd == argv[i] || stopValue < 0)
            {
                fprintf(stderr, "invalid <stop_value>\n");
                exit(EXIT_FAILURE);
            }
            shouldStop = true;
            continue;
        }

        fprintf(stderr, "invalid argument: %s\n", argv[i]);
        exit(EXIT_FAILURE);
    }

    if (inType == TYPE_INVALID)
        fprintf(stderr, "option '-it' missing\n");
    if (outType == TYPE_INVALID)
        fprintf(stderr, "option '-ot' missing\n");
    if (inType == TYPE_INVALID || outType == TYPE_INVALID)
        exit(EXIT_FAILURE);

    g_ComActionIn = ComActionEmpty();
    g_ComActionOut = ComActionEmpty();

    if (signal(SIGINT, &Shutdown) == SIG_ERR)
    {
        perror("failed to attach interrupt handler");
        exit(EXIT_FAILURE);
    }

    switch (inType)
    {
    case TYPE_NAMEDPIPE:
        CLEANUP_ON_ERROR(ComActionInitNamedPipe(&g_ComActionIn, inName, false));
        break;
    case TYPE_SHAREDMEM:
        CLEANUP_ON_ERROR(ComActionInitMemoryBlock(&g_ComActionIn, inName, false));
        break;
    case TYPE_SOCKET:
        CLEANUP_ON_ERROR(ComActionInitSocket(&g_ComActionIn, inName, false));
        break;
    default:
        exit(EXIT_FAILURE);
        break;
    }

    switch (outType)
    {
    case TYPE_NAMEDPIPE:
        CLEANUP_ON_ERROR(ComActionInitNamedPipe(&g_ComActionOut, outName, true));
        break;
    case TYPE_SHAREDMEM:
        CLEANUP_ON_ERROR(ComActionInitMemoryBlock(&g_ComActionOut, outName, true));
        break;
    case TYPE_SOCKET:
        CLEANUP_ON_ERROR(ComActionInitSocket(&g_ComActionOut, outName, true));
        break;
    default:
        exit(EXIT_FAILURE);
        break;
    }

    if (shouldStart)
    {
        u32 startValue = 0;
        CLEANUP_ON_ERROR(ComActionExec(&g_ComActionOut, &startValue));
        printf("sent: %d\n", startValue);
    }

    while (true)
    {
        u32 value = 0;
        CLEANUP_ON_ERROR(ComActionExec(&g_ComActionIn, &value));
        if (g_ShouldKill && value == KILL_SIGNAL)
        {
            printf("received: kill\n");
            break;
        }
        printf("received: %d\n", value);

        if (shouldStop && value > stopValue)
            break;

        ++value;
        if (g_ShouldKill && value == KILL_SIGNAL)
            ++value;

        CLEANUP_ON_ERROR(ComActionExec(&g_ComActionOut, &value));
        printf("sent: %d\n", value);
    }

    Shutdown(-1);
}
