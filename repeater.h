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
 
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
 
#define DEVICE_NAME "device"
#define CLASS_NAME "repeater"
#define REPEATER_MSG_FIFO_SIZE 1024
#define REPEATER_MSG_FIFO_MAX  128

#define AUTHOR "Mikko Rosten <mikko.roste@iki.fi>"
#define DESCRIPTION "'repeater' sample device driver"
#define VERSION "0.1"


/* We'll use our own macros for printk */
#define dbg(format, arg...) do { if (debug) pr_info(CLASS_NAME ": %s: " format, __FUNCTION__, ## arg); } while (0)
#define err(format, arg...) pr_err(CLASS_NAME ": " format, ## arg)
#define info(format, arg...) pr_info(CLASS_NAME ": " format, ## arg)
#define warn(format, arg...) pr_warn(CLASS_NAME ": " format, ## arg)
