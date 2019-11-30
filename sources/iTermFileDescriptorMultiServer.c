//
//  iTermFileDescriptorMultiServer.c
//  iTerm2
//
//  Created by George Nachman on 7/22/19.
//

#include "iTermFileDescriptorMultiServer.h"

#include "iTermFileDescriptorServerShared.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/un.h>

#ifndef ITERM_SERVER
#error ITERM_SERVER not defined. Build process is broken.
#endif

static int gDebugMode = 0;

// On entry there should be three file descriptors:
// 0: A socket we can accept() on. listen() was already called on it.
// 1: A connection we can sendmsg() on. accept() was already called on it.
// 2: A pipe that can be used to detect this process's termination. Do nothing with it.
// 3: A pipe we can recvmsg() on.
typedef enum {
    iTermMultiServerFileDescriptorAcceptSocket = 0,
    iTermMultiServerFileDescriptorInitialWrite = 1,
    iTermMultiServerFileDescriptorDeadMansPipe = 2,
    iTermMultiServerFileDescriptorInitialRead = 3
} iTermMultiServerFileDescriptor;

static void DLogImpl(const char *func, const char *file, int line, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *temp = NULL;
    asprintf(&temp, "iTermServer(pid=%d) %s:%d %s: %s", getpid(), file, line, func, format);
    vsyslog(LOG_DEBUG, temp, args);
    va_end(args);
    free(temp);
}
static int MakeBlocking(int fd);

#define DLog(args...) DLogImpl(__FUNCTION__, __FILE__, __LINE__, args)

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
    const char *tty;
} iTermMultiServerChild;

static iTermMultiServerChild *children;
static int numberOfChildren;

#pragma mark - Signal handlers

static void SigChildHandler(int arg) {
    // Wake the select loop.
    write(gPipe[1], "", 1);
}

#pragma mark - Mutate Children

static void LogChild(const iTermMultiServerChild *child) {
    DLog("masterFd=%d, pid=%d, terminated=%d, status=%d, tty=%s", child->masterFd, child->pid, child->terminated, child->status, child->tty ?: "(null)");
}

static void AddChild(const iTermMultiServerRequestLaunch *launch,
                     int masterFd,
                     const char *tty,
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
    children[i].tty = strdup(tty);

    DLog("Added child %d:", i);
    LogChild(&children[i]);
}

static void FreeChild(int i) {
    assert(i >= 0);
    assert(i < numberOfChildren);
    DLog("Free child %d", i);
    iTermMultiServerChild *child = &children[i];
    free((char *)child->tty);
    iTermMultiServerClientOriginatedMessageFree(&child->messageWithLaunchRequest);
    child->tty = NULL;
}

static void RemoveChild(int i) {
    assert(i >= 0);
    assert(i < numberOfChildren);

    DLog("Remove child %d", i);
    if (numberOfChildren == 1) {
        free(children);
        children = NULL;
    } else {
        FreeChild(i);
        const int afterCount = numberOfChildren - i - 1;
        memmove(children + i,
                children + i + 1,
                sizeof(*children) * afterCount);
        children = realloc(children, sizeof(*children) * (numberOfChildren - 1));
    }

    numberOfChildren -= 1;
}

#pragma mark - Launch

static void Log2DArray(const char *label, const char **p, int count) {
    for (int i = 0; i < count; i++) {
        DLog("%s[%d] = %s", label, i, p[i] ?: "(null)");
    }
}

static void LogLaunchRequest(const iTermMultiServerRequestLaunch *launch) {
    DLog("Launch request path=%s size=(%d x %d) utf8=%d pwd=%s uniqueId=%lld:",
         launch->path ?: "(null)",
         launch->width,
         launch->height,
         launch->isUTF8,
         launch->pwd ?: "(null)",
         launch->uniqueId);
    Log2DArray("argv", launch->argv, launch->argc);
    Log2DArray("environment", launch->envp, launch->envc);
}

