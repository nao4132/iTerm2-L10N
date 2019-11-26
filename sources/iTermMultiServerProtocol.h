//
//  iTermMultiServerProtocol.h
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#import "iTermClientServerProtocol.h"

enum {
    iTermMultiServerProtocolVersionRejected = -1,
    iTermMultiServerProtocolVersion1 = 1
};

typedef enum {
    iTermMultiServerTagType,

    iTermMultiServerTagHandshakeRequestClientMaximumProtocolVersion,

    iTermMultiServerTagHandshakeResponseProtocolVersion,
    iTermMultiServerTagHandshakeResponseChildReportsNumChildren,
    iTermMultiServerTagHandshakeResponseProcessID,

    iTermMultiServerTagLaunchRequestPath,
    iTermMultiServerTagLaunchRequestArgv,
    iTermMultiServerTagLaunchRequestEnvironment,
    iTermMultiServerTagLaunchRequestWidth,
    iTermMultiServerTagLaunchRequestHeight,
    iTermMultiServerTagLaunchRequestIsUTF8,
    iTermMultiServerTagLaunchRequestPwd,
    iTermMultiServerTagLaunchRequestUniqueId,

    iTermMultiServerTagWaitRequestPid,

    iTermMultiServerTagWaitResponsePid,
    iTermMultiServerTagWaitResponseStatus,
    iTermMultiServerTagWaitResponseErrno,

    iTermMultiServerTagLaunchResponseStatus,
    iTermMultiServerTagLaunchResponsePid,

    iTermMultiServerTagReportChildIsLast,
    iTermMultiServerTagReportChildPid,
    iTermMultiServerTagReportChildPath,
    iTermMultiServerTagReportChildArgs,
    iTermMultiServerTagReportChildEnv,
    iTermMultiServerTagReportChildPwd,
    iTermMultiServerTagReportChildIsUTF8,
    iTermMultiServerTagReportChildTerminated,
    iTermMultiServerTagReportChildTTY,

    iTermMultiServerTagTerminationPid,
    iTermMultiServerTagTerminationStatus,
} iTermMultiServerTagLaunch;

typedef struct {
    // iTermMultiServerTagHandshakeRequestClientMaximumProtocolVersion
    int maximumProtocolVersion;
} iTermMultiServerRequestHandshake;

typedef struct {
    // iTermMultiServerTagHandshakeResponseProtocolVersion
    int protocolVersion;

    // iTermMultiServerTagHandshakeResponseChildReportsNumChildren
    int numChildren;

    // iTermMultiServerTagHandshakeResponseProcessID
    int pid;
} iTermMultiServerResponseHandshake;

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

    // File descriptor. Passed out of band.
    int fd;
} iTermMultiServerResponseLaunch;

typedef struct {
    // iTermMultiServerTagWaitRequestPid
    pid_t pid;
} iTermMultiServerRequestWait;

typedef struct {
    // iTermMultiServerTagWaitResponsePid
    pid_t pid;

    // iTermMultiServerTagWaitResponseStatus
    // Meaningful only if errorNumber is 0. Gives exit status from waitpid().
    int status;

    // iTermMultiServerTagWaitResponseErrno
    // 0: No error. Status is valid. Child has been removed.
    // -1: No such child
    // -2: Child not terminated
    int errorNumber;
} iTermMultiServerResponseWait;

typedef struct iTermMultiServerReportChild {
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

    // iTermMultiServerTagReportChildTerminated
    int terminated;  // you should send iTermMultiServerResponseWait

    // iTermMultiServerTagReportChildTTY
    char *tty;

    // Sent out-of-band
    int fd;
} iTermMultiServerReportChild;

typedef enum {
    iTermMultiServerRPCTypeHandshake,  // Client-originated, has response
    iTermMultiServerRPCTypeLaunch,  // Client-originated, has response
    iTermMultiServerRPCTypeWait,  // Client-originated, has response
    iTermMultiServerRPCTypeReportChild,  // Server-originated, no response.
    iTermMultiServerRPCTypeTermination  // Server-originated, no response.
} iTermMultiServerRPCType;

// You should send iTermMultiServerResponseWait after getting this.
typedef struct {
    // iTermMultiServerTagTerminationPid
    pid_t pid;
} iTermMultiServerReportTermination;

typedef struct {
    iTermMultiServerRPCType type;
    union {
        iTermMultiServerRequestHandshake handshake;
        iTermMultiServerRequestLaunch launch;
        iTermMultiServerRequestWait wait;
    } payload;
} iTermMultiServerClientOriginatedMessage;

typedef struct {
    iTermMultiServerRPCType type;
    union {
        iTermMultiServerResponseHandshake handshake;
        iTermMultiServerResponseLaunch launch;
        iTermMultiServerResponseWait wait;
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

// Reads a message from the file descriptor. Returns 0 on success. When successful, the message
// must be freed by the caller with iTermClientServerProtocolMessageFree().
int iTermMultiServerRecv(int fd, iTermClientServerProtocolMessage *message);
