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

    // Copy the launch request into children[i].messageWithLaunchRequest. This is done because we
    // need to own our own pointers to arrays of strings.
    iTermClientServerProtocolMessage tempMessage;
    iTermClientServerProtocolMessageInitialize(&tempMessage);
    int status;
    status = iTermMultiServerProtocolEncodeMessageFromClient(&tempClientMessage, &tempMessage);
    assert(status == 0);
    status = iTermMultiServerProtocolParseMessageFromClient(&tempMessage,
                                                            &children[i].messageWithLaunchRequest);
    assert(status == 0);
    iTermClientServerProtocolMessageFree(&tempMessage);

    // Update for the remaining fields in children[i].
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

    ssize_t result;
    if (masterFd >= 0) {
        // Happy path. Send the file descrptor.
        ssize_t result = iTermFileDescriptorServerSendMessageAndFileDescriptor(fd,
                                                                               obj.ioVectors[0].iov_base,
                                                                               obj.ioVectors[0].iov_len,
                                                                               masterFd);
    } else {
        // Error happened. Don't send a file descriptor.
        result = iTermFileDescriptorServerSendMessage(fd,
                                                      obj.ioVectors[0].iov_base,
                                                      obj.ioVectors[0].iov_len);
    }
    iTermClientServerProtocolMessageFree(&obj);
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