static int Launch(const iTermMultiServerRequestLaunch *launch,
                  iTermForkState *forkState,
                  iTermTTYState *ttyStatePtr,
                  int *errorPtr) {
    LogLaunchRequest(launch);
#warning TODO: Pass pixel size
    iTermTTYStateInit(ttyStatePtr,
                      iTermTTYCellSizeMake(launch->width, launch->height),
                      iTermTTYPixelSizeMake(0, 0),
                      launch->isUTF8);
    int fd;
    forkState->numFileDescriptorsToPreserve = 3;
    DLog("Forking...");
    forkState->pid = forkpty(&fd, ttyStatePtr->tty, &ttyStatePtr->term, &ttyStatePtr->win);
    if (forkState->pid == (pid_t)0) {
        // Child
        iTermExec(launch->path,
                  (const char **)launch->argv,
                  1,  /* close file descriptors */
                  0,  /* restore resource limits */
                  forkState,
                  launch->pwd,
                  launch->envp,
                  fd);
        return -1;
    }
    if (forkState->pid == 1) {
        *errorPtr = errno;
        DLog("forkpty failed: %s", strerror(errno));
        return -1;
    } 
    DLog("forkpty succeeded. Child pid is %d", forkState->pid);
    *errorPtr = 0;
    return fd;
}

