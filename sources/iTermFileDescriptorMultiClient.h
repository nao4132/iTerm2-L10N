//
//  iTermFileDescriptorMultiClient.h
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#import <Foundation/Foundation.h>
#import "iTermMultiServerProtocol.h"
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

- (void)launchChildWithExecutablePath:(NSString *)path
                                 argv:(NSArray<NSString *> *)argv
                          environment:(NSDictionary<NSString *, NSString *> *)environment
                             gridSize:(VT100GridSize)gridSize
                                 utf8:(BOOL)utf8
                                  pwd:(NSString *)pwd
                           completion:(void (^)(iTermFileDescriptorMultiClientChild *, NSError * _Nullable))completion;

- (void)waitForChild:(iTermFileDescriptorMultiClientChild *)child
          completion:(void (^)(int status, NSError * _Nullable))completion;

- (void)killServerAndAllChildren;

@end

NS_ASSUME_NONNULL_END
