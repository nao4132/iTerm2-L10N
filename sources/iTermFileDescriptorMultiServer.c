//
//  iTermFileDescriptorMultiServer.c
//  iTerm2
//
//  Created by George Nachman on 7/22/19.
//

#include "iTermFileDescriptorServer.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

static int gPipe[2];
static char *gPath;

static void SigChildHandler(int arg) {
    // TODO: Wait if needed
}

static void SigUsr1Handler(int arg) {
    unlink(gPath);
    _exit(1);
}

static int Initialize(char *path) {
    openlog("iTerm2-Server", LOG_PID | LOG_NDELAY, LOG_USER);
    setlogmask(LOG_UPTO(LOG_DEBUG));
    FDLog(LOG_DEBUG, "Server starting Initialize()");
    gPath = strdup(path);
    // We get this when iTerm2 crashes. Ignore it.
    FDLog(LOG_DEBUG, "Installing SIGHUP handler.");
    signal(SIGHUP, SIG_IGN);

    pipe(gPipe);

    FDLog(LOG_DEBUG, "Installing SIGCHLD handler.");
    signal(SIGCHLD, SigChildHandler);
    signal(SIGUSR1, SigUsr1Handler);

    // Unblock SIGCHLD.
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGCHLD);
    FDLog(LOG_DEBUG, "Unblocking SIGCHLD.");
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);

    return 0;
}

typedef enum {
    iTermMultiServerTagType,

    iTermMultiServerTagLaunchPath,
    iTermMultiServerTagLaunchArgv,
    iTermMultiServerTagLaunchEnvironment,

    iTermMultiServerTagAttachPid,

    iTermMultiServerTagTerminationPid,
    iTermMultiServerTagTerminationStatus,

} iTermMultiServerTagLaunch;

typedef struct {
    // iTermMultiServerTagLaunchPath
    char *path;

    // iTermMultiServerTagLaunchArgv
    char **argv;
    int argc;

    // iTermMultiServerTagLaunchEnvironment
    char **envp;
    int envc;
} iTermMultiServerRequestLaunch;

typedef struct {
    // 0 means success. Otherwise, gives errno from fork or execve.
    int status;

    // Only defined if status is 0.
    pid_t pid;
} iTermMultiServerResponseLaunch;

typedef struct {
    // iTermMultiServerTagAttachPid
    pid_t pid;
} iTermMultiServerRequestAttach;

typedef struct {
    // 0 means success.
    int status;
} iTermMultiServerResponseAttach;

typedef enum {
    iTermMultiServerRPCTypeLaunch,
    iTermMultiServerRPCTypeAttach,
    iTermMultiServerRPCTypeTermination
} iTermMultiServerRPCType;

typedef struct {
    pid_t pid;
    int status;  // see stat_loc argument to wait(2)
} iTermMultiServerNotificationTermination;


typedef struct {
    iTermMultiServerRPCType type;
    union {
        iTermMultiServerRequestLaunch launch;
        iTermMultiServerRequestAttach attach;
    } payload;
} iTermMultiServerRequest;

typedef struct {
    iTermMultiServerRPCType type;
    union {
        iTermMultiServerResponseLaunch launch;
        iTermMultiServerResponseAttach attach;
        iTermMultiServerNotificationTermination termination;
    } payload;
} iTermMultiServerResponse;

#define ITERM_MULTISERVER_MAX_IO_VECTORS 5
const size_t ITERM_MULTISERVER_BUFFER_SIZE = 65536;
static const int ITERM_MULTISERVER_MAGIC = 0xdeadbeef;

typedef struct {
    int valid;
    struct msghdr message;
    iTermFileDescriptorControlMessage controlBuffer;
    struct iovec ioVectors[ITERM_MULTISERVER_MAX_IO_VECTORS];
} iTermMultiServerMessage;

