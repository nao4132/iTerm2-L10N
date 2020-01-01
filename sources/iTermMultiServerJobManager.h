//
//  iTermMultiServerJobManager.h
//  iTerm2SharedARC
//
//  Created by George Nachman on 11/25/19.
//

#import <Foundation/Foundation.h>
#import "PTYTask.h"

NS_ASSUME_NONNULL_BEGIN

extern NSString *const iTermMultiServerRestorationKeyType;
extern NSString *const iTermMultiServerRestorationKeyVersion;
extern NSString *const iTermMultiServerRestorationKeySocket;
extern NSString *const iTermMultiServerRestorationKeyChildPID;


@interface iTermMultiServerJobManager : NSObject<iTermJobManager>

+ (BOOL)getGeneralConnection:(iTermGeneralServerConnection *)generalConnection
   fromRestorationIdentifier:(NSDictionary *)dict;
@end

NS_ASSUME_NONNULL_END
