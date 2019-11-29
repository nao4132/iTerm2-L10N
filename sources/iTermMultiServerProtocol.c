//
//  iTermMultiServerProtocol.c
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#import "iTermMultiServerProtocol.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int ParseHandshakeRequest(iTermClientServerProtocolMessageParser *parser,
                                 iTermMultiServerRequestHandshake *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->maximumProtocolVersion, sizeof(out->maximumProtocolVersion), iTermMultiServerTagHandshakeRequestClientMaximumProtocolVersion)) {
        return -1;
    }
    return 0;
}

static int EncodeHandshakeRequest(iTermClientServerProtocolMessageEncoder *encoder,
                                  iTermMultiServerRequestHandshake *handshake) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &handshake->maximumProtocolVersion, sizeof(handshake->maximumProtocolVersion), iTermMultiServerTagHandshakeRequestClientMaximumProtocolVersion)) {
        return -1;
    }
    return 0;
}

static int ParseHandshakeResponse(iTermClientServerProtocolMessageParser *parser,
                                  iTermMultiServerResponseHandshake *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->protocolVersion, sizeof(out->protocolVersion), iTermMultiServerTagHandshakeResponseProtocolVersion)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->numChildren, sizeof(out->numChildren), iTermMultiServerTagHandshakeResponseChildReportsNumChildren)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->pid, sizeof(out->pid), iTermMultiServerTagHandshakeResponseProcessID)) {
        return -1;
    }
    if (out->numChildren < 0 || out->numChildren > 1024) {
        return -1;
    }
    return 0;
}

static int EncodeHandshakeResponse(iTermClientServerProtocolMessageEncoder *encoder,
                                   iTermMultiServerResponseHandshake *handshake) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &handshake->protocolVersion, sizeof(handshake->protocolVersion), iTermMultiServerTagHandshakeResponseProtocolVersion)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &handshake->numChildren, sizeof(handshake->numChildren), iTermMultiServerTagHandshakeResponseChildReportsNumChildren)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &handshake->pid, sizeof(handshake->pid), iTermMultiServerTagHandshakeResponseProcessID)) {
        return -1;
    }
    return 0;
}

static int ParseLaunchReqest(iTermClientServerProtocolMessageParser *parser,
                             iTermMultiServerRequestLaunch *out) {
    if (iTermClientServerProtocolParseTaggedString(parser, (char **)&out->path, iTermMultiServerTagLaunchRequestPath)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, (char ***)&out->argv, &out->argc, iTermMultiServerTagLaunchRequestArgv)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, (char ***)&out->envp, &out->envc, iTermMultiServerTagLaunchRequestEnvironment)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->width, sizeof(out->width), iTermMultiServerTagLaunchRequestWidth)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->height, sizeof(out->height), iTermMultiServerTagLaunchRequestHeight)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->isUTF8, sizeof(out->isUTF8), iTermMultiServerTagLaunchRequestIsUTF8)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedString(parser, (char **)&out->pwd, iTermMultiServerTagLaunchRequestPwd)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->uniqueId, sizeof(out->uniqueId), iTermMultiServerTagLaunchRequestUniqueId)) {
        return -1;
    }
    return 0;
}

static int EncodeLaunchRequest(iTermClientServerProtocolMessageEncoder *encoder,
                               iTermMultiServerRequestLaunch *launch) {
    if (iTermClientServerProtocolEncodeTaggedString(encoder, launch->path, iTermMultiServerTagLaunchRequestPath)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, (char **)launch->argv, launch->argc, iTermMultiServerTagLaunchRequestArgv)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, (char **)launch->envp, launch->envc, iTermMultiServerTagLaunchRequestEnvironment)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &launch->width, sizeof(launch->width), iTermMultiServerTagLaunchRequestWidth)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &launch->height, sizeof(launch->height), iTermMultiServerTagLaunchRequestHeight)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &launch->isUTF8, sizeof(launch->isUTF8), iTermMultiServerTagLaunchRequestIsUTF8)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedString(encoder, launch->pwd, iTermMultiServerTagLaunchRequestPwd)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &launch->uniqueId, sizeof(launch->uniqueId), iTermMultiServerTagLaunchRequestUniqueId)) {
        return -1;
    }
    return 0;
}

static int ParseLaunchResponse(iTermClientServerProtocolMessageParser *parser,
                               iTermMultiServerResponseLaunch *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->status, sizeof(out->status), iTermMultiServerTagLaunchResponseStatus)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->pid, sizeof(out->pid), iTermMultiServerTagLaunchResponsePid)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->uniqueId, sizeof(out->uniqueId), iTermMultiServerTagLaunchResponseUniqueID)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedString(parser, (char **)&out->tty, iTermMultiServerTagLaunchResponseTty)) {
        return -1;
    }
    return 0;
}

