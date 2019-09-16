#ifndef _FOO_MAIN_H

#define _FOO_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FOO_MODULE_NAME
#define FOO_MODULE_NAME "Foo"
#endif

#ifndef FOO_DEVICE_NAME
#define FOO_DEVICE_NAME "foo"
#endif

#ifndef FOO_DEV_RD_BUF_LEN
#define FOO_DEV_RD_BUF_LEN 1024
#endif

#ifndef FOO_DEV_WR_BUF_LEN
#define FOO_DEV_WR_BUF_LEN 1024
#endif

#ifndef FOO_DEVICE_COUNT
#define FOO_DEVICE_COUNT 4
#endif

/*
 * Ioctl definitions
 */

/* Use 'q' as magic number */
#define FOO_IOC_MAGIC 'q'

#define FOO_IOCRESET _IO(FOO_IOC_MAGIC, 0)

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */

/**Get device open count*/
#define FOO_IOC_GOPENCOUNT _IOR(FOO_IOC_MAGIC, 1, int)

/**Get device read buffer capacity*/
#define FOO_IOC_GRDCAP _IOR(FOO_IOC_MAGIC, 2, int)

/**Set device read buffer capacity*/
#define FOO_IOC_SRDCAP _IOW(FOO_IOC_MAGIC, 3, int)

/**Get device write buffer capacity*/
#define FOO_IOC_GWRCAP _IOR(FOO_IOC_MAGIC, 4, int)

/**Set device write buffer capacity*/
#define FOO_IOC_SWRCAP _IOW(FOO_IOC_MAGIC, 5, int)

/**Get device read buffer size*/
#define FOO_IOC_GRDSIZE _IOR(FOO_IOC_MAGIC, 6, int)

/**Get device write buffer size*/
#define FOO_IOC_GWRSIZE _IOR(FOO_IOC_MAGIC, 7, int)

#define FOO_IOC_MAXNR 7

#ifdef __cplusplus
}
#endif

#endif /*_FOO_MAIN_H */