static void iTermMultiServerMessageInitialize(iTermMultiServerMessage *message) {
    memset(message, 0, sizeof(*message));
    for (int i = 0; i < ITERM_MULTISERVER_MAX_IO_VECTORS; i++) {
        message->ioVectors[i].iov_base = malloc(ITERM_MULTISERVER_BUFFER_SIZE);
        message->ioVectors[i].iov_len = ITERM_MULTISERVER_BUFFER_SIZE;
    }
    message->message.msg_iov = message->ioVectors;
    message->message.msg_iovlen = ITERM_MULTISERVER_MAX_IO_VECTORS;

    message->message.msg_name = NULL;
    message->message.msg_namelen = 0;

    message->message.msg_control = &message->controlBuffer;
    message->message.msg_controllen = sizeof(message->controlBuffer);
    message->valid = ITERM_MULTISERVER_MAGIC;
}

static void iTermMultiServerMessageFree(iTermMultiServerMessage *message) {
    for (int i = 0; i < ITERM_MULTISERVER_MAX_IO_VECTORS; i++) {
        free(message->ioVectors[i].iov_base);
    }
    memset(message, 0, sizeof(*message));
}

static ssize_t RecvMsg(int fd, iTermMultiServerMessage *message) {
    assert(message->valid == ITERM_MULTISERVER_MAGIC);

    ssize_t n;
    while (1) {
        n = recvmsg(fd, &message->message, 0);
    } while (n < 0 && errno == EINTR);

    return n;
}

typedef struct {
    int iovec;
    ssize_t offset;
    iTermMultiServerMessage *message;
} iTermMultiServerMessageParser;

static void ParserCopyAndAdvance(iTermMultiServerMessageParser *parser,
                                 void *out,
                                 size_t size) {
    assert(ParserBytesLeft(parser) >= size);
    assert(parser->message->ioVectors[parser->iovec].iov_len == size);
    memmove(out, parser->message->ioVectors[parser->iovec].iov_base + parser->offset, size);
    parser->offset += size;
    if (parser->offset == parser->message->ioVectors[parser->iovec].iov_len) {
        parser->offset = 0;
        parser->iovec++;
    }
}

static size_t ParserPeekSize(iTermMultiServerMessageParser *parser) {
    if (parser->iovec >= ITERM_MULTISERVER_MAX_IO_VECTORS) {
        return 0;
    }
    const ssize_t iov_len = parser->message->ioVectors[parser->iovec].iov_len;
    assert(iov_len >= parser->offset);
    return iov_len - parser->offset;
}

static int ParseInt(iTermMultiServerMessageParser *parser,
                    void *out,
                    size_t size) {
    if (ParserPeekSize(parser) != size) {
        return -1;
    }
    ParserCopyAndAdvance(parser, out, size);
    return 0;
}

static int ParseTaggedInt(iTermMultiServerMessageParser *parser,
                          void *out,
                          size_t size,
                          int tag) {
    int actualTag;
    if (ParseInt(parser, &actualTag, sizeof(actualTag))) {
        return -1;
    }
    if (actualTag != tag) {
        return -1;
    }
    return ParseInt(parser, out, size);
}

static int ParseAttach(iTermMultiServerMessageParser *parser,
                       iTermMultiServerRequestAttach *out) {
    return ParseTaggedInt(parser,
                          &out->pid,
                          sizeof(out->pid),
                          iTermMultiServerTagAttachPid);
}

static int ParseString(iTermMultiServerMessageParser *parser,
                       char **out) {
    int length;
    if (ParseInt(parser, &length, sizeof(int))) {
        return -1;
    }
    if (ParserPeekSize(parser) < length) {
        return -1;
    }
    *out = malloc(length) + 1;
    ParserCopyAndAdvance(parser, *out, length);
    (*out)[length] = '\0';
    return 0;
}

static int ParseTaggedString(iTermMultiServerMessageParser *parser,
                             char **out,
                             int tag) {
    int actualTag;
    if (ParseInt(parser, &actualTag, sizeof(actualTag))) {
        return -1;
    }
    return ParseString(parser, out);
}

static int ParseStringArray(iTermMultiServerMessageParser *parser,
                            char ***arrayOut,
                            int *countOut) {
    if (ParseInt(parser, countOut, sizeof(*countOut))) {
        return -1;
    }
    *arrayOut = malloc(sizeof(char *) * *countOut);
    for (int i = 0; i < *countOut; i++) {
        if (ParseString(parser, &(*arrayOut)[i])) {
            return -1;
        }
    }
    return 0;
}

