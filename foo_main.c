#include <generated/utsrelease.h>

#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h> /* kmalloc() */

#include <asm/atomic.h>
#include <asm/current.h>
#include <asm/uaccess.h>

#include "foo_main.h"
#include "utils/ringbuffer.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ahmed Zamouche");
MODULE_DESCRIPTION(FOO_MODULE_NAME " Linux kernel module.");
MODULE_VERSION("0.01");

#ifndef SET_MODULE_OWNER
#define SET_MODULE_OWNER(dev) ((dev)->owner = THIS_MODULE)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef FOO_DEBUG
#define PRINTK(...) printk(__VA_ARGS__)
#else
#define PRINTK(...)
#endif
/* Prototypes for device functions */
static loff_t foo_llseek(struct file *, loff_t, int);
static ssize_t foo_read(struct file *, char *, size_t, loff_t *);
static ssize_t foo_write(struct file *, const char *, size_t, loff_t *);
int foo_fsync(struct file *, loff_t, loff_t, int datasync);
static long foo_unlocked_ioctl(struct file *, unsigned int, unsigned long);
unsigned int foo_poll(struct file *filp, poll_table *wait);
int foo_fasync(int, struct file *, int);
static int foo_open(struct inode *, struct file *);
static int foo_release(struct inode *, struct file *);

static const char *const FOO_MOD_NAME = FOO_MODULE_NAME;
static const char *const FOO_DEV_NAME = FOO_DEVICE_NAME;

typedef struct foo_dev_s {

  dev_t dev;

  struct cdev cdev;

  bool cdev_init;

  size_t open_count;

#ifdef FOO_DEV_SINGLE_OPEN
  atomic_t is_available;
#endif

#ifdef FOO_DEV_EXCLUSIVE_OPEN
  kuid_t uid;
#endif

  uint8_t *rd_buffer;
  uint8_t *wr_buffer;

  ringbuffer_t rb_rd;
  ringbuffer_t rb_wr;

  struct mutex mutex;           /* mutual exclusion mutexaphore */
  wait_queue_head_t q_rd, q_wr; /* read and write queues */

  struct fasync_struct *q_async; /* queue asynchronous readers */

} foo_dev_t;

static foo_dev_t foo_dev[FOO_DEVICE_COUNT];

#ifdef FOO_DEBUG
EXPORT_SYMBOL(foo_dev);
#endif

/*Prototypes for foo dev functions*/
static int foo_dev_write_buffer_realloc(foo_dev_t *foo_dev, size_t wrsize);
static int foo_dev_read_buffer_realloc(foo_dev_t *foo_dev, size_t rdsize);

/* This structure points to all of the device functions
@warning: Unsupported operations must be set to NULL*/
static struct file_operations s_foo_fops = {.owner = THIS_MODULE,
                                            .llseek = foo_llseek,
                                            .read = foo_read,
                                            .write = foo_write,
                                            .fsync = foo_fsync,
                                            .unlocked_ioctl =
                                                foo_unlocked_ioctl,
                                            .poll = foo_poll,
                                            .fasync = foo_fasync,
                                            .open = foo_open,
                                            .release = foo_release};

/**/
static loff_t foo_llseek(struct file *file, loff_t offset, int whence) {

  loff_t newpos;

  foo_dev_t *foo_dev = file->private_data;
  (void)foo_dev;

  printk(KERN_DEBUG
         "%s device pid=%d llseek(file=%p, offset=%llx, whence=%x)\n",
         FOO_DEV_NAME, current->pid, file, offset, whence);

  switch (whence) {
  case SEEK_SET:
  case SEEK_CUR:
  case SEEK_END:
  case SEEK_DATA:
  case SEEK_HOLE:
    newpos = -ESPIPE;
    break;
  default:
    newpos = -EINVAL;
    break;
  }

  return newpos;
}

