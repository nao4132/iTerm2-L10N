//
//  iTermMultiServerProtocol.h
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#import "iTermClientServerProtocol.h"

typedef enum {
    iTermMultiServerTagType,

    iTermMultiServerTagLaunchRequestPath,
    iTermMultiServerTagLaunchRequestArgv,
    iTermMultiServerTagLaunchRequestEnvironment,
    iTermMultiServerTagLaunchRequestWidth,
    iTermMultiServerTagLaunchRequestHeight,
    iTermMultiServerTagLaunchRequestIsUTF8,
    iTermMultiServerTagLaunchRequestPwd,
    iTermMultiServerTagLaunchRequestUniqueId,

    iTermMultiServerTagLaunchResponseStatus,
    iTermMultiServerTagLaunchResponsePid,

    iTermMultiServerTagReportChildIsLast,
    iTermMultiServerTagReportChildPid,
    iTermMultiServerTagReportChildPath,
    iTermMultiServerTagReportChildArgs,
    iTermMultiServerTagReportChildEnv,
    iTermMultiServerTagReportChildPwd,
    iTermMultiServerTagReportChildIsUTF8,

    iTermMultiServerTagTerminationPid,
    iTermMultiServerTagTerminationStatus,

} iTermMultiServerTagLaunch;

typedef struct {
    // iTermMultiServerTagLaunchRequestPath
    char *path;

    // iTermMultiServerTagLaunchRequestArgv
    char **argv;
    int argc;

    // iTermMultiServerTagLaunchRequestEnvironment
    char **envp;
    int envc;

    // iTermMultiServerTagLaunchRequestWidth
    int width;

    // iTermMultiServerTagLaunchRequestHeight
    int height;

    // iTermMultiServerTagLaunchRequestIsUTF8
    int isUTF8;

    // iTermMultiServerTagLaunchRequestPwd
    char *pwd;

    // iTermMultiServerTagLaunchRequestUniqueId
    long long uniqueId;
} iTermMultiServerRequestLaunch;

// NOTE: The PTY master file descriptor is also passed with this message.
typedef struct {
    // 0 means success. Otherwise, gives errno from fork or execve.
    // iTermMultiServerTagLaunchResponseStatus
    int status;

    // Only defined if status is 0.
    // iTermMultiServerTagLaunchResponsePid
    pid_t pid;
} iTermMultiServerResponseLaunch;

typedef struct {
    // iTermMultiServerTagReportChildIsLast
    int isLast;

    // iTermMultiServerTagReportChildPid
    pid_t pid;

    // iTermMultiServerTagReportChildPath
    char *path;

    // iTermMultiServerTagReportChildArgs
    char **argv;
    int argc;

    // iTermMultiServerTagReportChildEnv
    char **envp;
    int envc;

    // iTermMultiServerTagReportChildIsUTF8
    int isUTF8;

    // iTermMultiServerTagReportChildPwd
    char *pwd;
} iTermMultiServerReportChild;

typedef enum {
    iTermMultiServerRPCTypeLaunch,  // Client-originated, has response
    iTermMultiServerRPCTypeReportChild,  // Server-originated, no response.
    iTermMultiServerRPCTypeTermination  // Server-originated, no response.
} iTermMultiServerRPCType;

typedef struct {
    // iTermMultiServerTagTerminationPid
    pid_t pid;

    // See stat_loc argument to wait(2).
    // iTermMultiServerTagTerminationStatus
    int status;
} iTermMultiServerReportTermination;


typedef struct {
    iTermMultiServerRPCType type;
    union {
        iTermMultiServerRequestLaunch launch;
    } payload;
} iTermMultiServerClientOriginatedMessage;

typedef struct {
    iTermMultiServerRPCType type;
    union {
        iTermMultiServerResponseLaunch launch;
        iTermMultiServerReportTermination termination;
        iTermMultiServerReportChild reportChild;
    } payload;
} iTermMultiServerServerOriginatedMessage;

int iTermMultiServerProtocolParseMessageFromClient(iTermClientServerProtocolMessage *message,
                                                   iTermMultiServerClientOriginatedMessage *out);

int iTermMultiServerProtocolEncodeMessageFromClient(iTermMultiServerClientOriginatedMessage *obj,
                                                    iTermClientServerProtocolMessage *message);

int iTermMultiServerProtocolParseMessageFromServer(iTermClientServerProtocolMessage *message,
                                                   iTermMultiServerServerOriginatedMessage *out);

int iTermMultiServerProtocolEncodeMessageFromServer(iTermMultiServerServerOriginatedMessage *obj,
                                                    iTermClientServerProtocolMessage *message);

void iTermMultiServerClientOriginatedMessageFree(iTermMultiServerClientOriginatedMessage *obj);
void iTermMultiServerServerOriginatedMessageFree(iTermMultiServerServerOriginatedMessage *obj);

