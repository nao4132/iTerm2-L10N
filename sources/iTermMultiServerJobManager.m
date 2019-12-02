//
//  iTermMultiServerJobManager.m
//  iTerm2SharedARC
//
//  Created by George Nachman on 11/25/19.
//

#import "iTermMultiServerJobManager.h"

#import "DebugLogging.h"
#import "iTermFileDescriptorMultiClient.h"
#import "iTermMultiServerConnection.h"
#import "iTermNotificationCenter.h"
#import "iTermProcessCache.h"
#import "NSArray+iTerm.h"
#import "NSFileManager+iTerm.h"
#import "TaskNotifier.h"

@implementation iTermMultiServerJobManager {
    iTermMultiServerConnection *_conn;
    iTermFileDescriptorMultiClientChild *_child;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        static dispatch_once_t onceToken;
        static id subscriber;
        dispatch_once(&onceToken, ^{
            subscriber = [[NSObject alloc] init];
            [iTermMultiServerChildDidTerminateNotification subscribe:subscriber
                                                               block:
             ^(iTermMultiServerChildDidTerminateNotification * _Nonnull notification) {
                [[TaskNotifier sharedInstance] pipeDidBreakForExternalProcessID:notification.pid
                                                                         status:notification.terminationStatus];
            }];
        });
    }
    return self;
}

- (NSString *)description {
    return [NSString stringWithFormat:@"<%@: %p child=%@ connection=%@>",
            NSStringFromClass([self class]), self, _child, _conn];
}

- (void)forkAndExecWithTtyState:(iTermTTYState *)ttyStatePtr
                        argpath:(const char *)argpath
                           argv:(const char **)argv
                     initialPwd:(const char *)initialPwd
                     newEnviron:(const char **)newEnviron
                    synchronous:(BOOL)synchronous
                           task:(id<iTermTask>)task
                     completion:(void (^)(iTermJobManagerForkAndExecStatus))completion {
    _conn = [iTermMultiServerConnection primaryConnection];
    [_conn launchWithTTYState:ttyStatePtr
                      argpath:argpath
                         argv:argv
                   initialPwd:initialPwd
                   newEnviron:newEnviron
                   completion:^(iTermFileDescriptorMultiClientChild *child,
                                NSError *error) {
        self->_child = child;
        if (child != NULL) {
            // Happy path
            [[TaskNotifier sharedInstance] registerTask:task];
            [[iTermProcessCache sharedInstance] setNeedsUpdate:YES];
            completion(iTermJobManagerForkAndExecStatusSuccess);
            return;
        }

        // Handle errors
        assert([error.domain isEqualToString:iTermFileDescriptorMultiClientErrorDomain]);
        const iTermFileDescriptorMultiClientErrorCode code = (iTermFileDescriptorMultiClientErrorCode)error.code;
        switch (code) {
            case iTermFileDescriptorMultiClientErrorCodePreemptiveWaitResponse:
            case iTermFileDescriptorMultiClientErrorCodeConnectionLost:
            case iTermFileDescriptorMultiClientErrorCodeNoSuchChild:
            case iTermFileDescriptorMultiClientErrorCodeCanNotWait:
            case iTermFileDescriptorMultiClientErrorCodeUnknown:
                completion(iTermJobManagerForkAndExecStatusServerError);
                break;
            case iTermFileDescriptorMultiClientErrorCodeForkFailed:
                completion(iTermJobManagerForkAndExecStatusFailedToFork);
                break;
        }
        assert(NO);
    }];
}

- (int)fd {
    return _child.fd;
}

- (void)setFd:(int)fd {
    assert(fd == -1);
}

- (NSString *)tty {
    return _child.tty;
}

- (void)setTty:(NSString *)tty {
    assert(NO);
}

- (pid_t)externallyVisiblePid {
    return _child.pid;
}

- (BOOL)hasJob {
    return _child != nil;
}

- (id)sessionRestorationIdentifier {
    return @{ @"Multi": @(_child.pid) };
}

- (pid_t)pidToWaitOn {
    return -1;
}

- (BOOL)isSessionRestorationPossible {
    return _child != nil;
}

- (void)attachToServer:(iTermGeneralServerConnection)serverConnection
         withProcessID:(NSNumber *)thePid
                  task:(id<iTermTask>)task {
    assert(serverConnection.type == iTermGeneralServerConnectionTypeMulti);
    assert(!_conn);
    assert(!_child);
    _conn = [iTermMultiServerConnection connectionForSocketNumber:serverConnection.multi.number
                                                 createIfPossible:NO];
    if (!_conn) {
#warning TODO: Test this
        [task brokenPipe];
        return;
    }
    if (thePid != nil) {
        assert(thePid.integerValue == serverConnection.multi.pid);
    }
    _child = [_conn attachToProcessID:serverConnection.multi.pid];
    if (!_child) {
        return;
    }
    if (_child.hasTerminated) {
        const pid_t pid = _child.pid;
        [_conn waitForChild:_child removePreemptively:NO completion:^(int status, NSError *error) {
            if (error) {
                DLog(@"Failed to wait on child with pid %d: %@", pid, error);
            } else {
                DLog(@"Child with pid %d terminated with status %d", pid, status);
            }
        }];
        [task brokenPipe];
    } else {
        [[TaskNotifier sharedInstance] registerTask:task];
        [[iTermProcessCache sharedInstance] setNeedsUpdate:YES];
    }
}

- (void)sendSignal:(int)signo toServer:(BOOL)toServer {
    if (toServer) {
        if (_conn.pid <= 0) {
            return;
        }
        DLog(@"Sending signal to server %@", @(_conn.pid));
        kill(_conn.pid, signo);
        return;
    }
    if (_child.pid <= 0) {
        return;
    }
    [[iTermProcessCache sharedInstance] unregisterTrackedPID:_child.pid];
    kill(_child.pid, signo);
}

#warning TODO: Test all these killing modes
- (void)killWithMode:(iTermJobManagerKillingMode)mode {
    switch (mode) {
        case iTermJobManagerKillingModeRegular:
            [self sendSignal:SIGHUP toServer:NO];
            break;

        case iTermJobManagerKillingModeForce:
            [self sendSignal:SIGKILL toServer:NO];
            break;

        case iTermJobManagerKillingModeForceUnrestorable:
            [self sendSignal:SIGKILL toServer:YES];
            [self sendSignal:SIGHUP toServer:NO];
            break;

        case iTermJobManagerKillingModeProcessGroup:
            if (_child.pid > 0) {
                [[iTermProcessCache sharedInstance] unregisterTrackedPID:_child.pid];
                // Kill a server-owned child.
                // TODO: Don't want to do this when Sparkle is upgrading.
                killpg(_child.pid, SIGHUP);
            }
            break;

        case iTermJobManagerKillingModeBrokenPipe:
            [self sendSignal:SIGHUP toServer:NO];
            break;
    }
    if (_child.haveWaited) {
        return;
    }
    const pid_t pid = _child.pid;
    [_conn waitForChild:_child removePreemptively:YES completion:^(int status, NSError *error) {
        DLog(@"Preemptive wait for %d finished with status %d error %@", pid, status, error);
    }];
}

@end
