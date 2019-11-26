//
//  iTermFileDescriptorMultiClient.m
//  iTerm2SharedARC
//
//  Created by George Nachman on 8/9/19.
//

#import "iTermFileDescriptorMultiClient.h"
#import "iTermFileDescriptorMultiClient+MRR.h"

#import "DebugLogging.h"
#import "iTermFileDescriptorServer.h"
#import "NSArray+iTerm.h"

#include <syslog.h>
#include <sys/un.h>

NSString *const iTermFileDescriptorMultiClientErrorDomain = @"iTermFileDescriptorMultiClientErrorDomain";

@interface iTermFileDescriptorMultiClientChild()
@property (nonatomic, copy) void (^waitCompletion)(int status, NSError *error);
@end

@implementation iTermFileDescriptorMultiClientChild

- (instancetype)initWithReport:(iTermMultiServerReportChild *)report {
    self = [super init];
    if (self) {
        _pid = report->pid;
        _executablePath = [[NSString alloc] initWithUTF8String:report->path];
        NSMutableArray<NSString *> *args = [NSMutableArray array];
        for (int i = 0; i < report->argc; i++) {
            NSString *arg = [[NSString alloc] initWithUTF8String:report->argv[i]];
            [args addObject:arg];
        }
        _args = args;

        NSMutableDictionary<NSString *, NSString *> *environment = [NSMutableDictionary dictionary];
        for (int i = 0; i < report->envc; i++) {
            NSString *kvp = [[NSString alloc] initWithUTF8String:report->envp[i]];
            NSInteger equals = [kvp rangeOfString:@"="].location;
            if (equals == NSNotFound) {
                assert(false);
                continue;
            }
            NSString *key = [kvp substringToIndex:equals];
            NSString *value = [kvp substringFromIndex:equals + 1];
            if (environment[key]) {
                continue;
            }
            environment[key] = value;
        }
        _environment = environment;
        _utf8 = report->isUTF8;
        _initialDirectory = [[NSString alloc] initWithUTF8String:report->pwd];
        _hasTerminated = report->terminated;
        _fd = report->fd;
        _tty = [NSString stringWithUTF8String:report->tty] ?: @"";
        _haveWaited = NO;
    }
    return self;
}

- (void)setTerminationStatus:(int)status {
    assert(_hasTerminated);
    _haveWaited = YES;
    _terminationStatus = status;
}

- (void)didTerminate {
    _hasTerminated = YES;
}

@end

@implementation iTermFileDescriptorMultiClient {
    NSMutableArray<iTermFileDescriptorMultiClientChild *> *_children;
    NSString *_socketPath;
    int _socketFD;
    dispatch_queue_t _queue;
}

