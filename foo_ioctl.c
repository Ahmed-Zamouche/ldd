#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>


#include "foo_main.h"

int main(int argc, char **argv) {
  int fd, status;

  fd = open("/dev/foo0", O_RDONLY);

  if (ioctl(fd, FOO_IOC_GOPENCOUNT, &status) < 0) {
    printf("FOO_IOC_GOPENCOUNT failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_GOPENCOUNT get: %d\n", status);
  }

  if (ioctl(fd, FOO_IOC_GRDCAP, &status) < 0) {
    printf("FOO_IOC_GRDCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_GRDCAP get: %d\n", status);
  }
  
  status =  2048;
  if (ioctl(fd, FOO_IOC_SRDCAP, &status) < 0) {
    printf("FOO_IOC_SRDCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_SRDCAP set: %d\n", status);
  }

  if (ioctl(fd, FOO_IOC_GRDCAP, &status) < 0) {
    printf("FOO_IOC_GRDCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_GRDCAP get: %d\n", status);
  }

  status =  0;
  if (ioctl(fd, FOO_IOC_SRDCAP, &status) < 0) {
    printf("FOO_IOC_SRDCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_SRDCAP set: %d\n", status);
  }
  
  status =  128;
  if (ioctl(fd, FOO_IOC_SRDCAP, &status) < 0) {
    printf("FOO_IOC_SRDCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_SRDCAP set: %d\n", status);
  }

  if (ioctl(fd, FOO_IOC_GRDCAP, &status) < 0) {
    printf("FOO_IOC_GRDCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_GRDCAP get: %d\n", status);
  }

  if (ioctl(fd, FOO_IOC_GWRCAP, &status) < 0) {
    printf("FOO_IOC_GWRCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_GWRCAP get: %d\n", status);
  }

  status =  128;
  if (ioctl(fd, FOO_IOC_SWRCAP, &status) < 0) {
    printf("FOO_IOC_SWRCAP failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_SWRCAP set: %d\n", status);
  }

  if (ioctl(fd, FOO_IOC_GRDSIZE, &status) < 0) {
    printf("FOO_IOC_GRDSIZE failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_GRDSIZE get: %d\n", status);
  }

  if (ioctl(fd, FOO_IOC_GWRSIZE, &status) < 0) {
    printf("FOO_IOC_GWRSIZE failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOC_GWRSIZE get: %d\n", status);
  }

  if (ioctl(fd, FOO_IOCRESET, &status) < 0) {
    printf("FOO_IOCRESET failed: %s\n", strerror(errno));
  } else {
    printf("FOO_IOCRESET OK\n");
  }


  close(fd);
}
