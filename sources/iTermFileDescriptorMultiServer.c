//
//  iTermFileDescriptorMultiServer.c
//  iTerm2
//
//  Created by George Nachman on 7/22/19.
//

#include "iTermFileDescriptorMultiServer.h"

#import "iTermFileDescriptorServer.h"
#import "iTermMultiServerProtocol.h"
#import "iTermPosixTTYReplacements.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <util.h>

static int gPipe[2];
static char *gPath;

typedef struct {
    iTermMultiServerClientOriginatedMessage messageWithLaunchRequest;
    pid_t pid;
    int terminated;  // Nonzero if process is terminated and wait()ed on.
    int masterFd;  // Valid only if not terminated is false.
    int status;  // Only valid if terminated. Gives status from wait.
} iTermMultiServerChild;

static iTermMultiServerChild *children;
static int numberOfChildren;

#pragma mark - Signal handlers

static void SigChildHandler(int arg) {
    // Wake the select loop.
    write(gPipe[1], "", 1);
}

static void SigUsr1Handler(int arg) {
    unlink(gPath);
    _exit(1);
}

#pragma mark - POSIX

static ssize_t RecvMsg(int fd,
                       iTermClientServerProtocolMessage *message) {
    assert(message->valid == ITERM_MULTISERVER_MAGIC);

    ssize_t n;
    while (1) {
        n = recvmsg(fd, &message->message, 0);
    } while (n < 0 && errno == EINTR);

    return n;
}

#pragma mark - Mutate Children

static void AddChild(const iTermMultiServerRequestLaunch *launch,
                     int masterFd,
                     const iTermForkState *forkState) {
    if (!children) {
        children = malloc(sizeof(iTermMultiServerChild));
    } else {
        children = realloc(children, (numberOfChildren + 1) * sizeof(iTermMultiServerChild));
    }
    const int i = numberOfChildren;
    numberOfChildren += 1;
    iTermMultiServerClientOriginatedMessage tempClientMessage = {
        .type = iTermMultiServerRPCTypeLaunch,
        .payload = {
            .launch = *launch
        }
    };
    iTermClientServerProtocolMessage tempMessage;
    iTermClientServerProtocolMessageInitialize(&tempMessage);
    int status;
    status = iTermMultiServerProtocolEncodeMessageFromClient(&tempClientMessage, &tempMessage);
    assert(status == 0);
    status = iTermMultiServerProtocolParseMessageFromClient(&tempMessage,
                                                            &children[i].messageWithLaunchRequest);
    assert(status == 0);
    children[i].masterFd = masterFd;
    children[i].pid = forkState->pid;
    children[i].terminated = 0;
    children[i].status = 0;
}

static void RemoveChild(int i) {
    assert(i >= 0);
    assert(i < numberOfChildren);

    if (numberOfChildren == 1) {
        free(children);
        children = NULL;
    } else {
        const int afterCount = numberOfChildren - i - 1;
        memmove(children + i,
                children + i + 1,
                sizeof(*children) * afterCount);
        children = realloc(children, sizeof(*children) * (numberOfChildren - 1));
    }

    numberOfChildren -= 1;
}

#pragma mark - Launch

static int Launch(const iTermMultiServerRequestLaunch *launch,
                  iTermForkState *forkState,
                  int *errorPtr) {
    iTermTTYState ttyState;
    iTermTTYStateInitialize(&ttyState, launch->width, launch->height, launch->isUTF8);
    int fd;
    forkState->numFileDescriptorsToPreserve = 3;
    forkState->pid = forkpty(&fd, ttyState.tty, &ttyState.term, &ttyState.win);
    if (forkState->pid == (pid_t)0) {
        // Child
        iTermExec(launch->path,
                  (const char **)launch->argv,
                  1,
                  forkState,
                  launch->pwd,
                  launch->envp);
        return -1;
    } else {
        *errorPtr = errno;
    }
    return fd;
}