- (instancetype)initWithPath:(NSString *)path {
    self = [super init];
    if (self) {
        _children = [NSMutableArray array];
        _socketPath = [path copy];
        _socketFD = -1;
        _queue = dispatch_queue_create("com.iterm2.multi-client", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

- (BOOL)attachOrLaunchServer {
    switch ([self tryAttach]) {
        case iTermFileDescriptorMultiClientAttachStatusSuccess:
            assert(_socketFD >= 0);
            return YES;
        case iTermFileDescriptorMultiClientAttachStatusConnectFailed:
            assert(_socketFD < 0);
            if (![self launch]) {
                assert(_socketFD < 0);
                return NO;
            }
            break;
        case iTermFileDescriptorMultiClientAttachStatusFatalError:
            assert(_socketFD < 0);
            return NO;
    }
    assert(_socketFD >= 0);
    BOOL ok = [self handshakeWithChildDiscoveryBlock:^(iTermMultiServerReportChild *child) {
        [self addChild:[[iTermFileDescriptorMultiClientChild alloc] initWithReport:child]];
    }];
    if (!ok) {
        [self close];
    }
    return ok;
}

- (BOOL)attach {
    return [self tryAttach] == iTermFileDescriptorMultiClientAttachStatusSuccess;
}

- (void)close {
    assert(_socketFD >= 0);
    close(_socketFD);
    _socketFD = -1;
}

- (void)addChild:(iTermFileDescriptorMultiClientChild *)child {
    [_children addObject:child];
    [self.delegate fileDescriptorMultiClient:self didDiscoverChild:child];
}

- (BOOL)readAsynchronouslyOnQueue:(dispatch_queue_t)queue
                   withCompletion:(void (^)(BOOL ok, iTermMultiServerServerOriginatedMessage *message))block {
    return [self readSynchronously:NO queue:queue completion:block];
}

- (BOOL)readSynchronouslyWithCompletion:(void (^)(BOOL ok, iTermMultiServerServerOriginatedMessage *message))block {
    return [self readSynchronously:YES queue:dispatch_get_main_queue() completion:block];
}

- (BOOL)readSynchronously:(BOOL)synchronously
                    queue:(dispatch_queue_t)queue
               completion:(void (^)(BOOL ok, iTermMultiServerServerOriginatedMessage *message))block {
    if (_socketFD < 0) {
        return NO;
    }

    if (synchronously) {
        iTermClientServerProtocolMessage encodedMessage;
        const int status = iTermMultiServerRecv(_socketFD, &encodedMessage);
        [self didFinishReadingWithStatus:status
                                 message:encodedMessage
                              completion:block];
    } else {
        __weak __typeof(self) weakSelf = self;
        dispatch_async(queue, ^{
            [weakSelf recvWithCompletion:block];
        });
    }
    return YES;
}

// Runs in background queue
- (void)recvWithCompletion:(void (^)(BOOL ok, iTermMultiServerServerOriginatedMessage *message))block {
    __block iTermClientServerProtocolMessage encodedMessage;
    const int status = iTermMultiServerRecv(_socketFD, &encodedMessage);

    __weak __typeof(self) weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        __strong __typeof(self) strongSelf = weakSelf;
        if (!strongSelf && !status) {
            iTermClientServerProtocolMessageFree(&encodedMessage);
            return;
        }
        [strongSelf didFinishReadingWithStatus:status
                                       message:encodedMessage
                                    completion:block];
    });
}

// Main queue
- (void)didFinishReadingWithStatus:(int)status
                           message:(iTermClientServerProtocolMessage)encodedMessage
                             completion:(void (^)(BOOL ok, iTermMultiServerServerOriginatedMessage *message))block {
    BOOL mustFreeEncodedMessage = NO;
    if (status) {
        goto done;
    }
    mustFreeEncodedMessage = YES;

    iTermMultiServerServerOriginatedMessage decodedMessage;
    memset(&decodedMessage, 0, sizeof(decodedMessage));

    status = iTermMultiServerProtocolParseMessageFromServer(&encodedMessage, &decodedMessage);
    if (status == 0) {
        block(YES, &decodedMessage);
    }
    iTermMultiServerServerOriginatedMessageFree(&decodedMessage);

done:
    if (mustFreeEncodedMessage) {
        iTermClientServerProtocolMessageFree(&encodedMessage);
    }
    if (status) {
        block(NO, NULL);
    }
}

- (BOOL)send:(iTermMultiServerClientOriginatedMessage *)message {
    if (_socketFD < 0) {
        return NO;
    }
    iTermClientServerProtocolMessage obj;
    iTermClientServerProtocolMessageInitialize(&obj);

    int status = 0;

    status = iTermMultiServerProtocolEncodeMessageFromClient(message, &obj);
    if (status) {
        goto done;
    }

    const ssize_t bytesWritten = iTermFileDescriptorServerSendMessage(_socketFD, obj.ioVectors[0].iov_base, obj.ioVectors[0].iov_len);
    if (bytesWritten <= 0) {
        status = 1;
        goto done;
    }

done:
    iTermClientServerProtocolMessageFree(&obj);
    return status == 0;
}

- (BOOL)sendHandshakeRequest {
    iTermMultiServerClientOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeHandshake,
        .payload = {
            .handshake = {
                .maximumProtocolVersion = iTermMultiServerProtocolVersion1
            }
        }
    };
    if (![self send:&message]) {
        return NO;
    }
    return YES;
}

