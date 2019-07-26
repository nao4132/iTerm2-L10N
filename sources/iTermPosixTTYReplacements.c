//
//  iTermPosixTTYReplacements.c
//  iTerm2
//
//  Created by George Nachman on 7/25/19.
//

#include "iTermPosixTTYReplacements.h"
#import "iTermFileDescriptorServer.h"
#import "iTermResourceLimitsHelper.h"
#import "shell_launcher.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <util.h>

#define CTRLKEY(c) ((c)-'A'+1)

const int kNumFileDescriptorsToDup = NUM_FILE_DESCRIPTORS_TO_PASS_TO_SERVER;

// Like login_tty but makes fd 0 the master, fd 1 the slave, fd 2 an open unix-domain socket
// for transferring file descriptors, and fd 3 the write end of a pipe that closes when the server
// dies.
// IMPORTANT: This runs between fork and exec. Careful what you do.
static void iTermPosixTTYReplacementLoginTTY(int master,
                                             int slave,
                                             int serverSocketFd,
                                             int deadMansPipeWriteEnd) {
    setsid();
    ioctl(slave, TIOCSCTTY, NULL);

    // This array keeps track of which file descriptors are in use and should not be dup2()ed over.
    // It has |inuseCount| valid elements. inuse must have inuseCount + arraycount(orig) elements.
    int inuse[3 * NUM_FILE_DESCRIPTORS_TO_PASS_TO_SERVER] = {
        0, 1, 2, 3,  // FDs get duped to the lowest numbers so reserve them
        master, slave, serverSocketFd, deadMansPipeWriteEnd,  // FDs to get duped, which mustn't be overwritten
        -1, -1, -1, -1 };  // Space for temp values to ensure they don't get reused
    int inuseCount = 2 * kNumFileDescriptorsToDup;

    // File descriptors get dup2()ed to temporary numbers first to avoid stepping on each other or
    // on any of the desired final values. Their temporary values go in here. The first is always
    // master, then slave, then server socket.
    int temp[NUM_FILE_DESCRIPTORS_TO_PASS_TO_SERVER];

    // The original file descriptors to renumber.
    int orig[NUM_FILE_DESCRIPTORS_TO_PASS_TO_SERVER] = { master, slave, serverSocketFd, deadMansPipeWriteEnd };

    for (int o = 0; o < sizeof(orig) / sizeof(*orig); o++) {  // iterate over orig
        int original = orig[o];

        // Try to find a candidate file descriptor that is not important to us (i.e., does not belong
        // to the inuse array).
        for (int candidate = 0; candidate < sizeof(inuse) / sizeof(*inuse); candidate++) {
            int isInUse = 0;
            for (int i = 0; i < sizeof(inuse) / sizeof(*inuse); i++) {
                if (inuse[i] == candidate) {
                    isInUse = 1;
                    break;
                }
            }
            if (!isInUse) {
                // t is good. dup orig[o] to t and close orig[o]. Save t in temp[o].
                inuse[inuseCount++] = candidate;
                temp[o] = candidate;
                dup2(original, candidate);
                close(original);
                break;
            }
        }
    }

    // Dup the temp values to their desired values (which happens to equal the index in temp).
    // Close the temp file descriptors.
    for (int i = 0; i < sizeof(orig) / sizeof(*orig); i++) {
        dup2(temp[i], i);
        close(temp[i]);
    }
}

int iTermPosixTTYReplacementForkPty(int *amaster,
                                    iTermTTYState *ttyState,
                                    int serverSocketFd,
                                    int deadMansPipeWriteEnd) {
    int master;
    int slave;

    iTermFileDescriptorServerLog("Calling openpty");
    if (openpty(&master, &slave, ttyState->tty, &ttyState->term, &ttyState->win) == -1) {
        iTermFileDescriptorServerLog("openpty failed: %s", strerror(errno));
        return -1;
    }

    iTermFileDescriptorServerLog("Calling fork");
    pid_t pid = fork();
    switch (pid) {
        case -1:
            // error
            iTermFileDescriptorServerLog("Fork failed: %s", strerror(errno));
            return -1;

        case 0:
            // child
            iTermPosixTTYReplacementLoginTTY(master, slave, serverSocketFd, deadMansPipeWriteEnd);
            return 0;

        default:
            // parent
            *amaster = master;
            close(slave);
            close(serverSocketFd);
            close(deadMansPipeWriteEnd);
            return pid;
    }
}