static int EncodeLaunchResponse(iTermClientServerProtocolMessageEncoder *encoder,
                                iTermMultiServerResponseLaunch *launch) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &launch->status, sizeof(launch->status), iTermMultiServerTagLaunchResponseStatus)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &launch->pid, sizeof(launch->pid), iTermMultiServerTagLaunchResponsePid)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &launch->uniqueId, sizeof(launch->uniqueId), iTermMultiServerTagLaunchResponseUniqueID)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedString(encoder, launch->tty, iTermMultiServerTagLaunchResponseTty)) {
        return -1;
    }
    return 0;
}

static int ParseWaitRequest(iTermClientServerProtocolMessageParser *parser,
                            iTermMultiServerRequestWait *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->pid, sizeof(out->pid), iTermMultiServerTagWaitRequestPid)) {
        return -1;
    }
    return 0;
}

static int EncodeWaitRequest(iTermClientServerProtocolMessageEncoder *encoder,
                             iTermMultiServerRequestWait *wait) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &wait->pid, sizeof(wait->pid), iTermMultiServerTagWaitRequestPid)) {
        return -1;
    }
    return 0;
}

static int ParseWaitResponse(iTermClientServerProtocolMessageParser *parser,
                             iTermMultiServerResponseWait *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->pid, sizeof(out->pid), iTermMultiServerTagWaitResponsePid)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->status, sizeof(out->status), iTermMultiServerTagWaitResponseStatus)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->errorNumber, sizeof(out->errorNumber), iTermMultiServerTagWaitResponseErrno)) {
        return -1;
    }
    return 0;
}

static int EncodeWaitResponse(iTermClientServerProtocolMessageEncoder *encoder,
                              iTermMultiServerResponseWait *wait) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &wait->pid, sizeof(wait->pid), iTermMultiServerTagWaitResponsePid)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &wait->status, sizeof(wait->status), iTermMultiServerTagWaitResponseStatus)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &wait->errorNumber, sizeof(wait->errorNumber), iTermMultiServerTagWaitResponseErrno)) {
        return -1;
    }
    return 0;
}

static int ParseReportChild(iTermClientServerProtocolMessageParser *parser,
                            iTermMultiServerReportChild *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->isLast, sizeof(out->isLast), iTermMultiServerTagReportChildIsLast)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->pid, sizeof(out->pid), iTermMultiServerTagReportChildPid)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedString(parser, (char **)&out->path, iTermMultiServerTagReportChildPath)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, (char ***)&out->argv, &out->argc, iTermMultiServerTagReportChildArgs)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, (char ***)&out->envp, &out->envc, iTermMultiServerTagReportChildEnv)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->isUTF8, sizeof(out->isUTF8), iTermMultiServerTagReportChildIsUTF8)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedString(parser, (char **)&out->pwd, iTermMultiServerTagReportChildPwd)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->terminated, sizeof(out->terminated), iTermMultiServerTagReportChildTerminated)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedString(parser, (char **)&out->tty, iTermMultiServerTagReportChildTTY)) {
        return -1;
    }
    return 0;
}

static int EncodeReportChild(iTermClientServerProtocolMessageEncoder *encoder,
                             iTermMultiServerReportChild *obj) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->isLast, sizeof(obj->isLast), iTermMultiServerTagReportChildIsLast)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->pid, sizeof(obj->pid), iTermMultiServerTagReportChildPid)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedString(encoder, obj->path, iTermMultiServerTagReportChildPath)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, (char **)obj->argv, obj->argc, iTermMultiServerTagReportChildArgs)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, (char **)obj->envp, obj->envc, iTermMultiServerTagReportChildEnv)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->isUTF8, sizeof(obj->isUTF8), iTermMultiServerTagReportChildIsUTF8)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedString(encoder, obj->pwd, iTermMultiServerTagReportChildPwd)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->terminated, sizeof(obj->terminated), iTermMultiServerTagReportChildTerminated)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedString(encoder, obj->tty, iTermMultiServerTagReportChildTTY)) {
        return -1;
    }
    return 0;
}

static int ParseTermination(iTermClientServerProtocolMessageParser *parser,
                            iTermMultiServerReportTermination *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->pid, sizeof(out->pid), iTermMultiServerTagTerminationPid)) {
        return -1;
    }
    return 0;
}

static int EncodeTermination(iTermClientServerProtocolMessageEncoder *encoder,
                             iTermMultiServerReportTermination *obj) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->pid, sizeof(obj->pid), iTermMultiServerTagTerminationPid)) {
        return -1;
    }
    return 0;
}