static int SendLaunchResponse(int fd, int status, pid_t pid, int masterFd, const char *tty, long long uniqueId) {
    DLog("Send launch response fd=%d status=%d pid=%d masterFd=%d tty=%d uniqueId=%lld",
         fd, status, pid, masterFd, tty, uniqueId);

    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeLaunch,
        .payload = {
            .launch = {
                .status = status,
                .pid = pid,
                .uniqueId = uniqueId,
                .tty = tty
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    ssize_t result;
    if (masterFd >= 0) {
        // Happy path. Send the file descriptor.
        DLog("NOTE: sending file descriptor");
        result = iTermFileDescriptorServerSendMessageAndFileDescriptor(fd,
                                                                       obj.ioVectors[0].iov_base,
                                                                       obj.ioVectors[0].iov_len,
                                                                       masterFd);
    } else {
        // Error happened. Don't send a file descriptor.
        DLog("ERROR: *not* sending file descriptor");
        result = iTermFileDescriptorServerSendMessage(fd,
                                                      obj.ioVectors[0].iov_base,
                                                      obj.ioVectors[0].iov_len);
    }
    iTermClientServerProtocolMessageFree(&obj);
    return result == -1;
}

static int HandleLaunchRequest(int fd, const iTermMultiServerRequestLaunch *launch) {
    DLog("HandleLaunchRequest fd=%d", fd);
    LogLaunchRequest(launch);

    int error;
    iTermForkState forkState = {
        .connectionFd = -1,
        .deadMansPipe = { 0, 0 },
    };
    iTermTTYState ttyState;
    memset(&ttyState, 0, sizeof(ttyState));
    int masterFd = Launch(launch, &forkState, &ttyState, &error);
    if (masterFd < 0) {
        return SendLaunchResponse(fd,
                                  -1 /* status */,
                                  0 /* pid */,
                                  -1 /* masterFd */,
                                  "" /* tty */,
                                  launch->uniqueId);
    }

    // Happy path
    AddChild(launch, masterFd, ttyState.tty, &forkState);
    return SendLaunchResponse(fd,
                              0 /* status */,
                              forkState.pid,
                              masterFd,
                              ttyState.tty,
                              launch->uniqueId);
}

#pragma mark - Report Termination

static int ReportTermination(int fd, pid_t pid) {
    DLog("Report termination pid=%d fd=%d", (int)pid, fd);

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
    return result == -1;
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
        .pwd = child->messageWithLaunchRequest.payload.launch.pwd,
        .tty = child->tty
    };
    *out = temp;
}

static int ReportChild(int fd, const iTermMultiServerChild *child, int isLast) {
    DLog("Report child fd=%d isLast=%d:", fd, isLast);
    LogChild(child);

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
    DLog("Wait on pid %d", pid);
    pid_t result;
    do {
        result = waitpid(pid, statusOut, WNOHANG);
    } while (result < 0 && errno == EINTR);
    return result;
}

static int WaitForAllProcesses(int connectionFd) {
    DLog("WaitForAllProcesses connectionFd=%d", connectionFd);

    DLog("Emptying pipe...");
    ssize_t rc;
    do {
        char c;
        rc = read(gPipe[0], &c, sizeof(c));
    } while (rc > 0 || (rc == -1 && errno == EINTR));
    if (rc < 0 && errno != EAGAIN) {
        DLog("Read of gPipe[0] failed with %s", strerror(errno));
    }
    DLog("Done emptying pipe. Wait on non-terminated children.");
    for (int i = 0; i < numberOfChildren; i++) {
        if (children[i].terminated) {
            continue;
        }
        const pid_t pid = WaitPidNoHang(children[i].pid, &children[i].status);
        if (pid > 0) {
            DLog("Child with pid %d exited with status %d", (int)pid, children[i].status);
            children[i].terminated = 1;
            if (connectionFd >= 0 && ReportTermination(connectionFd, children[i].pid)) {
                DLog("ReportTermination returned an error");
                return -1;
            }
        }
    }
    DLog("Finished making waitpid calls");
    return 0;
}

#pragma mark - Report Children

static int ReportChildren(int fd) {
    DLog("Reporting children...");
    // Iterate backwards because ReportAndRemoveDeadChild deletes the index passed to it.
    for (int i = numberOfChildren - 1; i >= 0; i--) {
        if (ReportChild(fd, &children[i], i + 1 == numberOfChildren)) {
            return -1;
        }
    }
    DLog("Done reporting children...");
    return 0;
}

#pragma mark - Handshake

static int HandleHandshake(int fd, iTermMultiServerRequestHandshake *handshake) {
    DLog("Handle handshake maximumProtocolVersion=%d", handshake->maximumProtocolVersion);;
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    if (handshake->maximumProtocolVersion < iTermMultiServerProtocolVersion1) {
        DLog("Maximum protocol version is too low: %d", handshake->maximumProtocolVersion);
        return -1;
    }
    iTermMultiServerServerOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeHandshake,
        .payload = {
            .handshake = {
                .protocolVersion = iTermMultiServerProtocolVersion1,
                .numChildren = numberOfChildren,
                .pid = getpid()
            }
        }
    };
    iTermMultiServerProtocolEncodeMessageFromServer(&message, &obj);

    DLog("Send handshake response with protocolVersion=%d, numChildren=%d, pid=%d",
         message.payload.handshake.protocolVersion,
         message.payload.handshake.numChildren,
         message.payload.handshake.pid);
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
    DLog("Handle wait request for pid %d", wait->pid);

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

    DLog("Send wait response with pid=%d status=%d errorNumber=%d",
         message.payload.wait.pid,
         message.payload.wait.status,
         message.payload.wait.errorNumber);
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
    DLog("Reading a request...");
    int status = iTermMultiServerRead(fd, &message);
    if (status) {
        DLog("Read failed");
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

static int ReadAndHandleRequest(int readFd, int writeFd) {
    iTermMultiServerClientOriginatedMessage request;
    if (ReadRequest(readFd, &request)) {
        return -1;
    }
    DLog("Handle request of type %d", (int)request.type);
    switch (request.type) {
        case iTermMultiServerRPCTypeHandshake:
            return HandleHandshake(writeFd, &request.payload.handshake);
        case iTermMultiServerRPCTypeWait:
            return HandleWait(writeFd, &request.payload.wait);
        case iTermMultiServerRPCTypeLaunch:
            return HandleLaunchRequest(writeFd, &request.payload.launch);
        case iTermMultiServerRPCTypeTermination:
            DLog("Ignore termination message");
            break;
        case iTermMultiServerRPCTypeReportChild:
            DLog("Ignore report child message");
            break;
    }
    iTermMultiServerClientOriginatedMessageFree(&request);
    return 0;
}

#pragma mark - Core

static void AcceptAndReject(int socket) {
    DLog("Calling accept()...");
    int fd = iTermFileDescriptorServerAccept(socket);
    if (fd < 0) {
        DLog("Don't send message: accept failed");
        return;
    }

    DLog("Received connection attempt while already connected. Send rejection.");

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
                                         obj.ioVectors[0].iov_len);
    iTermClientServerProtocolMessageFree(&obj);
    
    close(fd);
}

// There is a client connected. Respond to requests from it until it disconnects, then return.
static void SelectLoop(int acceptFd, int writeFd, int readFd) {
    DLog("Begin SelectLoop.");
    while (1) {
        static const int fdCount = 3;
        int fds[fdCount] = { gPipe[0], acceptFd, readFd };
        int results[fdCount];
        DLog("Calling select()");
        iTermSelect(fds, sizeof(fds) / sizeof(*fds), results, 1 /* wantErrors */);

        if (results[2]) {
            // readFd
            DLog("select: have data to read");
            if (ReadAndHandleRequest(readFd, writeFd)) {
                if (results[0]) {
                    DLog("Client hung up and also have SIGCHLD to deal with. Wait for processes.");
                    WaitForAllProcesses(-1);
                }
                break;
            }
        }
        if (results[0]) {
            // gPipe[0]
            DLog("select: SIGCHLD happened during select");
            if (WaitForAllProcesses(writeFd)) {
                break;
            }
        }
        if (results[1]) {
            // socketFd
            DLog("select: socket is readable");
            AcceptAndReject(acceptFd);
        }
    }
    DLog("Exited select loop.");
    close(writeFd);
}

static int MakeAndSendPipe(int unixDomainSocketFd) {
    int fds[2];
    if (pipe(fds) != 0) {
        return -1;
    }

    int readPipe = fds[0];
    int writePipe = fds[1];

    const ssize_t rc = iTermFileDescriptorServerSendMessageAndFileDescriptor(unixDomainSocketFd, "", 0, writePipe);
    if (rc == -1) {
        DLog("Failed to send write file descriptor: %s", strerror(errno));
        close(readPipe);
        readPipe = -1;
    }

    DLog("Sent write end of pipe");
    close(writePipe);
    return readPipe;
}

static int iTermMultiServerAccept(int socketFd) {
    // incoming unix domain socket connection to get FDs
    int connectionFd = -1;
    while (1) {
        int fds[] = { socketFd, gPipe[0] };
        int results[2] = { 0, 0 };
        DLog("iTermMultiServerAccept calling iTermSelect...");
        iTermSelect(fds, sizeof(fds) / sizeof(*fds), results, 1);
        DLog("iTermSelect returned.");
        if (results[1]) {
            DLog("SIGCHLD pipe became readable while waiting for connection. Calling wait...");
            WaitForAllProcesses(-1);
            DLog("Done wait()ing on all children");
        }
        if (results[0]) {
            DLog("Socket became readable. Calling accept()...");
            connectionFd = iTermFileDescriptorServerAccept(socketFd);
            if (connectionFd != -1) {
                break;
            }
        }
        DLog("accept() returned %d error=%s", connectionFd, strerror(errno));
    }
    return connectionFd;
}

static pid_t LaunchTrivialChildForDebugging(void) {
    pid_t pid = fork();
    if (pid == 0) {
        // Do not start the new process with a signal handler.
        for (int i = 1; i < 32; i++) {
            signal(i, SIG_DFL);
        }

        // Unblock all signals.
        sigset_t signals;
        sigemptyset(&signals);
        sigprocmask(SIG_SETMASK, &signals, NULL);
        sleep(999999);
    } else if (pid < 1) {
        printf("fork failed\n");
    } else {
        printf("forked. child is %d\n", (int)pid);
    }
    return pid;
}

static pid_t LaunchLoginStandardlyForDebugging(void) {
    printf("Debug mode detected! Launching login.\n");
    const char *argv[] = { "login", "-fp", "gnachman", NULL };
    const char *envp[] = { "PATH=/bin:/usr/bin", NULL };
    iTermMultiServerRequestLaunch request = {
        .path = "login",
        .argv = argv,
        .argc = 3,
        .envp = envp,
        .envc = 1,
        .width = 80,
        .height = 25,
        .isUTF8 = 1,
        .pwd = NULL,
        .uniqueId = 1234
    };
    iTermForkState forkState;
    memset(&forkState, 0, sizeof(forkState));
    iTermTTYState ttyState;
    memset(&ttyState, 0, sizeof(ttyState));
    int error = 0;
    int fd = Launch(&request, &forkState, &ttyState, &error);
    if (fd < 0) {
        printf("Launch failed. error is %d\n", error);
        return -1;
    }

    printf("Launched login with pid %d\n", (int)forkState.pid);
    return forkState.pid;
}

#define iTermTTYMakeControlKey(x) (x - 'A' + 1)

static pid_t LaunchLoginForDebugging(void) {
    // Do not start the new process with a signal handler.
    for (int i = 1; i < 32; i++) {
        signal(i, SIG_DFL);
    }

    // Unblock all signals.
    sigset_t signals;
    sigemptyset(&signals);
    sigprocmask(SIG_SETMASK, &signals, NULL);

    int master;
    pid_t pid;
    char tty[PATH_MAX];
    struct termios term;
    memset(&term, 0, sizeof(term));
    term.c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT | IUTF8;
    term.c_oflag = OPOST | ONLCR;
    term.c_cflag = CREAD | CS8 | HUPCL;
    term.c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;
    term.c_cc[VEOF] = iTermTTYMakeControlKey('D');
    term.c_cc[VEOL] = -1;
    term.c_cc[VEOL2] = -1;
    term.c_cc[VERASE] = 0x7f;
    term.c_cc[VWERASE] = iTermTTYMakeControlKey('W');
    term.c_cc[VKILL] = iTermTTYMakeControlKey('U');
    term.c_cc[VREPRINT] = iTermTTYMakeControlKey('R');
    term.c_cc[VINTR] = iTermTTYMakeControlKey('C');
    term.c_cc[VQUIT] = 0x1c;
    term.c_cc[VSUSP] = iTermTTYMakeControlKey('Z');
    term.c_cc[VDSUSP] = iTermTTYMakeControlKey('Y');
    term.c_cc[VSTART] = iTermTTYMakeControlKey('Q');
    term.c_cc[VSTOP] = iTermTTYMakeControlKey('S');
    term.c_cc[VLNEXT] = iTermTTYMakeControlKey('V');
    term.c_cc[VDISCARD] = iTermTTYMakeControlKey('O');
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;
    term.c_cc[VSTATUS] = iTermTTYMakeControlKey('T');
    term.c_ispeed = B38400;
    term.c_ospeed = B38400;

    struct winsize win;
    win.ws_row = 25;
    win.ws_col = 80;
    win.ws_xpixel = 0;
    win.ws_ypixel = 0;

    const int fdsToClose = getdtablesize();

    pid = forkpty(&master, tty, &term, &win);

    if (pid < 0) {
        // fork failed
        return -1;
    }

    if (pid == 0) {
        // child

        for (int i = 3; i < fdsToClose; i++) {
            close(i);
        }

        char *args[] = { "login", "-fp", "gnachman", NULL };

        // run the BC calculator
        execvp("/usr/bin/login", args);
        // exec failed
        return -1;
    }

    printf("Child pid is %d\n", (int)pid);
    // parent
    return pid;
}

static void ReadAndPrintUntilEOF(int master) {
    while (1) {
        char buffer[1025];
        ssize_t n = read(master, buffer, 1024);
        if (n == -1 && errno != EINTR && errno != EAGAIN) {
            break;
        }
        if (n == 0) {
            printf("eof\n");
            int status=0;
            pid_t waited = wait(&status);
            if (waited > 0) {
                printf("Waited successfully. Status is %d\n", status);
            } else {
                printf("No luck with wait: %s\n", strerror(errno));
            }
            break;
        }
        buffer[n] = 0;
        printf("READ: %s\n", buffer);
    }
}

// Alternates between running the select loop and accepting a new connection.
static void MainLoop(char *path, int acceptFd, int initialWriteFd, int initialReadFd) {
    DLog("Entering main loop.");
    assert(acceptFd >= 0);
    assert(acceptFd != initialWriteFd);
    if (!gDebugMode) {
        assert(initialWriteFd >= 0);
        assert(initialReadFd >= 0);
    }

    if (gDebugMode) {
        pid_t childPid = LaunchLoginForDebugging();
//        kill(childPid, SIGHUP);
        printf("Looping forever, waiting for gPipe to become readable\n");
        while (1) {
            char c;
            ssize_t rc = read(gPipe[0], &c, 1);
            if (rc > 0) {
                printf("SigChild received, wait on child.");
                int status;
                pid_t pid = wait(&status);
                printf("wait() returned pid %d\n", (int)pid);
            } else if (rc == 0) {
                printf("gPipe closed. wtf?\n");
            } else {
                printf("read returned error %s\n", strerror(errno));
            }
            sleep(1);
        }
    }

    int writeFd = initialWriteFd;
    int readFd = initialReadFd;
    do {
        if (writeFd >= 0 && readFd >= 0) {
            SelectLoop(acceptFd, writeFd, readFd);
        }

        // You get here after the connection is lost. Listen and accept.
        DLog("Calling iTermMultiServerAccept");
        writeFd = iTermMultiServerAccept(acceptFd);
        if (writeFd == -1) {
            DLog("iTermMultiServerAccept failed: %s", strerror(errno));
            break;
        }
        DLog("Accept returned a valid file descriptor %d", writeFd);
        readFd = MakeAndSendPipe(writeFd);
    } while (writeFd >= 0 && readFd >= 0);
    DLog("Returning from MainLoop because of an error.");
}

#pragma mark - Bootstrap

static int MakeNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    int rc = 0;
    do {
        rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    } while (rc == -1 && errno == EINTR);
    return rc == -1;
}