void iTermTTYStateInitialize(iTermTTYState *ttyState,
                             int width,
                             int height,
                             int isUTF8) {
    struct termios *term = &ttyState->term;
    struct winsize *win = &ttyState->win;

    memset(term, 0, sizeof(struct termios));
    memset(win, 0, sizeof(struct winsize));

    // UTF-8 input will be added on demand.
    term->c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT | (isUTF8 ? IUTF8 : 0);
    term->c_oflag = OPOST | ONLCR;
    term->c_cflag = CREAD | CS8 | HUPCL;
    term->c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;

    term->c_cc[VEOF] = CTRLKEY('D');
    term->c_cc[VEOL] = -1;
    term->c_cc[VEOL2] = -1;
    term->c_cc[VERASE] = 0x7f;           // DEL
    term->c_cc[VWERASE] = CTRLKEY('W');
    term->c_cc[VKILL] = CTRLKEY('U');
    term->c_cc[VREPRINT] = CTRLKEY('R');
    term->c_cc[VINTR] = CTRLKEY('C');
    term->c_cc[VQUIT] = 0x1c;           // Control+backslash
    term->c_cc[VSUSP] = CTRLKEY('Z');
    term->c_cc[VDSUSP] = CTRLKEY('Y');
    term->c_cc[VSTART] = CTRLKEY('Q');
    term->c_cc[VSTOP] = CTRLKEY('S');
    term->c_cc[VLNEXT] = CTRLKEY('V');
    term->c_cc[VDISCARD] = CTRLKEY('O');
    term->c_cc[VMIN] = 1;
    term->c_cc[VTIME] = 0;
    term->c_cc[VSTATUS] = CTRLKEY('T');

    term->c_ispeed = B38400;
    term->c_ospeed = B38400;

    win->ws_row = height;
    win->ws_col = width;
    win->ws_xpixel = 0;
    win->ws_ypixel = 0;
}

void iTermExec(const char *argpath,
               const char **argv,
               int closeFileDescriptors,
               const iTermForkState *forkState,
               const char *initialPwd,
               char **newEnviron) {
    // BE CAREFUL WHAT YOU DO HERE!
    // See man sigaction for the list of legal function calls to make between fork and exec.

    // Do not start the new process with a signal handler.
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    sigprocmask(SIG_UNBLOCK, &signals, NULL);

    // Apple opens files without the close-on-exec flag (e.g., Extras2.rsrc).
    // See issue 2662.
    if (closeFileDescriptors) {
        // If running jobs in servers close file descriptors after exec when it's safe to
        // enumerate files in /dev/fd. This is the potentially very slow path (issue 5391).
        for (int j = forkState->numFileDescriptorsToPreserve; j < getdtablesize(); j++) {
            close(j);
        }
    }

    // setrlimit is *not* documented as being safe to use between fork and exec, but I believe it to
    // be safe nonetheless. The implementation is simply to make a system call. Neither memory
    // allocation nor mutex locking occurs in user space. There isn't any other way to do this besides
    // passing the desired limits to the child process, which is pretty gross.
    iTermResourceLimitsHelperRestoreSavedLimits();

    chdir(initialPwd);

    // Sub in our environ for the existing one. Since Mac OS doesn't have execvpe, this hack
    // does the job.
    extern char **environ;
    environ = newEnviron;
    execvp(argpath, (char* const*)argv);

    // NOTE: This won't be visible when jobs run in servers :(
    // exec error
    int e = errno;
    iTermSignalSafeWrite(1, "## exec failed ##\n");
    iTermSignalSafeWrite(1, "Program: ");
    iTermSignalSafeWrite(1, argpath);
    iTermSignalSafeWrite(1, "\nErrno: ");
    if (e == ENOENT) {
        iTermSignalSafeWrite(1, "\nNo such file or directory");
    } else {
        iTermSignalSafeWrite(1, "\nErrno: ");
        iTermSignalSafeWriteInt(1, e);
    }
    iTermSignalSafeWrite(1, "\n");

    sleep(1);
}

void iTermSignalSafeWrite(int fd, const char *message) {
    int len = 0;
    for (int i = 0; message[i]; i++) {
        len++;
    }
    ssize_t rc;
    do {
        rc = write(fd, message, len);
    } while (rc < 0 && (errno == EAGAIN || errno == EINTR));
}

void iTermSignalSafeWriteInt(int fd, int n) {
    if (n == INT_MIN) {
        iTermSignalSafeWrite(fd, "int_min");
        return;
    }
    if (n < 0) {
        iTermSignalSafeWrite(fd, "-");
        n = -n;
    }
    if (n < 10) {
        char str[2] = { n + '0', 0 };
        iTermSignalSafeWrite(fd, str);
        return;
    }
    iTermSignalSafeWriteInt(fd, n / 10);
    iTermSignalSafeWriteInt(fd, n % 10);
}


