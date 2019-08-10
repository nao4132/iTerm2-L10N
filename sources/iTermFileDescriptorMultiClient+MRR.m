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

- (BOOL)createAttachedSocketAtPath:(NSString *)path
                            socket:(int *)socketFDOut
                        connection:(int *)connectionFDOut {
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

    pipe(forkState.deadMansPipe);

    NSArray<NSString *> *argv = @[ path ];
    char **cargv = Make2DArray(argv);
    char **cenv = Make2DArray(@[]);
    const char *argpath = executable.UTF8String;

    forkState.pid = fork();
    switch (forkState.pid) {
        case -1:
            // error
            iTermFileDescriptorServerLog("Fork failed: %s", strerror(errno));
            return forkState;

        case 0: {
            // child
            int fds[] = { serverSocketFd, serverConnectionFd, forkState.deadMansPipe[1] };
            assert(sizeof(fds) / sizeof(*fds) == numberOfFileDescriptorsToPreserve);
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
            Free2DArray(cenv, 0);
            return forkState;
    }
}

@end