static int MakeBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    int rc = 0;
    do {
        rc = fcntl(fd, F_SETFL, flags & (~O_NONBLOCK));
    } while (rc == -1 && errno == EINTR);
    return rc == -1;
}

static int MakeStandardFileDescriptorsNonBlocking(void) {
    int status = 1;

    if (MakeNonBlocking(iTermMultiServerFileDescriptorAcceptSocket)) {
        goto done;
    }
    if (MakeBlocking(iTermMultiServerFileDescriptorInitialWrite)) {
        goto done;
    }
    if (MakeBlocking(iTermMultiServerFileDescriptorDeadMansPipe)) {
        goto done;
    }
    if (MakeBlocking(iTermMultiServerFileDescriptorInitialRead)) {
        goto done;
    }
    status = 0;

done:
    return status;
}

static int MakePipe(void) {
    if (pipe(gPipe) < 0) {
        DLog("Failed to create pipe: %s", strerror(errno));
        return 1;
    }

    // Make pipes nonblocking
    for (int i = 0; i < 2; i++) {
        if (MakeNonBlocking(gPipe[i])) {
            DLog("Failed to set gPipe[%d] nonblocking: %s", i, strerror(errno));
            return 2;
        }
    }
    return 0;
}

static int InitializeSignals(void) {
    // We get this when iTerm2 crashes. Ignore it.
    DLog("Installing SIGHUP handler.");
    sig_t rc = signal(SIGHUP, SIG_IGN);
    if (rc == SIG_ERR) {
        DLog("signal(SIGHUP, SIG_IGN) failed with %s", strerror(errno));
        return 1;
    }

    // Unblock SIGCHLD.
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGCHLD);
    DLog("Unblocking SIGCHLD.");
    if (sigprocmask(SIG_UNBLOCK, &signal_set, NULL) == -1) {
        DLog("sigprocmask(SIG_UNBLOCK, &signal_set, NULL) failed with %s", strerror(errno));
        return 1;
    }

    DLog("Installing SIGCHLD handler.");
    rc = signal(SIGCHLD, SigChildHandler);
    if (rc == SIG_ERR) {
        DLog("signal(SIGCHLD, SigChildHandler) failed with %s", strerror(errno));
        return 1;
    }

    DLog("signals initialized");
    return 0;
}

