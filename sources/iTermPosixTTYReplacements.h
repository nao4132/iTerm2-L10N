//
//  iTermPosixTTYReplacements.h
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#include <limits.h>
#include <termios.h>

extern const int kNumFileDescriptorsToDup;

typedef struct {
    struct termios term;
    struct winsize win;
    char tty[PATH_MAX];
} iTermTTYState;

typedef struct {
    pid_t pid;
    int connectionFd;
    int deadMansPipe[2];
    int numFileDescriptorsToPreserve;
} iTermForkState;

void iTermTTYStateInitialize(iTermTTYState *ttyState,
                             int width,
                             int height,
                             int isUTF8);

// Just like forkpty but fd 0 the master and fd 1 the slave.
int iTermPosixTTYReplacementForkPty(int *amaster,
                                    iTermTTYState *ttyState,
                                    int serverSocketFd,
                                    int deadMansPipeWriteEnd);

// Call this in the child after fork.
void iTermExec(const char *argpath,
               const char **argv,
               int closeFileDescriptors,
               const iTermForkState *forkState,
               const char *initialPwd,
               char **newEnviron);

void iTermSignalSafeWrite(int fd, const char *message);
void iTermSignalSafeWriteInt(int fd, int n);
