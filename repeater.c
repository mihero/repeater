/*
 * Linux 2.6 and 3.0 'repeater' sample device driver
 *
 * Copyright (c) 2013, Mikko Rosten (mikko.rosten@iki.fi)
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include "repeater.h"


/* Device variables */
static struct class* repeater_class = NULL;
static struct device* repeater_device = NULL;
static int repeater_major;
/* Module parameters that can be provided on insmod */
static bool debug = false; /* print extra debug info */
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "enable debug info (default: false)");
static bool one_shot = false; /* data is returned as one record at time */
module_param(one_shot, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(one_shot, "return data one record at time");
static int records_read = 0;
/* A mutex will ensure that only one process accesses our device */
static DEFINE_MUTEX(repeater_device_mutex);
/* Use a Kernel FIFO for read operations */
static DECLARE_KFIFO(repeater_msg_fifo, char, REPEATER_MSG_FIFO_SIZE);
static DECLARE_KFIFO(repeater_msg_size_fifo, long, REPEATER_MSG_FIFO_SIZE);




int repeater_open(struct inode *inode, struct file *filp);
int repeater_close(struct inode *inode, struct file *filp);
ssize_t repeater_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t repeater_write(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);

int repeater_open(struct inode *inode, struct file *filp){
   dbg("");
  /* Our sample device does not allow readwrite access */
   if ((filp->f_flags & O_ACCMODE) == O_RDWR ) {
    warn("write access is prohibited\n");
    return -EACCES;
   }
   //create counter for one_shot
   filp->private_data=kmalloc(sizeof(int), GFP_KERNEL);
   *(int*)(filp->private_data)=0;
   return 0;
}

int repeater_close(struct inode *inode, struct file *filp){
   dbg("");
   
   //release one_shot counter
   kfree(filp->private_data);
   return 0;
}

ssize_t repeater_read(struct file *filp, char *buf, size_t count, loff_t *f_pos){
   dbg("");
   long size;
   unsigned int copied;
   int ret;
   mutex_lock(&repeater_device_mutex);
   if (one_shot && *(int *)(filp->private_data)>0){
      dbg("one_shot is used");
      mutex_unlock(&repeater_device_mutex);
      return 0;
   }
   int * counter=(int *)(filp->private_data);
   (*counter)++;
   if (kfifo_is_empty(&repeater_msg_size_fifo)){
      dbg("no messages");
      mutex_unlock(&repeater_device_mutex);
      return 0;
   }
   
   ret = kfifo_get(&repeater_msg_size_fifo, &size);
   dbg("message size %d length %d", size, ret);
   if (ret != 1 && size<=0){
      err("failed to get message length");
      mutex_unlock(&repeater_device_mutex);
      return -EIO;
   }
   ret = kfifo_to_user(&repeater_msg_fifo, buf, size, &copied);
   if (copied!=size){
      warn("failed to copy full size");
   }
   mutex_unlock(&repeater_device_mutex);
   return ret ? ret : copied;
}

ssize_t repeater_write(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){
   dbg("");
   unsigned int copied;
   int retval;
   mutex_lock(&repeater_device_mutex);
   if (kfifo_is_full(&repeater_msg_fifo) || kfifo_is_full(&repeater_msg_size_fifo)){
      warn("kfifo is full");
      mutex_unlock(&repeater_device_mutex);
      return -ENOMEM;
   }
   
   retval = kfifo_from_user(&repeater_msg_fifo, buf, count, &copied);
   dbg("fifo size is %d and len is %d", kfifo_size(&repeater_msg_fifo),kfifo_len(&repeater_msg_fifo));
   if (retval!=0 || copied==0){
      warn("buffer write failed");
      mutex_unlock(&repeater_device_mutex);
      return retval;
   }
   retval = kfifo_put(&repeater_msg_size_fifo, &copied);
   if (retval==0){
      err("len buffer full");
      kfifo_skip(&repeater_msg_fifo);
      mutex_unlock(&repeater_device_mutex);
      return -EIO;
   }
   mutex_unlock(&repeater_device_mutex);
   return copied;
}



/* The file_operation scructure tells the kernel which device operations are handled.
 * For a list of available file operations, see http://lwn.net/images/pdf/LDD3/ch03.pdf */
static struct file_operations fops = {
   .read = repeater_read,
   .write = repeater_write,
   .open = repeater_open,
   .release = repeater_close
};

/* This sysfs entry resets the FIFO */
static ssize_t sys_reset(struct device* dev, struct device_attribute* attr, const char* buf, size_t count)
{
   dbg("");
   mutex_lock(&repeater_device_mutex);
   /* Reset both fifos 
   */
   kfifo_reset(&repeater_msg_fifo);
   kfifo_reset(&repeater_msg_size_fifo);
   mutex_unlock(&repeater_device_mutex);

   return count;
}
static DEVICE_ATTR(reset, S_IWUSR, NULL, sys_reset);

static int __init repeater_init(void) {
   dbg("");
   int retval;
   /* First, see if we can dynamically allocate a major for our device */
   repeater_major = register_chrdev(0, DEVICE_NAME, &fops);
   if (repeater_major < 0) {
    err("failed to register device: error %d\n", repeater_major);
    retval = repeater_major;
    goto failed_chrdevreg;
   }

   /* We can either tie our device to a bus (existing, or one that we create)
    * or use a "virtual" device class. For this example, we choose the latter */
   repeater_class = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(repeater_class)) {
    err("failed to register device class '%s'\n", CLASS_NAME);
    retval = PTR_ERR(repeater_class);
    goto failed_classreg;
   }

   /* With a class, the easiest way to instantiate a device is to call device_create() */
   repeater_device = device_create(repeater_class, NULL, MKDEV(repeater_major, 0), NULL, CLASS_NAME "_" DEVICE_NAME);
   if (IS_ERR(repeater_device)) {
    err("failed to create device '%s_%s'\n", CLASS_NAME, DEVICE_NAME);
    retval = PTR_ERR(repeater_device);
    goto failed_devreg;
   }

   /* Now we can create the sysfs endpoints (don't care about errors).
    * dev_attr_fifo and dev_attr_reset come from the DEVICE_ATTR(...) earlier */
   //retval = device_create_file(repeater_device, &dev_attr_fifo);
   //if (retval < 0) {
   // warn("failed to create write /sys endpoint - continuing without\n");
   //}
   retval = device_create_file(repeater_device, &dev_attr_reset);
   if (retval < 0) {
    warn("failed to create reset /sys endpoint - continuing without\n");
   }
   mutex_init(&repeater_device_mutex);
   /* This device uses a Kernel FIFO for its read operation */
   INIT_KFIFO(repeater_msg_fifo);
   INIT_KFIFO(repeater_msg_size_fifo);
   //repeater_msg_idx_rd = repeater_msg_idx_wr = 0;

   return 0;

   failed_devreg:
      class_unregister(repeater_class);
      class_destroy(repeater_class);
   failed_classreg:
      unregister_chrdev(repeater_major, DEVICE_NAME);
   failed_chrdevreg:
      return -1;
}	


static void __exit repeater_exit(void) {
   dbg("");
   device_remove_file(repeater_device, &dev_attr_reset);
   device_destroy(repeater_class, MKDEV(repeater_major, 0));
   class_unregister(repeater_class);
   class_destroy(repeater_class);
   unregister_chrdev(repeater_major, DEVICE_NAME);
}

module_init(repeater_init);
module_exit(repeater_exit);
