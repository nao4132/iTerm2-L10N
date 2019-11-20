//
//  iTermPosixTTYReplacements.h
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#import "iTermTTYState.h"

#include <limits.h>
#include <termios.h>

extern const int kNumFileDescriptorsToDup;

typedef struct {
    pid_t pid;
    int connectionFd;
    int deadMansPipe[2];
    int numFileDescriptorsToPreserve;
} iTermForkState;

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

// `orig` is an array of ints with file descriptor numbers that we wish to preserve. The first one
// gets remapped to fd 0, the second one to fd 1, etc. `count` gives the length of the array.
void iTermPosixMoveFileDescriptors(int *orig, int count);