- (BOOL)readHandshakeResponse:(int *)numberOfChildrenOut {
    __block BOOL ok = NO;
    __block int numberOfChildren = 0;
    const BOOL readOK = [self readSynchronouslyWithCompletion:^(BOOL readOK, iTermMultiServerServerOriginatedMessage *message) {
        if (!readOK) {
            ok = NO;
            return;
        }
        if (message->type != iTermMultiServerRPCTypeHandshake) {
            ok = NO;
            return;
        }
        if (message->payload.handshake.protocolVersion != iTermMultiServerProtocolVersion1) {
            ok = NO;
            return;
        }
        numberOfChildren = message->payload.handshake.numChildren;
        ok = YES;
    }];
    if (!readOK || !ok) {
        return NO;
    }
    *numberOfChildrenOut = numberOfChildren;
    return YES;
}

- (BOOL)receiveInitialChildReports:(int)numberOfChildren
                             block:(void (^)(iTermMultiServerReportChild *))block {
    __block BOOL ok = NO;
    for (int i = 0; i < numberOfChildren; i++) {
        const BOOL readChildOK = [self readSynchronouslyWithCompletion:^(BOOL readOK, iTermMultiServerServerOriginatedMessage *message) {
            if (!readOK) {
                ok = NO;
                return;
            }
            if (message->type != iTermMultiServerRPCTypeReportChild) {
                ok = NO;
                return;
            }
            block(&message->payload.reportChild);
            ok = YES;
        }];
        if (!readChildOK || !ok) {
            return NO;
        }
    }
    return YES;
}

- (BOOL)handshakeWithChildDiscoveryBlock:(void (^)(iTermMultiServerReportChild *))block {
    assert(_socketFD >= 0);

    if (![self sendHandshakeRequest]) {
        return NO;
    }

    int numberOfChildren;
    if (![self readHandshakeResponse:&numberOfChildren]) {
        return NO;
    }

    if (![self receiveInitialChildReports:numberOfChildren block:block]) {
        return NO;
    }

    return YES;
}

// This is copypasta from iTermFileDescriptorClient.c's iTermFileDescriptorClientConnect()
- (iTermFileDescriptorMultiClientAttachStatus)tryAttach {
    assert(_socketFD < 0);
    int interrupted = 0;
    int socketFd;
    int flags;

    FDLog(LOG_DEBUG, "Trying to connect to %s", _socketPath.UTF8String);
    do {
        FDLog(LOG_DEBUG, "Calling socket()");
        socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socketFd == -1) {
            FDLog(LOG_NOTICE, "Failed to create socket: %s\n", strerror(errno));
            return iTermFileDescriptorMultiClientAttachStatusFatalError;
        }

        struct sockaddr_un remote;
        remote.sun_family = AF_UNIX;
        strcpy(remote.sun_path, _socketPath.UTF8String);
        int len = strlen(remote.sun_path) + sizeof(remote.sun_family) + 1;
        FDLog(LOG_DEBUG, "Calling fcntl() 1");
        flags = fcntl(socketFd, F_GETFL, 0);

        // Put the socket in nonblocking mode so connect can fail fast if another iTerm2 is connected
        // to this server.
        FDLog(LOG_DEBUG, "Calling fcntl() 2");
        fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);

        FDLog(LOG_DEBUG, "Calling connect()");
        int rc = connect(socketFd, (struct sockaddr *)&remote, len);
        if (rc == -1) {
            interrupted = (errno == EINTR);
            FDLog(LOG_DEBUG, "Connect failed: %s\n", strerror(errno));
            close(socketFd);
            if (!interrupted) {
                return iTermFileDescriptorMultiClientAttachStatusConnectFailed;
            }
            FDLog(LOG_DEBUG, "Trying again because connect returned EINTR.");
        } else {
            // Make socket block again.
            interrupted = 0;
            FDLog(LOG_DEBUG, "Connected. Calling fcntl() 3");
            fcntl(socketFd, F_SETFL, flags & ~O_NONBLOCK);
        }
    } while (interrupted);

    _socketFD = socketFd;
    return iTermFileDescriptorMultiClientAttachStatusSuccess;
}