/* When a process reads from our device, this gets called. */
static ssize_t foo_read(struct file *file, char *buffer, size_t len,
                        loff_t *offset) {
  int bytes_read = 0;
  ringbuffer_t *rb_rd = NULL;
  foo_dev_t *foo_dev = file->private_data;

  printk(KERN_DEBUG
         "%s device pid=%d read(file=%p, buffer=%p, len=%ld, offset=%p)\n",
         FOO_DEV_NAME, current->pid, file, buffer, len, offset);

  if (!access_ok(VERIFY_WRITE, (void __user *)buffer, len)) {
    return -EFAULT;
  }

  if (mutex_lock_interruptible(&foo_dev->mutex)) {
    return -ERESTARTSYS;
  }

#ifdef FOO_DEV_LOOPBACK
  rb_rd = &foo_dev->rb_wr;
#else
  rb_rd = &foo_dev->rb_rd;
#endif

  while (ringbuffer_is_empty(rb_rd)) {
    mutex_unlock(&foo_dev->mutex);

    if (file->f_flags & O_NONBLOCK) {
      return -EAGAIN;
    }

    if (wait_event_interruptible(foo_dev->q_rd, !ringbuffer_is_empty(rb_rd))) {
      return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
    }

    if (mutex_lock_interruptible(&foo_dev->mutex)) {
      return -ERESTARTSYS;
    }
  }

  /* Put data in the buffer */
  while (len) {
    uint8_t u8;
    if (ringbuffer_get(rb_rd, &u8) < 0) {
      break;
    }

    if (__put_user(u8, buffer++)) {
      bytes_read = -EFAULT;
      break;
    }

    len--;
    bytes_read++;
  }

#ifdef FOO_DEV_LOOPBACK
  /* finally, awake any reader */
  wake_up_interruptible(&foo_dev->q_wr); /* blocked in read() and select() */
  if (foo_dev->q_async) {
    kill_fasync(&foo_dev->q_async, SIGIO, POLL_OUT);
  }
#else
#error "Need to wake up 'writers' somehow!"
#endif
  mutex_unlock(&foo_dev->mutex);

  return bytes_read;
}

/* Called when a process tries to write to our device */
static ssize_t foo_write(struct file *file, const char *buffer, size_t len,
                         loff_t *offset) {

  int bytes_writen = 0;

  foo_dev_t *foo_dev = file->private_data;

  printk(KERN_DEBUG
         "%s device pid=%d write(file=%p, buffer=%p, len=%ld, offset=%p)\n",
         FOO_DEV_NAME, current->pid, file, buffer, len, offset);

  if (!access_ok(VERIFY_READ, (void __user *)buffer, len)) {
    return -EFAULT;
  }

  if (mutex_lock_interruptible(&foo_dev->mutex)) {
    return -ERESTARTSYS;
  }

  while (ringbuffer_is_full(&foo_dev->rb_wr)) {
    mutex_unlock(&foo_dev->mutex);

    /*@note: Never make a write call wait for data transmission before
     returning, even if O_NONBLOCK is clear*/
    if (file->f_flags & O_NONBLOCK) {
      return -EAGAIN;
    }

    if (wait_event_interruptible(foo_dev->q_wr,
                                 !ringbuffer_is_full(&foo_dev->rb_wr))) {
      return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
    }

    if (mutex_lock_interruptible(&foo_dev->mutex)) {
      return -ERESTARTSYS;
    }
  }

  /* Get data from the buffer */
  while (len) {

    uint8_t u8;
    if (__get_user(u8, buffer++)) {
      bytes_writen = -EFAULT;
      break;
    }

    if (ringbuffer_put(&foo_dev->rb_wr, u8) < 0) {
      break;
    }

    len--;
    bytes_writen++;
  }

  // bytes_writen = -ENOSPC;

  mutex_unlock(&foo_dev->mutex);

#ifdef FOO_DEV_LOOPBACK
  /* finally, awake any reader */
  wake_up_interruptible(&foo_dev->q_rd); /* blocked in read() and select() */
  /* and signal asynchronous readers, explained late in chapter 5 */
  if (foo_dev->q_async) {
    kill_fasync(&foo_dev->q_async, SIGIO, POLL_IN);
  }
#else
#error "Need to wake up 'sync|async readers' somehow!"
#endif
  return bytes_writen;
}

int foo_fsync(struct file *file, loff_t start, loff_t end, int datasync) {
  printk(KERN_DEBUG
         "%s device pid=%d fsync(file=%p, start=%llx, end=%llx, datasync=%d)\n",
         FOO_DEV_NAME, current->pid, file, start, end, datasync);
  return 0;
}

