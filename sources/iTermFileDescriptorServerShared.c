//
//  iTermFileDescriptorServerShared.c
//  iTerm2
//
//  Created by George Nachman on 11/26/19.
//

#include "iTermFileDescriptorServerShared.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

static int gRunningServer;

void SetRunningServer(void) {
    gRunningServer = 1;
}

void iTermFileDescriptorServerLog(char *format, ...) {
    va_list args;
    va_start(args, format);
    char temp[1000];
    snprintf(temp, sizeof(temp) - 1, "%s(%d) %s", gRunningServer ? "Server" : "ParentServer", getpid(), format);
    vsyslog(LOG_DEBUG, temp, args);
    va_end(args);
}

int iTermFileDescriptorServerAccept(int socketFd) {
    // incoming unix domain socket connection to get FDs
    struct sockaddr_un remote;
    socklen_t sizeOfRemote = sizeof(remote);
    int connectionFd = -1;
    do {
        FDLog(LOG_DEBUG, "accept()");
        connectionFd = accept(socketFd, (struct sockaddr *)&remote, &sizeOfRemote);
        FDLog(LOG_DEBUG, "accept() returned %d error=%s", connectionFd, strerror(errno));
    } while (connectionFd == -1 && errno == EINTR);
    if (connectionFd != -1) {
        close(socketFd);
    }
    return connectionFd;
}

// Returns number of bytes sent, or -1 for error.
ssize_t iTermFileDescriptorServerSendMessage(int connectionFd,
                                             void *buffer,
                                             size_t bufferSize) {
    struct msghdr message;
    memset(&message, 0, sizeof(message));

    struct iovec iov[1];
    iov[0].iov_base = buffer;
    iov[0].iov_len = bufferSize;
    message.msg_iov = iov;
    message.msg_iovlen = 1;

    errno = 0;
    ssize_t rc = sendmsg(connectionFd, &message, 0);
    while (rc == -1 && errno == EINTR) {
        rc = sendmsg(connectionFd, &message, 0);
    }
    return rc;
}

ssize_t iTermFileDescriptorServerSendMessageAndFileDescriptor(int connectionFd,
                                                              void *buffer,
                                                              size_t bufferSize,
                                                              int fdToSend) {
    iTermFileDescriptorControlMessage controlMessage;
    struct msghdr message;
    message.msg_control = controlMessage.control;
    message.msg_controllen = sizeof(controlMessage.control);

    struct cmsghdr *messageHeader = CMSG_FIRSTHDR(&message);
    messageHeader->cmsg_len = CMSG_LEN(sizeof(int));
    messageHeader->cmsg_level = SOL_SOCKET;
    messageHeader->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(messageHeader)) = fdToSend;

    message.msg_name = NULL;
    message.msg_namelen = 0;

    struct iovec iov[1];
    iov[0].iov_base = buffer;
    iov[0].iov_len = bufferSize;
    message.msg_iov = iov;
    message.msg_iovlen = 1;

    ssize_t rc = sendmsg(connectionFd, &message, 0);
    while (rc == -1 && errno == EINTR) {
        rc = sendmsg(connectionFd, &message, 0);
    }
    return rc;
}

int iTermSelect(int *fds, int count, int *results) {
    int result;
    int theError;
    fd_set readset;
    do {
        FD_ZERO(&readset);
        int max = 0;
        for (int i = 0; i < count; i++) {
            if (fds[i] > max) {
                max = fds[i];
            }
            FD_SET(fds[i], &readset);
        }
        FDLog(LOG_DEBUG, "Calling select...");
        result = select(max + 1, &readset, NULL, NULL, NULL);
        theError = errno;
        FDLog(LOG_DEBUG, "select returned %d, error = %s", result, strerror(theError));
    } while (result == -1 && theError == EINTR);

    int n = 0;
    for (int i = 0; i < count; i++) {
        results[i] = FD_ISSET(fds[i], &readset);
        if (results[i]) {
            n++;
        }
    }
    return n;
}

