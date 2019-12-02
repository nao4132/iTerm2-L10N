//
//  iTermOrphanServerAdopter.h
//  iTerm2
//
//  Created by George Nachman on 6/7/15.
//
//

#import <Foundation/Foundation.h>
#import "PTYTask.h"

@protocol iTermOrphanServerAdopterDelegate<NSObject>
- (id)orphanServerAdopterOpenSessionForConnection:(iTermGeneralServerConnection)connection
                                         inWindow:(id)window;
@end

@interface iTermOrphanServerAdopter : NSObject

@property(nonatomic, readonly) BOOL haveOrphanServers;
@property(nonatomic, weak) id<iTermOrphanServerAdopterDelegate> delegate;

+ (instancetype)sharedInstance;
- (void)openWindowWithOrphans;
- (void)removePath:(NSString *)path;

@end