static long foo_unlocked_ioctl(struct file *file, unsigned int cmd,
                               unsigned long arg) {
  int err = 0, tmp;
  int retval = 0;
  foo_dev_t *foo_dev = file->private_data;

  printk(KERN_DEBUG "%s device pid=%d unlocked_ioctl(file=%p, cmd=%x)\n",
         FOO_DEV_NAME, current->pid, file, cmd);
  /*
   * extract the type and number bitfields, and don't decode
   * wrong cmds: return -ENOTTY (inappropriate ioctl) before access_ok()
   */

  if ((_IOC_TYPE(cmd) != FOO_IOC_MAGIC) || (_IOC_NR(cmd) > FOO_IOC_MAXNR)) {
    return -ENOTTY;
  }

  /*
   * the direction is a bitmask, and VERIFY_WRITE catches R/W
   * transfers. `Type' is user-oriented, while* access_ok is kernel-oriented, so
   * the concept of "read" and "write" is reversed
   */
  if (_IOC_DIR(cmd) & _IOC_READ) {
    err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
  } else if (_IOC_DIR(cmd) & _IOC_WRITE) {
    err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
  } else {
    err = 0;
  }

  if (err) {
    return -EFAULT;
  }
  if (mutex_lock_interruptible(&foo_dev->mutex)) {
    return -ERESTARTSYS;
  }

  switch (cmd) {
  case FOO_IOCRESET:
    if (!capable(CAP_SYS_ADMIN)) {
      retval = -EPERM;
      break;
    }
    ringbuffer_reset(&foo_dev->rb_rd);
    ringbuffer_reset(&foo_dev->rb_wr);
    retval = 0;
    break;
  case FOO_IOC_GOPENCOUNT:
    retval = __put_user(foo_dev->open_count, (int __user *)arg);
    break;
  case FOO_IOC_GRDCAP:
    retval = __put_user(foo_dev->rb_rd.capacity, (int __user *)arg);
    break;
  case FOO_IOC_SRDCAP:
    if (!capable(CAP_SYS_ADMIN)) {
      retval = -EPERM;
      break;
    }
    retval = __get_user(tmp, (int __user *)arg);
    if (retval) {
      break;
    }
    if (!tmp) {
      retval = -EINVAL;
      break;
    }
    retval = foo_dev_read_buffer_realloc(foo_dev, (size_t)tmp);
    if (retval) {
      break;
    }

    break;
  case FOO_IOC_GWRCAP:
    retval = __put_user(foo_dev->rb_wr.capacity, (int __user *)arg);
    break;
  case FOO_IOC_SWRCAP:
    if (!capable(CAP_SYS_ADMIN)) {
      retval = -EPERM;
      break;
    }
    retval = __get_user(tmp, (int __user *)arg);
    if (retval) {
      break;
    }
    if (!tmp) {
      retval = -EINVAL;
      break;
    }
    retval = foo_dev_write_buffer_realloc(foo_dev, (size_t)tmp);
    if (retval) {
      break;
    }
    break;
  case FOO_IOC_GRDSIZE:
    retval = __put_user(foo_dev->rb_rd.size, (int __user *)arg);
    break;
  case FOO_IOC_GWRSIZE:
    retval = __put_user(foo_dev->rb_wr.size, (int __user *)arg);
    break;
  default: /* redundant, as cmd was checked against MAXNR */
    retval = -ENOTTY;
    break;
  }

  mutex_unlock(&foo_dev->mutex);

  return retval;
}

unsigned int foo_poll(struct file *file, poll_table *wait) {

  foo_dev_t *foo_dev = file->private_data;

  unsigned int mask = 0;

  printk(KERN_DEBUG "%s device pid=%d poll(file=%p, poll_table=%p)\n",
         FOO_DEV_NAME, current->pid, file, wait);

  if (mutex_lock_interruptible(&foo_dev->mutex)) {
    // return -ERESTARTSYS;
    return 0;
  }

  poll_wait(file, &foo_dev->q_rd, wait);
  poll_wait(file, &foo_dev->q_wr, wait);

  if (!ringbuffer_is_full(&foo_dev->rb_wr)) {
    mask |= POLLIN | POLLRDNORM; /* readable */
  }

  if (!ringbuffer_is_full(&foo_dev->rb_wr)) {
    mask |= POLLOUT | POLLWRNORM; /* writable */
  }

  mutex_unlock(&foo_dev->mutex);

  return mask;
}