- (BOOL)launch {
    assert(_socketFD < 0);
    NSString *executable = [[NSBundle bundleForClass:[self class]] pathForResource:@"iTermServer" ofType:nil];
    assert(executable);
    iTermForkState forkState = [self launchWithSocketPath:_socketPath executable:executable];
    if (forkState.pid < 0) {
        return NO;
    }

    _socketFD = forkState.connectionFd;
    return YES;
}

- (void)launchChildWithExecutablePath:(const char *)path
                                 argv:(const char **)argv
                          environment:(char **)environment
                                  pwd:(const char *)pwd
                             ttyState:(id)ttyStatePtr
                           completion:(void (^)(iTermFileDescriptorMultiClientChild * _Nonnull, NSError * _Nullable))completion {
#warning TODO
}

- (void)waitForChild:(iTermFileDescriptorMultiClientChild *)child
          completion:(void (^)(int, NSError * _Nullable))completion {
    assert(!child.haveWaited);
    iTermMultiServerClientOriginatedMessage message = {
        .type = iTermMultiServerRPCTypeWait,
        .payload = {
            .wait = {
                .pid = child.pid
            }
        }
    };
    if (![self send:&message]) {
        [self close];
        completion(0, [self connectionLostError]);
        return;
    }
    __weak __typeof(child) weakChild = child;
    child.waitCompletion = ^(int status, NSError *error) {
        if (!error) {
            [weakChild setTerminationStatus:status];
        }
        completion(status, error);
    };
}

// Runs on a background queue
- (void)readLoop {
    const BOOL ok = [self readAsynchronouslyOnQueue:_queue withCompletion:^(BOOL readOK, iTermMultiServerServerOriginatedMessage *message) {
        if (!readOK) {
            [self close];
            return;
        }
        [self dispatch:message];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self readLoop];
        });
    }];
    if (!ok) {
        [self close];
    }
}

- (void)killServerAndAllChildren {
    // TODO
}

- (iTermFileDescriptorMultiClientChild *)childWithPID:(pid_t)pid {
    return [_children objectPassingTest:^BOOL(iTermFileDescriptorMultiClientChild *element, NSUInteger index, BOOL *stop) {
        return element.pid == pid;
    }];
}

- (void)handleWait:(iTermMultiServerResponseWait)wait {
    iTermFileDescriptorMultiClientChild *child = [self childWithPID:wait.pid];
    if (child.waitCompletion) {
        child.waitCompletion(wait.status, [self waitError:wait.errorNumber]);
        child.waitCompletion = nil;
    }
}

- (void)handleLaunch:(iTermMultiServerResponseLaunch)launch {
    assert(NO);  // TODO
}

- (void)handleTermination:(iTermMultiServerReportTermination)termination {
    iTermFileDescriptorMultiClientChild *child = [self childWithPID:termination.pid];
    if (child) {
        [child didTerminate];
        [self.delegate fileDescriptorMultiClient:self childDidTerminate:child];
    }
}

- (void)dispatch:(iTermMultiServerServerOriginatedMessage *)message {
    switch (message->type) {
        case iTermMultiServerRPCTypeWait:
            [self handleWait:message->payload.wait];
            break;

        case iTermMultiServerRPCTypeLaunch:
            [self handleLaunch:message->payload.launch];
            break;

        case iTermMultiServerRPCTypeTermination:
            [self handleTermination:message->payload.termination];
            break;

        case iTermMultiServerRPCTypeHandshake:
        case iTermMultiServerRPCTypeReportChild:
            [self close];
            break;
    }
}

- (NSError *)connectionLostError {
    return [NSError errorWithDomain:iTermFileDescriptorMultiClientErrorDomain
                               code:iTermFileDescriptorMultiClientErrorCodeConnectionLost
                           userInfo:nil];
}

- (NSError *)waitError:(int)errorNumber {
    iTermFileDescriptorMultiClientErrorCode code = iTermFileDescriptorMultiClientErrorCodeUnknown;
    switch (errorNumber) {
        case 0:
            return nil;
        case -1:
            code = iTermFileDescriptorMultiClientErrorCodeNoSuchChild;
        case -2:
            code = iTermFileDescriptorMultiClientErrorCodeCanNotWait;
    }
    return [NSError errorWithDomain:iTermFileDescriptorMultiClientErrorDomain
                               code:code
                           userInfo:nil];
}

@end
