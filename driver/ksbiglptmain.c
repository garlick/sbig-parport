// SPDX-License-Identifier: GPL-2.0-or-later

/* SBIG astronomy camera parallel port driver
 *
 * Replacement driver mainline developed on Linux 4.10, supercedes mainline
 * written for Linux 2.4 by Jan Soldan (c) 2002-2003.
 *
 * Copyright (c) 2017 by Jim Garlick
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/parport.h>

#include "ksbiglptmain.h"

#define SBIG_NO 3
static struct {
	struct pardevice *dev;
	spinlock_t spinlock;
} sbig_table[SBIG_NO];
static unsigned int sbig_count;

static dev_t sbig_dev;
static struct class *sbig_class;
static struct cdev sbig_cdev;


static int sbig_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct sbig_client *pd = file->private_data;
	int rc = 0;

	if (minor >= sbig_count) {
		rc = -ENODEV;
		goto out;
	}
	spin_lock(&sbig_table[minor].spinlock);
	if (pd) {
		rc = -EBUSY;
		goto out_unlock;
	}
	pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		rc = -ENOMEM;
		goto out_unlock;
	}
	pd->buffer = kzalloc(LDEFAULT_BUFFER_SIZE, GFP_KERNEL);
	if (!pd->buffer) {
		kfree(pd);
		rc = -ENOMEM;
		goto out_unlock;
	}
	pd->buffer_size = LDEFAULT_BUFFER_SIZE;
	pd->port = sbig_table[minor].dev->port;
	file->private_data = pd;
out_unlock:
	spin_unlock(&sbig_table[minor].spinlock);
out:
	return rc;
}

static int sbig_release(struct inode *inode, struct file *file)
{
	struct sbig_client *pd = file->private_data;

	if (pd) {
		kfree(pd->buffer);
		kfree(pd);
		file->private_data = NULL;
	}
	return 0;
}

static long sbig_unlocked_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	struct sbig_client *pd = file->private_data;
	int minor = iminor(file_inode(file));

	return sbig_ioctl(pd, cmd, arg, &sbig_table[minor].spinlock);
}

static void sbig_attach(struct parport *port)
{
	int nr = 0 + sbig_count;

	if (!(port->modes & PARPORT_MODE_PCSPP)) {
		dev_info(port->dev, "ignoring %s - no SPP capability\n",
			 port->name);
		return;
	}
	if (nr == SBIG_NO) {
		dev_info(port->dev, "ignoring %s - max %d reached\n",
			 port->name, SBIG_NO);
		return;
	}
	sbig_table[nr].dev = parport_register_device(port, "sbiglpt", NULL,
						     NULL, NULL, 0, NULL);
	if (sbig_table[nr].dev == NULL) {
		dev_err(port->dev, "parport_register_device failed\n");
		return;
	}
	if (parport_claim(sbig_table[nr].dev)) {
		parport_unregister_device(sbig_table[nr].dev);
		dev_err(port->dev, "parport_claim failed\n");
		return;
	}
	device_create(sbig_class, port->dev, MKDEV(MAJOR(sbig_dev), nr), NULL,
		      "sbiglpt%d", nr);
	spin_lock_init(&sbig_table[nr].spinlock);
	sbig_count++;
	dev_info(port->dev, "%s claimed by sbiglpt%d\n", port->name, nr);
}

static void sbig_detach(struct parport *port)
{
	//dev_info(port->dev, "detach %s\n", port->name);
}

static struct parport_driver sbig_driver = {
	.name = "sbiglpt",
	.attach = sbig_attach,
	.detach = sbig_detach,
};

static const struct file_operations sbig_fops = {
	.owner = THIS_MODULE,
	.open = sbig_open,
	.release = sbig_release,
	.unlocked_ioctl = sbig_unlocked_ioctl,
};

static int sbig_init_module(void)
{
	if (alloc_chrdev_region(&sbig_dev, 0, SBIG_NO, "sbiglpt") < 0) {
		pr_err("sbiglpt: alloc_chrdev_region failed\n");
		goto out;
	}
	sbig_class = class_create(THIS_MODULE, "chardrv");
	if (!sbig_class) {
		pr_err("sbiglpt: class_create failed\n");
		goto out_reg;
	}
	cdev_init(&sbig_cdev, &sbig_fops);
	if (cdev_add(&sbig_cdev, sbig_dev, SBIG_NO) < 0) {
		pr_err("sbiglpt: cdev_add failed\n");
		goto out_class;
	}
	if (parport_register_driver(&sbig_driver)) {
		pr_err("sbiglpt: parport_register_driver failed\n");
		goto out_cdev;
	}
	return (0);
out_cdev:
	cdev_del(&sbig_cdev);
out_class:
	class_destroy(sbig_class);
out_reg:
	unregister_chrdev_region(sbig_dev, SBIG_NO);
out:
	return (-1);
}

static void sbig_cleanup_module(void)
{
	int nr;

	parport_unregister_driver(&sbig_driver);
	cdev_del(&sbig_cdev);
	for (nr = 0; nr < sbig_count; nr++) {
		parport_release(sbig_table[nr].dev);
		parport_unregister_device(sbig_table[nr].dev);
		device_destroy(sbig_class, MKDEV(MAJOR(sbig_dev), nr));
	}
	class_destroy(sbig_class);
	unregister_chrdev_region(sbig_dev, SBIG_NO);
}
//========================================================================

module_init(sbig_init_module);
module_exit(sbig_cleanup_module);

// N.B. no license was declared with orig. SBIG source code.
// This *file* is declared GPL by its author (Jim Garlick).
// TODO: see if we can get a statement from copyright holders of the other bits.
MODULE_LICENSE("GPL");
