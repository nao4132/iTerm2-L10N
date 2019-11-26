//
//  iTermFileDescriptorMultiClient.h
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#import <Foundation/Foundation.h>
#import "iTermMultiServerProtocol.h"
#import "iTermTTYState.h"
#import "VT100GridTypes.h"

NS_ASSUME_NONNULL_BEGIN

@class iTermFileDescriptorMultiClient;

extern NSString *const iTermFileDescriptorMultiClientErrorDomain;
typedef NS_ENUM(NSUInteger, iTermFileDescriptorMultiClientErrorCode) {
    iTermFileDescriptorMultiClientErrorCodeConnectionLost,
    iTermFileDescriptorMultiClientErrorCodeNoSuchChild,
    iTermFileDescriptorMultiClientErrorCodeCanNotWait,  // child not terminated
    iTermFileDescriptorMultiClientErrorCodeUnknown
};

@interface iTermFileDescriptorMultiClientChild : NSObject
@property (nonatomic, readonly) pid_t pid;
@property (nonatomic, readonly) NSString *executablePath;
@property (nonatomic, readonly) NSArray<NSString *> *args;
@property (nonatomic, readonly) NSDictionary<NSString *, NSString *> *environment;
@property (nonatomic, readonly) BOOL utf8;
@property (nonatomic, readonly) NSString *initialDirectory;
@property (nonatomic, readonly) BOOL hasTerminated;
@property (nonatomic, readonly) BOOL haveWaited;
@property (nonatomic, readonly) int terminationStatus;  // only defined if haveWaited is YES
@property (nonatomic, readonly) int fd;
@property (nonatomic, readonly) NSString *tty;
@end

@protocol iTermFileDescriptorMultiClientDelegate<NSObject>

- (void)fileDescriptorMultiClient:(iTermFileDescriptorMultiClient *)client
                 didDiscoverChild:(iTermFileDescriptorMultiClientChild *)child;

- (void)fileDescriptorMultiClientDidFinishHandshake:(iTermFileDescriptorMultiClient *)client;

- (void)fileDescriptorMultiClient:(iTermFileDescriptorMultiClient *)client
                childDidTerminate:(iTermFileDescriptorMultiClientChild *)child;

@end

@interface iTermFileDescriptorMultiClient : NSObject

@property (nonatomic, weak) id<iTermFileDescriptorMultiClientDelegate> delegate;

- (instancetype)initWithPath:(NSString *)path NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

// Returns YES on success or NO if it failed to create a socket (out of file descriptors maybe?)
- (BOOL)attachOrLaunchServer;
- (BOOL)attach;

- (void)launchChildWithExecutablePath:(const char *)path
                                 argv:(const char **)argv
                          environment:(const char **)environment
                                  pwd:(const char *)pwd
                             ttyState:(iTermTTYState *)ttyStatePtr
                           completion:(void (^)(iTermFileDescriptorMultiClientChild * _Nullable child, NSError * _Nullable))completion;

- (void)waitForChild:(iTermFileDescriptorMultiClientChild *)child
          completion:(void (^)(int status, NSError * _Nullable))completion;

- (void)killServerAndAllChildren;

@end

NS_ASSUME_NONNULL_END
