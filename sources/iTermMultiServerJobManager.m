//
//  iTermMultiServerJobManager.m
//  iTerm2SharedARC
//
//  Created by George Nachman on 11/25/19.
//

#import "iTermMultiServerJobManager.h"

#import "DebugLogging.h"
#import "iTermFileDescriptorMultiClient.h"
#import "iTermProcessCache.h"
#import "NSArray+iTerm.h"
#import "NSFileManager+iTerm.h"
#import "TaskNotifier.h"

@interface iTermMultiServerConnection: NSObject<iTermFileDescriptorMultiClientDelegate>

@property (nonatomic, readonly) pid_t pid;

+ (instancetype)primaryConnection;

- (void)launchWithTTYState:(iTermTTYState *)ttyStatePtr
                   argpath:(const char *)argpath
                      argv:(const char **)argv
                initialPwd:(const char *)initialPwd
                newEnviron:(char **)newEnviron
                completion:(void (^)(iTermFileDescriptorMultiClientChild *child))completion;

@end

@implementation iTermMultiServerConnection {
    iTermFileDescriptorMultiClient *_client;
    BOOL _isPrimary;
    NSMutableArray<iTermFileDescriptorMultiClientChild *> *_unattachedChildren;
}

+ (instancetype)primaryConnection {
    static dispatch_once_t onceToken;
    static iTermMultiServerConnection *instance;
    dispatch_once(&onceToken, ^{
        int i = 0;
        while (1) {
            instance = [[iTermMultiServerConnection alloc] initPrimary:YES
                                                                number:i++];
            assert(instance);
            const BOOL ok = [instance->_client attachOrLaunchServer];
            if (ok) {
                break;
            }
            i++;
        }
        self.registry[@(i)] = instance;
    });
    return instance;
}

+ (NSMutableDictionary<NSNumber *, iTermMultiServerConnection *> *)registry {
    static NSMutableDictionary<NSNumber *, iTermMultiServerConnection *> *registry;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        registry = [NSMutableDictionary dictionary];
    });
    return registry;
}

+ (instancetype)connectionForSocketNumber:(int)number {
    iTermMultiServerConnection *result = self.registry[@(number)];
    if (result) {
        return result;
    }
    result = [[self alloc] initPrimary:NO number:number];
    assert(result);
    if (![result->_client attach]) {
        return nil;
    }
    self.registry[@(number)] = result;
    return result;
}

+ (NSString *)pathForNumber:(int)number {
    NSString *appSupportPath = [[NSFileManager defaultManager] applicationSupportDirectory];
    NSString *filename = [NSString stringWithFormat:@"daemon-%d.socket", number];
    NSURL *url = [[NSURL fileURLWithPath:appSupportPath] URLByAppendingPathComponent:filename];
    return url.path;
}

- (instancetype)initPrimary:(BOOL)primary number:(int)number {
    self = [super init];
    if (self) {
        _unattachedChildren = [NSMutableArray array];
        _isPrimary = primary;
        NSString *const path = [self.class pathForNumber:number];
        _client = [[iTermFileDescriptorMultiClient alloc] initWithPath:path];
        _client.delegate = self;
    }
    return self;
}

- (void)launchWithTTYState:(iTermTTYState *)ttyStatePtr
                   argpath:(const char *)argpath
                      argv:(const char **)argv
                initialPwd:(const char *)initialPwd
                newEnviron:(char **)newEnviron
                completion:(void (^)(iTermFileDescriptorMultiClientChild *child))completion {
    [_client launchChildWithExecutablePath:argpath
                                      argv:argv
                               environment:newEnviron
                                       pwd:initialPwd
                                  ttyState:ttyStatePtr
                                completion:^(iTermFileDescriptorMultiClientChild * _Nonnull child, NSError * _Nullable error) {
        if (error) {
            DLog(@"While creating child: %@", error);
        }
        completion(child);
    }];
}

- (iTermFileDescriptorMultiClientChild *)attachToProcessID:(pid_t)pid {
    iTermFileDescriptorMultiClientChild *child = [_unattachedChildren objectPassingTest:^BOOL(iTermFileDescriptorMultiClientChild *element, NSUInteger index, BOOL *stop) {
        return element.pid == pid;
    }];
    if (!child) {
        return nil;
    }
    [_unattachedChildren removeObject:child];
    return child;
}

#pragma mark - iTermFileDescriptorMultiClientDelegate

- (void)fileDescriptorMultiClient:(iTermFileDescriptorMultiClient *)client
                 didDiscoverChild:(iTermFileDescriptorMultiClientChild *)child {
        [_unattachedChildren addObject:child];
}

- (void)fileDescriptorMultiClientDidFinishHandshake:(iTermFileDescriptorMultiClient *)client {
#warning todo
}

- (void)fileDescriptorMultiClient:(iTermFileDescriptorMultiClient *)client
                childDidTerminate:(iTermFileDescriptorMultiClientChild *)child {
#warning todo
}

@end

@implementation iTermMultiServerJobManager {
    iTermMultiServerConnection *_conn;
    iTermFileDescriptorMultiClientChild *_child;
}

@synthesize tty = _tty;

- (NSString *)description {
    return [NSString stringWithFormat:@"<%@: %p child=%@ connection=%@>",
            NSStringFromClass([self class]), self, _child, _conn];
}

- (void)forkAndExecWithTtyState:(iTermTTYState *)ttyStatePtr
                        argpath:(const char *)argpath
                           argv:(const char **)argv
                     initialPwd:(const char *)initialPwd
                     newEnviron:(char **)newEnviron
                    synchronous:(BOOL)synchronous
                           task:(id<iTermTask>)task
                     completion:(void (^)(iTermJobManagerForkAndExecStatus))completion {
    _conn = [iTermMultiServerConnection primaryConnection];
    [_conn launchWithTTYState:ttyStatePtr
                      argpath:argpath
                         argv:argv
                   initialPwd:initialPwd
                   newEnviron:newEnviron
                   completion:^(iTermFileDescriptorMultiClientChild *child) {
        self->_child = child;
    }];
}

- (int)fd {
    return _child.fd;
}

- (NSString *)tty {
    return _child.tty;
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
    _conn = [iTermMultiServerConnection connectionForSocketNumber:serverConnection.multi.number];
    if (!_conn) {
#warning TODO: Test this
        [task brokenPipe];
        return;
    }
    _child = [_conn attachToProcessID:thePid.intValue];
    if (!_child) {
        return;
    }
    [[TaskNotifier sharedInstance] registerTask:task];
#warning TODO: Update orphan server adopter
    [[iTermProcessCache sharedInstance] setNeedsUpdate:YES];
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
            if (_serverChildPid > 0) {
                [[iTermProcessCache sharedInstance] unregisterTrackedPID:_serverChildPid];
                // Kill a server-owned child.
                // TODO: Don't want to do this when Sparkle is upgrading.
                killpg(_serverChildPid, SIGHUP);
            }
            break;

        case iTermJobManagerKillingModeBrokenPipe:
            [self killServerIfRunning];
            break;
    }
}


@end
