/*
Copyright (c) 2016, Vyronas Tsingaras <vyronas@vtsingaras.me>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
* Neither the name of the <organization> nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
        ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
                                                                LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
        ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
        (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "ptydaemonizer.h"
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "arguments.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <string.h>

#ifndef WITH_SYSTEMD
#include <sys/syslog.h>
#else
#include <systemd/sd-daemon.h>
#endif

#ifndef WITH_SYSTEMD
#define log_notice(fmt, ...) \
    syslog(LOG_NOTICE, fmt, ##__VA_ARGS__);

#define log_error(fmt, ...) \
    syslog(LOG_ERR, fmt, ##__VA_ARGS__);

void initialize() {
    pid_t child;

    openlog(PROJECT_NAME , LOG_PID, LOG_DAEMON);

    if( (child=fork())<0 ) {
        exit(EXIT_FAILURE);
    }
    if (child > 0) {
        exit(EXIT_SUCCESS);
    }
    if( setsid() < 0 ) {
        exit(EXIT_FAILURE);
    }

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);

    if ( (child=fork())<0 ) {
        exit(EXIT_FAILURE);
    }
    if( child > 0 ) {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    chdir("/");

    int fd;
    for( fd=sysconf(_SC_OPEN_MAX); fd > 0; --fd ) {
        close(fd);
    }

    stdin=fopen("/dev/null","r");
    stdout=fopen("/dev/null","w+");
    stderr=fopen("/dev/null","w+");

    return;
}

void write_pidfile(char* path) {
    FILE* pidf;
    if( (pidf = fopen(path, "w+")) != NULL ) {
        fprintf(pidf, "%d", getpid());
        fclose(pidf);
    }else {
        log_error("Error opening pidfile: %s", path);
        exit(EXIT_FAILURE);
    }
}

void ready() {
    log_notice("Child running.");
};
#else
#define log_notice(fmt, ...) \
    fprintf(stderr, SD_NOTICE fmt, ##__VA_ARGS__)

#define log_error(fmt, ...) \
    fprintf(stderr, SD_ERR fmt, ##__VA_ARGS__); \
    sd_notifyf(1, "STATUS=ptydaemonizer fatal error: %s\nERRNO=%i", strerror(errno), errno)

void initialize() {
    sd_notifyf(0, "STATUS=Not much to say here");
}

void ready() {
    sd_notifyf(0, "STATUS=Child running.\nREADY=1");
};
#endif

int main(int argc, char** argv) {
    int pty_master_fd, pty_slave_fd;
    pid_t child;
    struct arguments arguments;
#ifndef WITH_SYSTEMD
    arguments.pidfile = NULL;
#endif

    argp_parse (&argp, argc, argv, 0, 0, &arguments);
    initialize();
    log_notice("ptydaemonizer fired up! pid: %d\n", getpid());
#ifndef WITH_SYSTEMD
    write_pidfile(arguments.pidfile);
#endif
    if( (pty_master_fd = posix_openpt(O_RDWR)) < 0) {
        log_error("Error allocating PTY device!");
        exit(EXIT_FAILURE);
    }
    if( grantpt(pty_master_fd) != 0 ) {
        log_error("Can't access PTY device");
        exit(EXIT_FAILURE);
    }
    if( unlockpt(pty_master_fd) != 0 ) {
        log_error("Error unlocking slave PTY");
        exit(EXIT_FAILURE);
    }
    pty_slave_fd = open(ptsname(pty_master_fd), O_RDWR);

    if( (child = fork()) < 0) {
        log_error("Can't spawn child daemon");
        exit(EXIT_FAILURE);
    }
    if( child > 0 ) {
        ready();
        //In ptydaemonizer
        fd_set pty_fds;
        //Close slave side as we are in father
        close(pty_slave_fd);

        while(1) {
            //Wait for data on child daemon's stdout
            FD_ZERO(&pty_fds);
            FD_SET(pty_master_fd, &pty_fds);
            char child_buf[512];
            int fds_ready;
            fds_ready = select(pty_master_fd + 1, &pty_fds, NULL, NULL, NULL);
            switch(fds_ready) {
                case -1:
                    log_error("ptydaemonizer error polling slave fds");
                    exit(EXIT_FAILURE);
                default:
                    if( FD_ISSET(pty_master_fd, &pty_fds) ) {
                        int bytes_read;
                        bytes_read = read(pty_master_fd, child_buf, sizeof(child_buf) - 1);
                        child_buf[sizeof(child_buf) - 1] = '\0';
                        if (bytes_read > 0) {
                            log_notice("%s", child_buf);
                        }else {
                            if( bytes_read < 0 ) {
                                log_error("ptydaemonizer error reading from child");
                                exit(EXIT_FAILURE);
                            }
                        }
                    }
            }
        }
    } else {
        // In child daemon
        struct termios child_term_settings;

        close(pty_master_fd);

        tcgetattr(pty_slave_fd, &child_term_settings);
        child_term_settings.c_lflag |= ICANON;
        tcsetattr(pty_slave_fd, TCSANOW, &child_term_settings);

        close(0);
        close(1);
        close(2);

        dup2(pty_slave_fd, 0);
        dup2(pty_slave_fd, 1);
        dup2(pty_slave_fd, 2);

        //become daemon
        close(pty_slave_fd);
        setsid();
        ioctl(0, TIOCSCTTY, 1);

        //build child command line
        execvp(arguments.command[0], arguments.command);
    }

    return 0;
}