#pragma mark - APIs

int iTermMultiServerProtocolParseMessageFromClient(iTermClientServerProtocolMessage *message,
                                                   iTermMultiServerClientOriginatedMessage *out) {
    iTermClientServerProtocolMessageParser parser = {
        .offset = 0,
        .message = message
    };

    if (iTermClientServerProtocolParseTaggedInt(&parser, &out->type, sizeof(out->type), iTermMultiServerTagType)) {
        return -1;
    }
    switch (out->type) {
        case iTermMultiServerRPCTypeHandshake:
            return ParseHandshakeRequest(&parser, &out->payload.handshake);
        case iTermMultiServerRPCTypeLaunch:
            return ParseLaunchReqest(&parser, &out->payload.launch);
        case iTermMultiServerRPCTypeWait:
            return ParseWaitRequest(&parser, &out->payload.wait);

        case iTermMultiServerRPCTypeReportChild:  // Server-originated, no response.
        case iTermMultiServerRPCTypeTermination: // Server-originated, no response.
            return -1;
    }
    return -1;
}

static int GetFileDescriptor(iTermClientServerProtocolMessage *message,
                             int *receivedFileDescriptorPtr) {
    // Should be this:
//    struct cmsghdr *messageHeader = CMSG_FIRSTHDR(&message->message);
    // But because the structure is copied you can't trust the pointer.
    struct cmsghdr *messageHeader = &message->controlBuffer.cm;
    if (messageHeader->cmsg_len != CMSG_LEN(sizeof(int))) {
        return -1;
    }
    if (messageHeader->cmsg_level != SOL_SOCKET) {
        return -1;
    }
    if (messageHeader->cmsg_type != SCM_RIGHTS) {
        return -1;
    }
    *receivedFileDescriptorPtr = *((int *)CMSG_DATA(messageHeader));
    return 0;
}

int iTermMultiServerProtocolParseMessageFromServer(iTermClientServerProtocolMessage *message,
                                                   iTermMultiServerServerOriginatedMessage *out) {
    iTermClientServerProtocolMessageParser parser = {
        .offset = 0,
        .message = message
    };

    if (iTermClientServerProtocolParseTaggedInt(&parser, &out->type, sizeof(out->type), iTermMultiServerTagType)) {
        return -1;
    }
    switch (out->type) {
        case iTermMultiServerRPCTypeHandshake:
            return ParseHandshakeResponse(&parser, &out->payload.handshake);

        case iTermMultiServerRPCTypeLaunch:  // Server-originated response to client-originated request
            if (ParseLaunchResponse(&parser, &out->payload.launch)) {
                return -1;
            }
            if (GetFileDescriptor(message, &out->payload.launch.fd)) {
                return -1;
            }
            return 0;

        case iTermMultiServerRPCTypeReportChild:  // Server-originated, no response.
            if (ParseReportChild(&parser, &out->payload.reportChild)) {
                return -1;
            }
            if (GetFileDescriptor(message, &out->payload.reportChild.fd)) {
                return -1;
            }
            return 0;

        case iTermMultiServerRPCTypeWait:
            return ParseWaitResponse(&parser, &out->payload.wait);

        case iTermMultiServerRPCTypeTermination: // Server-originated, no response.
            return ParseTermination(&parser, &out->payload.termination);
    }
    return -1;
}

int iTermMultiServerProtocolEncodeMessageFromClient(iTermMultiServerClientOriginatedMessage *obj,
                                                    iTermClientServerProtocolMessage *message) {
    iTermClientServerProtocolMessageEncoder encoder = {
        .offset = 0,
        .message = message
    };

    int status = iTermClientServerProtocolEncodeTaggedInt(&encoder, &obj->type, sizeof(obj->type), iTermMultiServerTagType);
    if (status) {
        return status;
    }
    switch (obj->type) {
        case iTermMultiServerRPCTypeHandshake:
            status = EncodeHandshakeRequest(&encoder, &obj->payload.handshake);
            break;

        case iTermMultiServerRPCTypeLaunch:
            status = EncodeLaunchRequest(&encoder, &obj->payload.launch);
            break;

        case iTermMultiServerRPCTypeWait:
            status = EncodeWaitRequest(&encoder, &obj->payload.wait);
            break;

        case iTermMultiServerRPCTypeReportChild:
        case iTermMultiServerRPCTypeTermination:
            break;
    }
    if (!status) {
        iTermEncoderCommit(&encoder);
    }
    return status;
}