static int ReportTermination(int fd, pid_t pid) {
    FDLog(LOG_DEBUG, "Report termination pid %d", (int)pid);

    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeTermination,
        .payload = {
            .termination = {
                .pid = pid,
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t result = iTermFileDescriptorServerSendMessage(fd,
                                                          obj.ioVectors[0].iov_base,
                                                          obj.ioVectors[0].iov_len);
    iTermClientServerProtocolMessageFree(&obj);
    return result > 0;
}

#pragma mark - Report Child

static void PopulateReportChild(const iTermMultiServerChild *child, int isLast, iTermMultiServerReportChild *out) {
    iTermMultiServerReportChild temp = {
        .isLast = isLast,
        .pid = child->pid,
        .path = child->messageWithLaunchRequest.payload.launch.path,
        .argv = child->messageWithLaunchRequest.payload.launch.argv,
        .argc = child->messageWithLaunchRequest.payload.launch.argc,
        .envp = child->messageWithLaunchRequest.payload.launch.envp,
        .envc = child->messageWithLaunchRequest.payload.launch.envc,
        .isUTF8 = child->messageWithLaunchRequest.payload.launch.isUTF8,
        .pwd = child->messageWithLaunchRequest.payload.launch.pwd
    };
    *out = temp;
}

static int ReportChild(int fd, const iTermMultiServerChild *child, int isLast) {
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeReportChild,
    };
    PopulateReportChild(child, isLast, &message.payload.reportChild);
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t bytes = iTermFileDescriptorServerSendMessageAndFileDescriptor(fd,
                                                                          obj.ioVectors[0].iov_base,
                                                                          obj.ioVectors[0].iov_len,
                                                                          child->masterFd);
    iTermClientServerProtocolMessageFree(&obj);
    return bytes >= 0;
}

#pragma mark - Termination Handling

static pid_t WaitPidNoHang(pid_t pid, int *statusOut) {
    pid_t result;
    do {
        result = waitpid(pid, statusOut, WNOHANG);;
    } while (result < 0 && errno == EINTR);
    return result;
}

static int WaitForAllProcessesAndReportTerminations(int connectionFd) {
    FDLog(LOG_DEBUG, "WaitForAllProcessesAndReportTerminations");
    for (int i = 0; i < numberOfChildren; i++) {
        if (children[i].terminated) {
            continue;
        }
        const pid_t pid = WaitPidNoHang(children[i].pid, &children[i].status);
        if (pid > 0) {
            children[i].terminated = 1;
            if (ReportTermination(connectionFd, i)) {
                return -1;
            }
        }
    }
    return 0;
}

#pragma mark - Report Children

static int ReportChildren(int fd) {
    // Iterate backwards because ReportAndRemoveDeadChild deletes the index passed to it.
    for (int i = numberOfChildren - 1; i >= 0; i--) {
        if (ReportChild(fd, &children[i], i + 1 == numberOfChildren)) {
            return -1;
        }
    }
    return 0;
}

#pragma mark - Handshake

static int HandleHandshake(int fd, iTermMultiServerRequestHandshake *handshake) {
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    if (handshake->maximumProtocolVersion < iTermMultiServerProtocolVersion1) {
        FDLog(LOG_ERR, "Maximum protocol version is too low: %d", handshake->maximumProtocolVersion);
        return -1;
    }
    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeHandshake,
        .payload = {
            .handshake = {
                .protocolVersion = iTermMultiServerProtocolVersion1,
                .numChildren = numberOfChildren,
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t bytes = iTermFileDescriptorServerSendMessage(fd,
                                                         obj.ioVectors[0].iov_base,
                                                         obj.ioVectors[0].iov_len);
    iTermClientServerProtocolMessageFree(&obj);
    if (bytes < 0) {
        return -1;
    }
    return ReportChildren(fd);
}

#pragma mark - Wait

static int GetChildIndexByPID(pid_t pid) {
    for (int i = 0; i < numberOfChildren; i++) {
        if (children[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

static int HandleWait(int fd, iTermMultiServerRequestWait *wait) {
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    int childIndex = GetChildIndexByPID(wait->pid);
    int status = 0;
    int errorNumber = 0;
    if (childIndex < 0) {
        errorNumber = -1;
    } else if (!children[childIndex].terminated) {
        errorNumber = -2;
    } else {
        status = children[childIndex].status;
    }
    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeWait,
        .payload = {
            .wait = {
                .pid = wait->pid,
                .status = status,
                .errorNumber = errorNumber
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t bytes = iTermFileDescriptorServerSendMessage(fd,
                                                         obj.ioVectors[0].iov_base,
                                                         obj.ioVectors[0].iov_len);
    iTermClientServerProtocolMessageFree(&obj);
    if (bytes < 0) {
        return -1;
    }

    if (errorNumber == 0) {
        RemoveChild(childIndex);
    }
    return 0;
}

#pragma mark - Requests

static int ReadRequest(int fd, iTermMultiServerClientOriginatedMessage *out) {
    iTermClientServerProtocolMessage message;

    int status = iTermMultiServerRecv(fd, &message);
    if (status) {
        goto done;
    }

    memset(out, 0, sizeof(*out));

    status = iTermMultiServerProtocolParseMessageFromClient(&message, out);
    iTermClientServerProtocolMessageFree(&message);

done:
    if (status) {
        iTermMultiServerClientOriginatedMessageFree(out);
    }
    return status;
}

static int ReadAndHandleRequest(int fd) {
    iTermMultiServerClientOriginatedMessage request;
    if (!ReadRequest(fd, &request)) {
        return -1;
    }
    switch (request.type) {
        case iTermMultiServerRPCTypeHandshake:
            return HandleHandshake(fd, &request.payload.handshake);
        case iTermMultiServerRPCTypeWait:
            return HandleWait(fd, &request.payload.wait);
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

static void AcceptAndReject(int socket) {
    int fd = iTermFileDescriptorServerAccept(socket);
    if (fd < 0) {
        return;
    }

    FDLog(LOG_DEBUG, "Received connection while already connected");

    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeHandshake,
        .payload = {
            .handshake = {
                .protocolVersion = iTermMultiServerProtocolVersionRejected,
                .numChildren = 0,
            }
        }
    };
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);
    iTermFileDescriptorServerSendMessage(fd,
                                         obj.ioVectors[0].iov_base,
                                         obj.ioVectors[0].iov_len)
    iTermClientServerProtocolMessageFree(&obj);
    
    close(fd);
}

// There is a client connected. Respond to requests from it until it disconnects, then return.
static void SelectLoop(int socketFd, int connectionFd) {
    FDLog(LOG_DEBUG, "Begin SelectLoop.");
    while (1) {
        int fds[2] = { gPipe[0], connectionFd, socketFd };
        int results[2];
        iTermSelect(fds, sizeof(fds) / sizeof(*fds), results);
        if (results[0]) {
            if (WaitForAllProcessesAndReportTerminations(connectionFd)) {
                break;
            }
        }
        if (results[1]) {
            if (ReadAndHandleRequest(connectionFd)) {
                break;
            }
        }
        if (results[2]) {
            AcceptAndReject(socketFD);
        }
    }
    FDLog(LOG_DEBUG, "Exited select loop.");
    close(connectionFd);
}

// Alternates between running the select loop and accepting a new connection.
static void MainLoop(char *path, int socketFd, int initialConnectionFd) {
    FDLog(LOG_DEBUG, "Entering main loop.");
    assert(socketFd >= 0);
    assert(initialConnectionFd >= 0);
    assert(socketFd != initialConnectionFd);

    int connectionFd = initialConnectionFd;
    do {
        SelectLoop(socketFd, connectionFd);

        // You get here after the connection is lost. Listen and accept.
        FDLog(LOG_DEBUG, "Calling accept");
        connectionFd = iTermFileDescriptorServerAccept(socketFd);
        if (connectionFd == -1) {
            FDLog(LOG_DEBUG, "accept failed: %s", strerror(errno));
        }
    } while (connectionFd > 0);
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

    // Unblock SIGCHLD.
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGCHLD);
    FDLog(LOG_DEBUG, "Unblocking SIGCHLD.");
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);

    return 0;
}

static int iTermFileDescriptorMultiServerRun(char *path, int socketFd, int connectionFd) {
    SetRunningServer();
    // syslog raises sigpipe when the parent job dies on 10.12.
    signal(SIGPIPE, SIG_IGN);
    int rc = Initialize(path);
    if (rc) {
        FDLog(LOG_DEBUG, "Initialize failed with code %d", rc);
    } else {
        MainLoop(path, socketFd, connectionFd);
        // MainLoop never returns, except by dying on a signal.
    }
    FDLog(LOG_DEBUG, "Unlink %s", path);
    unlink(path);
    return 1;
}

// On entry there should be three file descriptors:
// 0: A socket we can accept() on.
// 1: A connection we can recvmsg() and sendmsg() on.
// 2: A pipe that can be used to detect this process's termination.
//
// There should be a single command-line argument, which is the path tot he unix-domain socket
// I'll use.
int main(int argc, char *argv[]) {
    assert(argc == 2);
    iTermFileDescriptorMultiServerRun(argv[1], 0, 1);
    return 1;
}
