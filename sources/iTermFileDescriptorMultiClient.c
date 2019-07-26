//
//  iTermFileDescriptorMultiClient.c
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#include "iTermFileDescriptorMultiClient.h"
#import "iTermMultiServerProtocol.h"

#include <unistd.h>

typedef struct {
    int ok;
    const char *error;
    int socketFd;
    pid_t serverPid;
} iTermFileDescriptorMultiServerConnection;

// Connects to the server at a path to a Unix Domain Socket inferred from `pid` and receives a file
// descriptor and PID for the child it owns. The socket is left open. When iTerm2 dies unexpectedly,
// the socket will be closed; the server won't accept another connection until that happens.
iTermFileDescriptorMultiServerConnection iTermFileDescriptorMultiClientAttach(pid_t pid);

typedef void iTermFileDescriptorMultiClientHandler(iTermMultiServerServerOriginatedMessage *);

// Spawn a multiserver. Blocks until it is running. The handler will be called when the server
// sends a message.
iTermFileDescriptorMultiServerConnection iTermFileDescriptorMultiServerCreate(iTermFileDescriptorMultiClientHandler handler);

// Request the server to launch a process. Eventually, the handler will be called with a child
// report or termination.
void iTermFileDescriptorMultiServerLaunch(char *path,
                                          int argc,
                                          char **argv,
                                          int envc,
                                          char **envp,
                                          int width,
                                          int height,
                                          int isUTF8,
                                          char *pwd,
                                          long long uniqueId);