int foo_fasync(int fd, struct file *file, int mode) {

  foo_dev_t *foo_dev = file->private_data;

  return fasync_helper(fd, file, mode, &foo_dev->q_async);
}

/* Called when a process opens our device */
static int foo_open(struct inode *inode, struct file *file) {

  // int i = iminor(inode);

  foo_dev_t *foo_dev; /* device information */
  foo_dev = container_of(inode->i_cdev, foo_dev_t, cdev);

  printk(KERN_DEBUG "%s device pid=%d open(inode=%p, file=%p)\n", FOO_DEV_NAME,
         current->pid, inode, file);

  file->private_data = foo_dev;

  (void)nonseekable_open(inode, file);

#ifdef FOO_DEV_SINGLE_OPEN
  if (!atomic_dec_and_test(&foo_dev->is_available)) {
    atomic_inc(&foo_dev->is_available);
    return -EBUSY; /* already open */
  }
#endif

  if (mutex_lock_interruptible(&foo_dev->mutex)) {
    return -ERESTARTSYS;
  }

#ifdef FOO_DEV_EXCLUSIVE_OPEN
  if (foo_dev->open_count) {                             /*already opened*/
    if ((foo_dev->uid.val != current_cred()->uid.val) && /* allow user */
        (foo_dev->uid.val !=
         current_cred()->euid.val) && /* allow whoever did su */
        !capable(CAP_DAC_OVERRIDE)) { /* still allow root */
      mutex_unlock(&foo_dev->mutex);
      return -EBUSY; /* -EPERM would confuse the user */
    }
  } else {
    foo_dev->uid.val = current_cred()->uid.val; /* grab it */
  }
#endif

  foo_dev->open_count++;

  mutex_unlock(&foo_dev->mutex);

  try_module_get(THIS_MODULE);

  return 0;
}

/* Called when a process closes our device */
static int foo_release(struct inode *inode, struct file *file) {
  /* Decrement the open counter and usage count. Without this, the module would
   * not unload. */

  // int index = iminor(inode);
  foo_dev_t *foo_dev; /* device information */
  foo_dev = container_of(inode->i_cdev, foo_dev_t, cdev);

#ifdef FOO_DEV_SINGLE_OPEN
  atomic_inc(&foo_dev->is_available); /* release the device */
#endif

  printk(KERN_DEBUG "%s device pid=%d release(inode=%p, file=%p)\n",
         FOO_DEV_NAME, current->pid, inode, file);

  if (mutex_lock_interruptible(&foo_dev->mutex)) {
    return -ERESTARTSYS;
  }

  foo_dev->open_count--;

  mutex_unlock(&foo_dev->mutex);

  module_put(THIS_MODULE);

  return 0;
}

static int foo_dev_read_buffer_realloc(foo_dev_t *foo_dev, size_t rdsize) {

  uint8_t *buffer = krealloc(foo_dev->rd_buffer, rdsize, GFP_KERNEL);

  if (!buffer) {
    return -ENOMEM;
  }

  foo_dev->rd_buffer = buffer;

  ringbuffer_wrap(&foo_dev->rb_rd, foo_dev->rd_buffer, rdsize);

  return 0;
}

static int foo_dev_write_buffer_realloc(foo_dev_t *foo_dev, size_t wrsize) {

  uint8_t *buffer = krealloc(foo_dev->wr_buffer, wrsize, GFP_KERNEL);

  if (!buffer) {
    return -ENOMEM;
  }

  foo_dev->wr_buffer = buffer;

  ringbuffer_wrap(&foo_dev->rb_wr, foo_dev->wr_buffer, wrsize);

  return 0;
}

static void foo_deinit_cdev(foo_dev_t *foo_dev) {
  if (!foo_dev->cdev_init) {
    return;
  }
  cdev_del(&foo_dev->cdev);
}