int iTermMultiServerProtocolEncodeMessageFromServer(iTermMultiServerServerOriginatedMessage *obj,
                                                    iTermClientServerProtocolMessage *message) {
    iTermClientServerProtocolMessageEncoder encoder = {
        .offset = 0,
        .message = message
    };
    int status = iTermClientServerProtocolEncodeTaggedInt(&encoder, &obj->type, sizeof(obj->type), iTermMultiServerTagType);
    if (status) {
        return status;
    }
    switch (obj->type) {
        case iTermMultiServerRPCTypeHandshake:
            status = EncodeHandshakeResponse(&encoder, &obj->payload.handshake);
            break;
        case iTermMultiServerRPCTypeLaunch:
            status = EncodeLaunchResponse(&encoder, &obj->payload.launch);
            break;
        case iTermMultiServerRPCTypeWait:
            status = EncodeWaitResponse(&encoder, &obj->payload.wait);
            break;
        case iTermMultiServerRPCTypeReportChild:
            status = EncodeReportChild(&encoder, &obj->payload.reportChild);
            break;
        case iTermMultiServerRPCTypeTermination:
            status = EncodeTermination(&encoder, &obj->payload.termination);
            break;
    }
    if (!status) {
        iTermEncoderCommit(&encoder);
    }
    return status;
}

static void FreeLaunchRequest(iTermMultiServerRequestLaunch *obj) {
    free((void *)obj->path);
    for (int i = 0; i < obj->argc; i++) {
        free((void *)obj->argv[i]);
    }
    free((void *)obj->argv);
    for (int i = 0; i < obj->envc; i++) {
        free((void *)obj->envp[i]);
    }
    free((void *)obj->envp);
    free((void *)obj->pwd);
    memset(obj, 0xab, sizeof(*obj));
}

static void FreeReportChild(iTermMultiServerReportChild *obj) {
    free((void *)obj->path);
    for (int i = 0; i < obj->argc; i++) {
        free((void *)obj->argv[i]);
    }
    free((void *)obj->argv);
    for (int i = 0; i < obj->envc; i++) {
        free((void *)obj->envp[i]);
    }
    free((void *)obj->envp);
    free((void *)obj->tty);
    memset(obj, 0xab, sizeof(*obj));
}

static void FreeWaitRequest(iTermMultiServerRequestWait *wait) {
}

static void FreeWaitResponse(iTermMultiServerResponseWait *wait) {
}

static void FreeHandshakeRequest(iTermMultiServerRequestHandshake *handshake) {
}

static void FreeHandshakeResponse(iTermMultiServerResponseHandshake *handshake) {
}

static void FreeLaunchResponse(iTermMultiServerResponseLaunch *launch) {
    free((void *)launch->tty);
}

void iTermMultiServerClientOriginatedMessageFree(iTermMultiServerClientOriginatedMessage *obj) {
    switch (obj->type) {
        case iTermMultiServerRPCTypeHandshake:
            FreeHandshakeRequest(&obj->payload.handshake);
            break;
        case iTermMultiServerRPCTypeLaunch:
            FreeLaunchRequest(&obj->payload.launch);
            break;
        case iTermMultiServerRPCTypeWait:
            FreeWaitRequest(&obj->payload.wait);
            break;
        case iTermMultiServerRPCTypeReportChild:
        case iTermMultiServerRPCTypeTermination:
            break;
    }
    memset(obj, 0xAB, sizeof(*obj));
}

void iTermMultiServerServerOriginatedMessageFree(iTermMultiServerServerOriginatedMessage *obj) {
    switch (obj->type) {
        case iTermMultiServerRPCTypeHandshake:
            FreeHandshakeResponse(&obj->payload.handshake);
            break;
        case iTermMultiServerRPCTypeLaunch:
            FreeLaunchResponse(&obj->payload.launch);
            break;
        case iTermMultiServerRPCTypeWait:
            FreeWaitResponse(&obj->payload.wait);
            break;
        case iTermMultiServerRPCTypeReportChild:
            FreeReportChild(&obj->payload.reportChild);
            break;
        case iTermMultiServerRPCTypeTermination:
            break;
    }
    memset(obj, 0xCD, sizeof(*obj));
}

static ssize_t RecvMsg(int fd,
                       iTermClientServerProtocolMessage *message) {
    assert(message->valid == ITERM_MULTISERVER_MAGIC);

    ssize_t n = -1;
    do {
        n = recvmsg(fd, &message->message, 0);
    } while (n < 0 && errno == EINTR);

    return n;
}

int iTermMultiServerRecv(int fd, iTermClientServerProtocolMessage *message) {
    iTermClientServerProtocolMessageInitialize(message);

    const ssize_t recvStatus = RecvMsg(fd, message);
    if (recvStatus <= 0) {
        iTermClientServerProtocolMessageFree(message);
        return 1;
    }

    return 0;
}
