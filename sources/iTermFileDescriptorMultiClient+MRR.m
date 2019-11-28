//
//  iTermFileDescriptorMultiClient+MRR.m
//  iTerm2
//
//  Created by George Nachman on 8/9/19.
//

#import "iTermFileDescriptorMultiClient+MRR.h"

#import "DebugLogging.h"

#import "iTermAdvancedSettingsModel.h"
#import "iTermFileDescriptorServer.h"
#import "iTermPosixTTYReplacements.h"
#include <sys/un.h>

static const NSInteger numberOfFileDescriptorsToPreserve = 3;

static char **Make2DArray(NSArray<NSString *> *strings) {
    char **result = (char **)malloc(sizeof(char *) * (strings.count + 1));
    for (NSInteger i = 0; i < strings.count; i++) {
        result[i] = strdup(strings[i].UTF8String);
    }
    result[strings.count] = NULL;
    return result;
}

static void Free2DArray(char **array, NSInteger count) {
    for (NSInteger i = 0; i < count; i++) {
        free(array[i]);
    }
    free(array);
}

@implementation iTermFileDescriptorMultiClient (MRR)

// NOTE: Sets _socketFD as client file descriptor as a side-effect
- (BOOL)createAttachedSocketAtPath:(NSString *)path
                            socket:(int *)socketFDOut  // server fd for accept()
                        connection:(int *)connectionFDOut {  // server socket fd for recvmsg/sendmsg
    DLog(@"iTermForkAndExecToRunJobInServer");
    *socketFDOut = iTermFileDescriptorServerSocketBindListen(path.UTF8String);

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    // In another thread, accept on the unix domain socket. Since it's
    // already listening, there's no race here. connect will block until
    // accept is called if the main thread wins the race. accept will block
    // til connect is called if the background thread wins the race.
    iTermFileDescriptorServerLog("Kicking off a background job to accept() in the server");
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        iTermFileDescriptorServerLog("Now running the accept queue block");
        *connectionFDOut = iTermFileDescriptorServerAccept(*socketFDOut);

        // Let the main thread go. This is necessary to ensure that
        // *connectionFDOut is written to before the main thread uses it.
        iTermFileDescriptorServerLog("Signal the semaphore");
        dispatch_semaphore_signal(semaphore);
    });

    // Connect to the server running in a thread.
    switch ([self tryAttach]) {
        case iTermFileDescriptorMultiClientAttachStatusSuccess:
            break;
        case iTermFileDescriptorMultiClientAttachStatusConnectFailed:
        case iTermFileDescriptorMultiClientAttachStatusFatalError:
            // It's pretty weird if this fails.
            dispatch_release(semaphore);
            close(*connectionFDOut);
            *connectionFDOut = -1;
            close(*socketFDOut);
            *socketFDOut = -1;
            return NO;
    }

    // Wait until the background thread finishes accepting.
    iTermFileDescriptorServerLog("Waiting for the semaphore");
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    iTermFileDescriptorServerLog("The semaphore was signaled");
    dispatch_release(semaphore);

    return YES;
}

static void
do_sendmsg(int sock)
{
    struct msghdr msg;
        memset(&msg, 0, sizeof(msg));

    struct iovec iov[1];
    iov[0].iov_base = "Hello";
    iov[0].iov_len = 6;

    msg.msg_iov = iov;
    msg.msg_iovlen = sizeof(iov) / sizeof(iov[0]);

    assert(sendmsg(sock, &msg, 0) != -1);
}

static int
make_socket(const char *sockpath) {
    int sock = socket(PF_LOCAL, SOCK_STREAM, 0);
        assert(sock != -1);

    struct sockaddr_storage storage;
    struct sockaddr_un *addr = (struct sockaddr_un *)&storage;
    addr->sun_family = AF_LOCAL;
    strlcpy(addr->sun_path, sockpath, sizeof(addr->sun_path));
    addr->sun_len = SUN_LEN(addr);
    assert(bind(sock, (struct sockaddr *)addr, addr->sun_len) != -1);
    assert(listen(sock, 0) != -1);
    return sock;
}

static void
server_main(const char *sockpath)
{
    int sock = make_socket(sockpath);

    int s;
    assert((s = accept(sock, NULL, 0)) != -1);

    do_sendmsg(s);

    assert (close(s) != -1);
    assert (close(sock) != -1);
    assert (unlink(sockpath) != -1);
}

- (iTermForkState)launchWithSocketPath:(NSString *)path
                            executable:(NSString *)executable {
    assert([iTermAdvancedSettingsModel runJobsInServers]);

    iTermForkState forkState = {
        .pid = -1,
        .connectionFd = 0,
        .deadMansPipe = { 0, 0 },
        .numFileDescriptorsToPreserve = numberOfFileDescriptorsToPreserve
    };

    // Get ready to run the server in a thread.
    int serverSocketFd;
    int serverConnectionFd;
    if (![self createAttachedSocketAtPath:path socket:&serverSocketFd connection:&serverConnectionFd]) {
        return forkState;
    }

    forkState.connectionFd = _socketFD;

    pipe(forkState.deadMansPipe);

    NSArray<NSString *> *argv = @[ executable, path ];
    char **cargv = Make2DArray(argv);
    const char **cenv = (const char **)Make2DArray(@[]);
    const char *argpath = executable.UTF8String;

    int fds[] = { serverSocketFd, serverConnectionFd, forkState.deadMansPipe[1] };
    assert(sizeof(fds) / sizeof(*fds) == numberOfFileDescriptorsToPreserve);

    forkState.pid = fork();
    switch (forkState.pid) {
        case -1:
            // error
            iTermFileDescriptorServerLog("Fork failed: %s", strerror(errno));
            return forkState;

        case 0: {
            // child

            iTermPosixMoveFileDescriptors(fds, numberOfFileDescriptorsToPreserve);
            iTermExec(argpath, (const char **)cargv, NO, &forkState, "/", cenv);
            _exit(-1);
            return forkState;
        }
        default:
            // parent
            close(serverSocketFd);
            close(forkState.deadMansPipe[1]);
            Free2DArray(cargv, argv.count);
            Free2DArray((char **)cenv, 0);
//            do_sendmsg(_socketFD); WORKS HERE
            return forkState;
    }
}

@end
