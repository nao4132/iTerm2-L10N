//
//  iTermFileDescriptorMultiClient+MRR.h
//  iTerm2
//
//  Created by George Nachman on 8/9/19.
//

#import "iTermFileDescriptorMultiClient.h"
#import "iTermPosixTTYReplacements.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSUInteger, iTermFileDescriptorMultiClientAttachStatus) {
    iTermFileDescriptorMultiClientAttachStatusSuccess,
    iTermFileDescriptorMultiClientAttachStatusConnectFailed,
    iTermFileDescriptorMultiClientAttachStatusFatalError
};

@interface iTermFileDescriptorMultiClient (Private)
- (iTermFileDescriptorMultiClientAttachStatus)tryAttach;
@end

@interface iTermFileDescriptorMultiClient (MRR)

- (iTermForkState)launchWithSocketPath:(NSString *)path
                            executable:(NSString *)executable;

@end

NS_ASSUME_NONNULL_END
