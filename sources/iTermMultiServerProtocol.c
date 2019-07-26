//
//  iTermMultiServerProtocol.c
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#import "iTermMultiServerProtocol.h"

#include <stdlib.h>
#include <string.h>

static int ParseLaunchReqest(iTermClientServerProtocolMessageParser *parser,
                             iTermMultiServerRequestLaunch *out) {
    if (iTermClientServerProtocolParseTaggedString(parser, &out->path, iTermMultiServerTagLaunchRequestPath)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, &out->argv, &out->argc, iTermMultiServerTagLaunchRequestArgv)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, &out->envp, &out->envc, iTermMultiServerTagLaunchRequestEnvironment)) {
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
    if (iTermClientServerProtocolParseTaggedString(parser, &out->pwd, iTermMultiServerTagLaunchRequestPwd)) {
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
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, launch->argv, launch->argc, iTermMultiServerTagLaunchRequestArgv)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, launch->envp, launch->envc, iTermMultiServerTagLaunchRequestEnvironment)) {
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
    if (iTermClientServerProtocolParseTaggedString(parser, &out->path, iTermMultiServerTagReportChildPath)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, &out->argv, &out->argc, iTermMultiServerTagReportChildArgs)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedStringArray(parser, &out->envp, &out->envc, iTermMultiServerTagReportChildEnv)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->isUTF8, sizeof(out->isUTF8), iTermMultiServerTagReportChildIsUTF8)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedString(parser, &out->pwd, iTermMultiServerTagReportChildPwd)) {
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
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, obj->argv, obj->argc, iTermMultiServerTagReportChildArgs)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedStringArray(encoder, obj->envp, obj->envc, iTermMultiServerTagReportChildEnv)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->isUTF8, sizeof(obj->isUTF8), iTermMultiServerTagReportChildIsUTF8)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedString(encoder, obj->pwd, iTermMultiServerTagReportChildPwd)) {
        return -1;
    }
    return 0;
}

static int ParseTermination(iTermClientServerProtocolMessageParser *parser,
                            iTermMultiServerReportTermination *out) {
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->pid, sizeof(out->pid), iTermMultiServerTagTerminationPid)) {
        return -1;
    }
    if (iTermClientServerProtocolParseTaggedInt(parser, &out->status, sizeof(out->status), iTermMultiServerTagTerminationStatus)) {
        return -1;
    }
    return 0;
}

static int EncodeTermination(iTermClientServerProtocolMessageEncoder *encoder,
                             iTermMultiServerReportTermination *obj) {
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->pid, sizeof(obj->pid), iTermMultiServerTagTerminationPid)) {
        return -1;
    }
    if (iTermClientServerProtocolEncodeTaggedInt(encoder, &obj->status, sizeof(obj->status), iTermMultiServerTagTerminationStatus)) {
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
        case iTermMultiServerRPCTypeLaunch:
            return ParseLaunchReqest(&parser, &out->payload.launch);

        case iTermMultiServerRPCTypeReportChild:  // Server-originated, no response.
        case iTermMultiServerRPCTypeTermination: // Server-originated, no response.
            return -1;
    }
    return -1;
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
        case iTermMultiServerRPCTypeLaunch:  // Server-originated response to client-originated request
            return ParseLaunchResponse(&parser, &out->payload.launch);

        case iTermMultiServerRPCTypeReportChild:  // Server-originated, no response.
            return ParseReportChild(&parser, &out->payload.reportChild);

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

    int status = -1;
    switch (obj->type) {
        case iTermMultiServerRPCTypeLaunch:
            status = EncodeLaunchRequest(&encoder, &obj->payload.launch);
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

    int status = -1;
    switch (obj->type) {
        case iTermMultiServerRPCTypeLaunch:
            status = EncodeLaunchResponse(&encoder, &obj->payload.launch);
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
    free(obj->path);
    for (int i = 0; i < obj->argc; i++) {
        free(obj->argv[i]);
    }
    free(obj->argv);
    for (int i = 0; i < obj->envc; i++) {
        free(obj->envp[i]);
    }
    free(obj->envp);
    free(obj->pwd);
}

static void FreeReportChild(iTermMultiServerReportChild *obj) {
    free(obj->path);
    for (int i = 0; i < obj->argc; i++) {
        free(obj->argv[i]);
    }
    free(obj->argv);
    for (int i = 0; i < obj->envc; i++) {
        free(obj->envp[i]);
    }
    free(obj->envp);
}

void iTermMultiServerClientOriginatedMessageFree(iTermMultiServerClientOriginatedMessage *obj) {
    switch (obj->type) {
        case iTermMultiServerRPCTypeLaunch:
            FreeLaunchRequest(&obj->payload.launch);
            break;
        case iTermMultiServerRPCTypeReportChild:
        case iTermMultiServerRPCTypeTermination:
            break;
    }
    memset(obj, 0xAB, sizeof(*obj));
}

void iTermMultiServerServerOriginatedMessageFree(iTermMultiServerServerOriginatedMessage *obj) {
    switch (obj->type) {
        case iTermMultiServerRPCTypeLaunch:
            break;
        case iTermMultiServerRPCTypeReportChild:
            FreeReportChild(&obj->payload.reportChild);
            break;
        case iTermMultiServerRPCTypeTermination:
            break;
    }
    memset(obj, 0xCD, sizeof(*obj));
}
