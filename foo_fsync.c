#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "foo_main.h"

static int fd;

static void sigio_handler(int sig, siginfo_t *siginfo, void *context) {
  fprintf(stderr, "Received SIGIO signal\n");

  char buf[64];

  ssize_t n = read(fd, buf, sizeof(buf));
  if (n > 0) {
    // Signal  send by POLL_IN
    fprintf(stderr, "Read input data: ");
    for (size_t i = 0; i < n; i++) {
      fprintf(stderr, "%02x", buf[i]);
    }
    fprintf(stderr, "\n");
  } else {
    // Signal  send by POLL_OUT
    fprintf(stderr, "No input data available\n");
  }
}

int main(int argc, char **argv) {

  struct sigaction sig_act;

  memset(&sig_act, 0, sizeof(sig_act));

  fd = open("/dev/foo0", O_RDONLY);

  if (fd < 0) {
    perror("open");
    return 1;
  }
  /* Use the sa_sigaction field because the handles has two additional
   * parameters */
  sig_act.sa_sigaction = &sigio_handler;

  /* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not
   * sa_handler. */
  sig_act.sa_flags = SA_SIGINFO;

  if (sigaction(SIGIO, &sig_act, NULL) < 0) {
    perror("sigaction");
    return 1;
  }

  fcntl(fd, F_SETOWN, getpid());
  int oflags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, oflags | O_NONBLOCK | FASYNC);

  while (1) {
    sleep(10);
  }

  return 0;

  close(fd);
}