static int ParseTaggedStringArray(iTermMultiServerMessageParser *parser,
                                  char ***arrayOut,
                                  int *countOut,
                                  int tag) {
    int actualTag;
    if (ParseInt(parser, &actualTag, sizeof(actualTag))) {
        return -1;
    }
    if (actualTag != tag) {
        return -1;
    }
    return ParseStringArray(parser, arrayOut, countOut);
}

static int ParseLaunch(iTermMultiServerMessageParser *parser,
                       iTermMultiServerRequestLaunch *out) {
    if (ParseTaggedString(parser, &out->path, iTermMultiServerTagLaunchPath)) {
        return -1;
    }
    if (ParseTaggedStringArray(parser, &out->argv, &out->argc, iTermMultiServerTagLaunchArgv)) {
        return -1;
    }
    if (ParseTaggedStringArray(parser, &out->envp, &out->envc, iTermMultiServerTagLaunchEnvironment)) {
        return -1;
    }
    return 0;
}

static int ParseMessage(iTermMultiServerMessage *message,
                        iTermMultiServerRequest *out) {
    iTermMultiServerMessageParser parser = {
        .iovec = 0,
        .message = message
    };

    if (ParseTaggedInt(&parser, &out->type, sizeof(out->type))) {
        return -1;
    }
    switch (out->type) {
        case iTermMultiServerRequestTypeAttach:
            return ParseAttach(&parser, &out->payload.attach);
        case iTermMultiServerRequestTypeLaunch:
            return ParseLaunch(&parser, &out->payload.launch);
    }
    return -1;
}

static int ReadRequest(int fd, iTermMultiServerRequest *out) {
    iTermMultiServerMessage message;
    iTermMultiServerMessageInitialize(&message);

    const ssize_t recvStatus = RecvMsg(fd, &message);
    int status = 0;
    if (recvStatus <= 0) {
        status = -1;
        goto done;
    }

    if (ParseMessage(&message, out)) {
        status = -1;
        goto done;
    }

done:
    iTermMultiServerMessageFree(&message);
    return status;
}

// There is a client connected. Respond to requests from it until it disconnects, then return.
static void HandleRequests(int fd) {
    while (1) {
        iTermMultiServerRequest request;
        if (!ReadRequest(fd, &request)) {
            return;
        }
        RespondToRequest(fd, request);
    }
}

static int PerformAcceptActivity(int socketFd) {
    int connectionFd = iTermFileDescriptorServerAccept(socketFd);
    if (connectionFd == -1) {
        FDLog(LOG_DEBUG, "accept failed %s", strerror(errno));
        return -1;
    }

    HandleRequests(connectionFd);
    return 0;
}

static void MainLoop(char *path, int initialConnectionFd) {
    // Listen on a Unix Domain Socket.
    FDLog(LOG_DEBUG, "Entering main loop.");
    HandleRequests(initialConnectionFd);

    int socketFd;
    do {
        // You get here after the connection is lost.
        FDLog(LOG_DEBUG, "Calling iTermFileDescriptorServerSocketBindListen.");
        socketFd = iTermFileDescriptorServerSocketBindListen(path);
        if (socketFd < 0) {
            FDLog(LOG_DEBUG, "iTermFileDescriptorServerSocketBindListen failed");
            return;
        }
        FDLog(LOG_DEBUG, "Calling PerformAcceptActivity");
    } while (!PerformAcceptActivity(socketFd));
}

int iTermFileDescriptorMultiServerRun(char *path, int connectionFd) {
    SetRunningServer();
    // syslog raises sigpipe when the parent job dies on 10.12.
    signal(SIGPIPE, SIG_IGN);
    int rc = Initialize(path);
    if (rc) {
        FDLog(LOG_DEBUG, "Initialize failed with code %d", rc);
    } else {
        MainLoop(path, connectionFd);
        // MainLoop never returns, except by dying on a signal.
    }
    FDLog(LOG_DEBUG, "Unlink %s", path);
    unlink(path);
    return 1;
}