static int SendLaunchResponse(int fd, int status, pid_t pid, int masterFd) {
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeLaunch,
        .payload = {
            .launch = {
                .status = status,
                .pid = pid
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t result = iTermFileDescriptorServerSendMessageAndFileDescriptor(fd,
                                                                           obj.ioVectors[0].iov_base,
                                                                           obj.ioVectors[0].iov_len,
                                                                           masterFd);
    return result > 0;
}

static int HandleLaunchRequest(int fd, const iTermMultiServerRequestLaunch *launch) {
    int error;
    iTermForkState forkState = {
        .connectionFd = -1,
        .deadMansPipe = { 0, 0 },
    };
    int masterFd = Launch(launch, &forkState, &error);
    if (masterFd < 0) {
        return SendLaunchResponse(fd, -1, 0, -1);
    } else {
        AddChild(launch, masterFd, &forkState);
        return SendLaunchResponse(fd, 0, forkState.pid, masterFd);
    }
}

#pragma mark - Report Termination

static int ReportTermination(int fd, pid_t pid, int status) {
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeTermination,
        .payload = {
            .termination = {
                .pid = pid,
                .status = status
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t result = iTermFileDescriptorServerSendMessage(fd,
                                                          obj.ioVectors[0].iov_base,
                                                          obj.ioVectors[0].iov_len);
    return result > 0;
}

#pragma mark - Report Child

static int ReportChild(int fd, const iTermMultiServerChild *child, int isLast) {
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeReportChild,
        .payload = {
            .reportChild = {
                .isLast = isLast,
                .pid = child->pid,
                .path = child->messageWithLaunchRequest.payload.launch.path,
                .argv = child->messageWithLaunchRequest.payload.launch.argv,
                .argc = child->messageWithLaunchRequest.payload.launch.argc,
                .envp = child->messageWithLaunchRequest.payload.launch.envp,
                .envc = child->messageWithLaunchRequest.payload.launch.envc,
                .isUTF8 = child->messageWithLaunchRequest.payload.launch.isUTF8,
                .pwd = child->messageWithLaunchRequest.payload.launch.pwd
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t bytes = iTermFileDescriptorServerSendMessageAndFileDescriptor(fd,
                                                                          obj.ioVectors[0].iov_base,
                                                                          obj.ioVectors[0].iov_len,
                                                                          child->masterFd);
    return bytes >= 0;
}

#pragma mark - Dead Child Handling

static int ReportAndRemoveDeadChild(int connectionFd, int i) {
    if (ReportTermination(connectionFd, children[i].pid, children[i].status)) {
        return -1;
    }
    RemoveChild(i);
    return 0;
}

static int ReportAndRemoveDeadChildren(int connectionFd) {
    for (int i = 0; i < numberOfChildren; i++) {
        if (children[i].terminated) {
            continue;
        }
        const pid_t pid = waitpid(children[i].pid, &children[i].status, WNOHANG);
        if (pid == -1) {
            children[i].terminated = 1;
            return ReportAndRemoveDeadChild(connectionFd, i);
        }
    }
    return 0;
}

#pragma mark - Report Children

static int ReportChildren(int fd) {
    // Iterate backwards because ReportAndRemoveDeadChild deletes the index passed to it.
    for (int i = numberOfChildren - 1; i >= 0; i--) {
        if (children[i].terminated) {
            if (ReportAndRemoveDeadChild(fd, i)) {
                return -1;
            }
            // NOTE: children is now a different pointer because it has been realloced.
        } else {
            if (ReportChild(fd, &children[i], i + 1 == numberOfChildren)) {
                return -1;
            }
        }
    }
    return 0;
}

#pragma mark - Requests

static int ReadRequest(int fd, iTermMultiServerClientOriginatedMessage *out) {
    iTermClientServerProtocolMessage message;
    iTermClientServerProtocolMessageInitialize(&message);

    const ssize_t recvStatus = RecvMsg(fd, &message);
    int status = 0;
    if (recvStatus <= 0) {
        status = -1;
        goto done;
    }

    memset(out, 0, sizeof(*out));

    if (iTermMultiServerProtocolParseMessageFromClient(&message, out)) {
        status = -1;
        goto done;
    }

done:
    if (status) {
        iTermMultiServerClientOriginatedMessageFree(out);
    }
    iTermClientServerProtocolMessageFree(&message);
    return status;
}

static int ReadAndHandleRequest(int fd) {
    iTermMultiServerClientOriginatedMessage request;
    if (!ReadRequest(fd, &request)) {
        return -1;
    }
    switch (request.type) {
        case iTermMultiServerRPCTypeLaunch:
            return HandleLaunchRequest(fd, &request.payload.launch);
        case iTermMultiServerRPCTypeTermination:
        case iTermMultiServerRPCTypeReportChild:
            break;
    }
    iTermMultiServerClientOriginatedMessageFree(&request);
    return 0;
}

#pragma mark - Core

// There is a client connected. Respond to requests from it until it disconnects, then return.
static void SelectLoop(int connectionFd) {
    while (1) {
        int fds[2] = { gPipe[0], connectionFd };
        int results[2];
        iTermSelect(fds, sizeof(fds) / sizeof(*fds), results);
        if (results[0]) {
            if (ReportAndRemoveDeadChildren(connectionFd)) {
                break;
            }
        }
        if (results[1]) {
            if (ReadAndHandleRequest(connectionFd)) {
                break;
            }
        }
    }
    close(connectionFd);
}

static int PerformAcceptActivity(int socketFd) {
    int connectionFd = iTermFileDescriptorServerAccept(socketFd);
    if (connectionFd == -1) {
        FDLog(LOG_DEBUG, "accept failed %s", strerror(errno));
        return -1;
    }

    if (ReportChildren(connectionFd)) {
        return 0;
    }
    SelectLoop(connectionFd);
    return 0;
}

static void MainLoop(char *path, int initialConnectionFd) {
    // Listen on a Unix Domain Socket.
    FDLog(LOG_DEBUG, "Entering main loop.");
    SelectLoop(initialConnectionFd);

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

#pragma mark - Bootstrap

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

