//
//  iTermOrphanServerAdopter.m
//  iTerm2
//
//  Created by George Nachman on 6/7/15.
//
//

#import "iTermOrphanServerAdopter.h"

#import "DebugLogging.h"
#import "iTermAdvancedSettingsModel.h"
#import "iTermController.h"
#import "iTermFileDescriptorSocketPath.h"
#import "iTermMultiServerConnection.h"
#import "NSApplication+iTerm.h"
#import "NSArray+iTerm.h"
#import "NSFileManager+iTerm.h"
#import "PseudoTerminal.h"

@implementation iTermOrphanServerAdopter {
    NSArray<NSString *> *_pathsOfOrphanedMonoServers;
    NSArray<NSString *> *_pathsOfMultiServers;
    __weak PseudoTerminal *_window;
}

+ (instancetype)sharedInstance {
    static id instance;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

NSArray<NSString *> *iTermOrphanServerAdopterFindMonoServers(void) {
    NSMutableArray *array = [NSMutableArray array];
    NSString *dir = [NSString stringWithUTF8String:iTermFileDescriptorDirectory()];
    for (NSString *filename in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil]) {
        NSString *prefix = [NSString stringWithUTF8String:iTermFileDescriptorSocketNamePrefix];
        if ([filename hasPrefix:prefix]) {
            [array addObject:[dir stringByAppendingPathComponent:filename]];
        }
    }
    return array;
}

NSArray<NSString *> *iTermOrphanServerAdopterFindMultiServers(void) {
    NSMutableArray<NSString *> *result = [NSMutableArray array];
    NSString *appSupportPath = [[NSFileManager defaultManager] applicationSupportDirectory];
    NSDirectoryEnumerator *enumerator =
    [[NSFileManager defaultManager] enumeratorAtURL:[NSURL fileURLWithPath:appSupportPath]
                         includingPropertiesForKeys:nil
                                            options:NSDirectoryEnumerationSkipsSubdirectoryDescendants
                                       errorHandler:nil];
    for (NSURL *url in enumerator) {
        if (![url.path.lastPathComponent stringMatchesGlobPattern:@"daemon-*.socket" caseSensitive:YES]) {
            continue;
        }
        [result addObject:url.path];
    }
    return result;
}

- (instancetype)init {
    if (![iTermAdvancedSettingsModel runJobsInServers]) {
        return nil;
    }
    if ([[NSApplication sharedApplication] isRunningUnitTests]) {
        return nil;
    }
    self = [super init];
    if (self) {
        if ([iTermAdvancedSettingsModel multiserver]) {
            _pathsOfMultiServers = iTermOrphanServerAdopterFindMultiServers();
        } else {
            _pathsOfOrphanedMonoServers = iTermOrphanServerAdopterFindMonoServers();
        }
    }
    return self;
}

- (void)removePath:(NSString *)path {
    _pathsOfOrphanedMonoServers = [_pathsOfOrphanedMonoServers arrayByRemovingObject:path];
    _pathsOfMultiServers = [_pathsOfMultiServers arrayByRemovingObject:path];
}

- (void)openWindowWithOrphans {
    for (NSString *path in _pathsOfOrphanedMonoServers) {
        [self adoptMonoServerOrphanWithPath:path];
    }
    for (NSString *path in _pathsOfMultiServers) {
        [self adoptMultiServerOrphansWithPath:path];
    }
    _window = nil;
}

- (void)adoptMonoServerOrphanWithPath:(NSString *)filename {
    DLog(@"Try to connect to orphaned server at %@", filename);
    pid_t pid = iTermFileDescriptorProcessIdFromPath(filename.UTF8String);
    if (pid < 0) {
        DLog(@"Invalid pid in filename %@", filename);
        return;
    }

    iTermFileDescriptorServerConnection serverConnection = iTermFileDescriptorClientRun(pid);
    if (serverConnection.ok) {
        DLog(@"Restore it");
        iTermGeneralServerConnection generalConnection = {
            .type = iTermGeneralServerConnectionTypeMono,
            .mono = serverConnection
        };
        _window = [self.delegate orphanServerAdopterOpenSessionForConnection:generalConnection
                                                                    inWindow:_window];
    } else {
        DLog(@"Failed: %s", serverConnection.error);
    }
}

- (void)adoptMultiServerOrphansWithPath:(NSString *)filename {
    DLog(@"Try to connect to multiserver at %@", filename);
    NSString *basename = filename.lastPathComponent.stringByDeletingPathExtension;
    NSString *const prefix = @"daemon-";
    assert([basename hasPrefix:prefix]);
    NSString *numberAsString = [basename substringFromIndex:prefix.length];
    NSScanner *scanner = [NSScanner scannerWithString:numberAsString];
    NSInteger number = -1;
    if (![scanner scanInteger:&number]) {
        return;
    }
    iTermMultiServerConnection *connection = [iTermMultiServerConnection connectionForSocketNumber:number
                                                                                  createIfPossible:NO];
    if (connection == nil) {
        NSLog(@"Failed to connect to %@", filename);
        return;
    }

    NSArray<iTermFileDescriptorMultiClientChild *> *children = [connection.unattachedChildren copy];
    for (iTermFileDescriptorMultiClientChild *child in children) {
        iTermGeneralServerConnection generalConnection = {
            .type = iTermGeneralServerConnectionTypeMulti,
            .multi = {
                .pid = child.pid,
                .number = number
            }
        };
        _window = [self.delegate orphanServerAdopterOpenSessionForConnection:generalConnection
                                                                    inWindow:_window];
    }
}

#pragma mark - Properties

- (BOOL)haveOrphanServers {
    return _pathsOfOrphanedMonoServers.count > 0;
}

@end