static int foo_init_cdev(foo_dev_t *foo_dev) {

  int err = 0;

  foo_dev->cdev_init = false;

  cdev_init(&foo_dev->cdev, &s_foo_fops);

  foo_dev->cdev.owner = THIS_MODULE;
  foo_dev->cdev.ops = &s_foo_fops;

  err = cdev_add(&foo_dev->cdev, foo_dev->dev, 1);

  if (err) {
    printk(KERN_ERR "Error %d adding %s", err, FOO_DEV_NAME);
    return err;
  }

  foo_dev->cdev_init = true;

  return 0;
}

static void foo_deinit_dev(foo_dev_t *foo_dev) {
  foo_dev_read_buffer_realloc(foo_dev, 0);
  foo_dev_read_buffer_realloc(foo_dev, 0);
}

static int foo_init_dev(foo_dev_t *foo_dev) {

  int err = 0;

  mutex_init(&foo_dev->mutex);

  init_waitqueue_head(&foo_dev->q_rd);

  init_waitqueue_head(&foo_dev->q_wr);

#ifdef FOO_DEV_SINGLE_OPEN
  atomic_set(&foo_dev->is_available, 1);
#endif

  foo_dev->q_async = NULL;

  foo_dev->rd_buffer = NULL;

  foo_dev->wr_buffer = NULL;

  err = foo_dev_read_buffer_realloc(foo_dev, FOO_DEV_RD_BUF_LEN);
  if (err) {
    return err;
  }
  err = foo_dev_write_buffer_realloc(foo_dev, FOO_DEV_WR_BUF_LEN);
  if (err) {
    return err;
  }
  return 0;
}

#include <linux/workqueue.h>

typedef struct foo_taskqueue_s {
  struct workqueue_struct *workqueue;
  atomic_t enabled;
} foo_taskqueue_t;

typedef struct foo_task_s {
  struct delayed_work dwork;
  atomic_t data;
  /**/
  foo_taskqueue_t *taskqueue;
} foo_task_t;

static foo_taskqueue_t s_taskqueue;
static foo_task_t s_task;

static void foo_task_fn(struct work_struct *work) {

  struct delayed_work *dwork;
  foo_task_t *task;

  dwork = container_of(work, struct delayed_work, work);

  task = container_of(dwork, foo_task_t, dwork);

  printk(KERN_DEBUG
         "task.data  = %d, in_interrupt = %d, pid: %d, smp_pid: %d, com: %s\n",
         atomic_read(&task->data), in_interrupt() ? 1 : 0, current->pid,
         smp_processor_id(), current->comm);
  atomic_inc(&task->data);
}

static int foo_task_init(foo_task_t *task, foo_taskqueue_t *taskqueue) {

  INIT_DELAYED_WORK(&task->dwork, foo_task_fn);

  atomic_set(&task->data, 0);

  task->taskqueue = taskqueue;

  return 0;
}

static bool foo_task_start(foo_task_t *task, unsigned long delay) {
  bool enabled = false;

  if (atomic_dec_and_test(&task->taskqueue->enabled)) {
    enabled = true;
    queue_delayed_work(task->taskqueue->workqueue, &task->dwork, delay);
  }
  atomic_inc(&task->taskqueue->enabled);
  return enabled;
}

static inline bool foo_task_cancel(foo_task_t *task) {
  return cancel_delayed_work(&task->dwork);
}

static int foo_taskqueue_create(foo_taskqueue_t *taskqueue) {
  // create_workqueue(const char *name);
  taskqueue->workqueue = create_singlethread_workqueue("foo-workqueue");

  if (!taskqueue->workqueue) {
    printk(KERN_ERR "Failed to create a queue work\n");
    return -ENOMEM;
  }

  atomic_set(&taskqueue->enabled, 1);

  return 0;
}

static void foo_taskqueue_destroy(foo_taskqueue_t *taskqueue) {

  atomic_set(&taskqueue->enabled, 0);

  /*if (!cancel_delayed_work(dwork)) {

  }*/

  flush_workqueue(taskqueue->workqueue);

  destroy_workqueue(taskqueue->workqueue);
}

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/timex.h>

static struct timer_list foo_timer;

void foo_timer_fn(unsigned long arg) {

  printk(KERN_DEBUG
         "timer.data = %ld, in_interrupt = %d, pid: %d, smp_pid: %d, com: %s\n",
         arg, in_interrupt() ? 1 : 0, current->pid, smp_processor_id(),
         current->comm);

  foo_timer.data = arg + 1;
  foo_timer.expires = jiffies + 10 * HZ; /* parameter */

  add_timer(&foo_timer);
  foo_task_start(&s_task, 0 * HZ);
}

