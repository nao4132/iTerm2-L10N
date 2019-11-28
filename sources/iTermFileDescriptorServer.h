#ifndef __ITERM_FILE_DESCRIPTOR_SERVER_H
#define __ITERM_FILE_DESCRIPTOR_SERVER_H

#include "iTermFileDescriptorServerShared.h"

// Spin up a new server. |connectionFd| comes from iTermFileDescriptorServerAccept(),
// which should be run prior to fork()ing.
int iTermFileDescriptorServerRun(char *path, pid_t childPid, int connectionFd);

// Create a socket and listen on it. Returns the socket's file descriptor.
// This is used for connecting a client and server prior to fork.
// Follow it with a call to iTermFileDescriptorServerAccept().
int iTermFileDescriptorServerSocketBindListen(const char *path);

// Wait for a client connection on |socketFd|, which comes from
// iTermFileDescriptorServerSocketBindListen(). Returns a connection file descriptor,
// suitable to pass to iTermFileDescriptorServerRun() in |connectionFd|.
int iTermFileDescriptorServerAccept(int socketFd);

void iTermFileDescriptorServerLog(char *format, ...);

void SetRunningServer(void);

#endif  // __ITERM_FILE_DESCRIPTOR_SERVER_H