static void InitializeLogging(void) {
    openlog("iTerm2-Server", LOG_PID | LOG_NDELAY, LOG_USER);
    setlogmask(LOG_UPTO(LOG_DEBUG));
}

static int Initialize(char *path) {
    InitializeLogging();

    DLog("Server starting Initialize()");

    if (MakeStandardFileDescriptorsNonBlocking()) {
        return 1;
    }

    gPath = strdup(path);

    if (MakePipe()) {
        return 1;
    }

    if (InitializeSignals()) {
        return 1;
    }

    return 0;
}

static int iTermFileDescriptorMultiServerRun(char *path, int socketFd, int writeFD, int readFD) {
    SetRunningServer();
    // syslog raises sigpipe when the parent job dies on 10.12.
//    signal(SIGPIPE, SIG_IGN);
    int rc = Initialize(path);
    if (rc) {
        DLog("Initialize failed with code %d", rc);
    } else {
        MainLoop(path, socketFd, writeFD, readFD);
        // MainLoop never returns, except by dying on a signal.
    }
    DLog("Cleaning up to exit");
    DLog("Unlink %s", path);
    unlink(path);
    return 1;
}


// There should be a single command-line argument, which is the path to the unix-domain socket
// I'll use.
int main(int argc, char *argv[]) {
    if (argc == 1) {
        printf("In debug mode! Will spawn login -fp gnachman\nFYI my pid is %d\n", (int)getpid());
        const char *path = "/tmp/debug.socket";
        unlink(path);
        int acceptFd = iTermFileDescriptorServerSocketBindListen(path);
        if (acceptFd < 0) {
            printf("socket-bind-listen on %s failed", path);
            return 1;
        }
        gDebugMode = 1;
        iTermFileDescriptorMultiServerRun("/tmp/debug.socket",
                                          acceptFd,
                                          -1, /* write */
                                          -1); /* read */
    }
    assert(argc == 2);
    iTermFileDescriptorMultiServerRun(argv[1],
                                      iTermMultiServerFileDescriptorAcceptSocket,
                                      iTermMultiServerFileDescriptorInitialWrite,
                                      iTermMultiServerFileDescriptorInitialRead);
    return 1;
}