int foo_timer_init(struct timer_list *timer) {

  init_timer(timer);

  timer->data = 0;
  foo_timer.expires = jiffies + 10 * HZ; /* parameter */
  timer->function = foo_timer_fn;

  /* register the timer */

  add_timer(timer);

  return 0;
}
void foo_timer_deinit(struct timer_list *timer) {
  // del_timer(timer);
  del_timer_sync(timer);
}

static void foo_delay(unsigned long ms) {
  unsigned long timeout = (ms * HZ) / 1000;
#if 0
  unsigned long j = jiffies + timeout;
  while (time_before(jiffies, j)) {
#if 0    
    cpu_relax();
#else
    schedule();
#endif
  }
#else
  set_current_state(TASK_INTERRUPTIBLE);
  schedule_timeout(timeout);
#endif
}

static void foo_jiffies(void) {

  unsigned long j = 1000;
  unsigned long start, end;
  long overhead;

  start = get_cycles();
  end = get_cycles();

  overhead = end - start;
  printk(KERN_DEBUG "get_cycles overhead: %li\n", overhead);

  start = get_cycles();
  foo_delay(j); /* read the current value */
  end = get_cycles();

  printk(KERN_DEBUG "delay(%ld ms) time lapse: %li\n", j,
         (end - start) - overhead);
}

static int __init foo_init(void) {

  int err = 0;
  size_t i, j;

  printk(KERN_INFO "Initializing %s kernel-%s module\n", FOO_MOD_NAME,
         UTS_RELEASE);

  foo_jiffies();

  foo_timer_init(&foo_timer);

  foo_taskqueue_create(&s_taskqueue);
  foo_task_init(&s_task, &s_taskqueue);

  SET_MODULE_OWNER(&s_foo_fops);

  err = alloc_chrdev_region(&foo_dev[0].dev, 0, ARRAY_SIZE(foo_dev),
                            FOO_DEV_NAME);
  if (err < 0) {
    printk(KERN_ERR "%s: can't get major %d\n", FOO_DEV_NAME,
           MAJOR(foo_dev[0].dev));
    return err;
  }

  for (i = 0; i < ARRAY_SIZE(foo_dev); i++) {

    err = foo_init_dev(&foo_dev[i]);
    if (err) {
      break;
    }

    foo_dev[i].dev = MKDEV(MAJOR(foo_dev[0].dev), i);

    err = foo_init_cdev(&foo_dev[i]);

    if (err) {
      break;
    }
  }

  if (err) {
    for (j = 0; j < ARRAY_SIZE(foo_dev); j++) {
      //@warning: deinittialation order matter
      foo_deinit_cdev(&foo_dev[j]);
      foo_deinit_dev(&foo_dev[j]);
    }
    unregister_chrdev_region(foo_dev[0].dev, ARRAY_SIZE(foo_dev));
    return err;
  }

  printk(KERN_INFO "%s module loaded with device major number %d\n",
         FOO_MOD_NAME, MAJOR(foo_dev[0].dev));

  printk(KERN_INFO "Initialized %s kernel-%s module\n", FOO_MOD_NAME,
         UTS_RELEASE);
  return 0;
}

static void __exit foo_exit(void) {

  size_t i;

  /* Remember — we have to clean up after ourselves. Unregister the character
   * device. */
  printk(KERN_INFO "Exiting %s kernel-%s module\n", FOO_MOD_NAME, UTS_RELEASE);

  foo_timer_deinit(&foo_timer);

  foo_task_cancel(&s_task);

  foo_taskqueue_destroy(&s_taskqueue);

  for (i = 0; i < ARRAY_SIZE(foo_dev); i++) {
    //@warning: deinittialation order matter
    foo_deinit_cdev(&foo_dev[i]);
    foo_deinit_dev(&foo_dev[i]);
  }

  unregister_chrdev_region(foo_dev[0].dev, ARRAY_SIZE(foo_dev));

  printk(KERN_INFO "Exited %s kernel-%s module\n", FOO_MOD_NAME, UTS_RELEASE);
}

module_init(foo_init);
module_exit(foo_exit);
