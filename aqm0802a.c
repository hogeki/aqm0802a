#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>

#define BUFSIZE 17

#define AQM0802A_IOC_MAGIC 'h'
#define AQM0802A_IOSETCONT _IO(AQM0802A_IOC_MAGIC, 1)
#define AQM0802A_IOSETCURSOR _IO(AQM0802A_IOC_MAGIC, 2)

static int major = 0;

struct aqm0802a_device
{
	struct i2c_client *client;
	struct cdev cdev;
	char buf[BUFSIZE];
	int size;
};

int aqm0802a_open(struct inode *inode, struct file *filp)
{
	struct aqm0802a_device *dev;
	
	dev = container_of(inode->i_cdev, struct aqm0802a_device, cdev);
	filp->private_data = dev;
	return 0;
}

int aqm0802a_release(struct inode *inode, struct file *filp)
{
	return 0;
}

ssize_t aqm0802a_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct aqm0802a_device *dev = filp->private_data;
	int c = count;

	if((long)*f_pos + c > dev->size)
	{
		c = dev->size - (long)*f_pos;
		if(c <= 0)
			return 0;
	}

	if(copy_to_user(buf, &dev->buf[(long)*f_pos], c))
	{
		return -EFAULT;
	}

	*f_pos += c;
	return c;
}

ssize_t aqm0802a_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct aqm0802a_device *dev = filp->private_data;
	int c = count;
	char cmd[32];
	int pos, len, l, i;

	if((long)*f_pos + c > BUFSIZE - 1)
	{
		c = BUFSIZE - 1 - (long)*f_pos;
		if(c <= 0)
			return -ENOSPC;
	}

	if(copy_from_user(&dev->buf[(long)*f_pos], buf, c))
	{
		return -EFAULT;
	}

	pos = (int)*f_pos;
	len = c;
	l = len;

	if(pos < 8)
	{
		cmd[0] = 0x00;
		cmd[1] = 0x80 | pos; //set dram addr pos
		i2c_master_send(dev->client, cmd, 2);
		udelay(27);
		if(pos + len > 8)
		{
			l = 8 - pos;
		}
		else
		{
			l = len;
		}
		cmd[0] = 0x40;
		for(i = 0; i < l; i++)
		{
			cmd[i+1] = dev->buf[pos+i];
		}
		i2c_master_send(dev->client, cmd, l+1);
		if(pos + len == 8)
		{
			cmd[0] = 0x00;
			cmd[1] = 0x80 | 0x40;
			i2c_master_send(dev->client, cmd, 2);
			udelay(27);
		}
	}
	if(pos + len > 8)
	{
		if(pos < 8)
		{
			pos = 8;
			len -= l;
		}
		cmd[0] = 0x00;
		cmd[1] = 0x80 | 0x40 | (pos - 8);
		i2c_master_send(dev->client, cmd, 2);
		udelay(27);
		cmd[0] = 0x40;
		for(i = 0; i < len; i++)
		{
			cmd[i+1] = dev->buf[pos+i];
		}
		i2c_master_send(dev->client, cmd, len+1);
	}

	*f_pos+=c;
	if(*f_pos > dev->size)
		dev->size = (int)*f_pos;

	return c;
}

loff_t aqm0802a_llseek(struct file *filp, loff_t off, int whence)
{
	struct aqm0802a_device *dev = filp->private_data;
	loff_t newpos;
	char cmd[2];

	switch(whence)
	{
		case 0:
			newpos = off;
			break;
		case 1:
			newpos = filp->f_pos + off;
			break;
		case 2:
			newpos = dev->size + off;
			break;
		default:
			return -EINVAL;
	}
	if(newpos < 0)
		newpos = 0;
	else if(newpos > BUFSIZE - 1)
		newpos = BUFSIZE - 1;
	filp->f_pos = newpos;
	if(newpos > dev->size)
		dev->size = newpos;
	cmd[0] = 0x00;
	if(newpos < 8)
		cmd[1] = 0x80 | (char)newpos;
	else
		cmd[1] = 0x80 | 0x40 | ((char)newpos-8);
	i2c_master_send(dev->client, cmd, 2);
		
	return newpos;
}

static void setcontrast(struct i2c_client *client, int cont)
{
	char cmd[2];

	cmd[0] = 0x00;
	cmd[1] = 0x70 | (cont & 0x0F);
	i2c_master_send(client, cmd, 2);
	cmd[1] = 0x5c | ((cont >> 4) & 0x03);
	i2c_master_send(client, cmd, 2);
}

static void setcursor(struct i2c_client *client, int c)
{
	char cmd[2];
	
	cmd[0] = 0x00;
	switch(c)
	{
		case 0: //cursor off
			cmd[1] = 0x0C;
			break;
		case 1: //cursor on
			cmd[1] = 0x0E;
			break;
		case 2: //cursor blink
			cmd[1] = 0x0F;
			break;
	}
	i2c_master_send(client, cmd, 2);
}

long aqm0802a_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct aqm0802a_device *dev = filp->private_data;

	switch(cmd)
	{
		case AQM0802A_IOSETCONT:
			setcontrast(dev->client, (int)arg);
			break;
		case AQM0802A_IOSETCURSOR:
			setcursor(dev->client, (int)arg);
			break;
		default:
			return -ENOTTY;
	}
	return 0;
}

struct file_operations aqm0802a_fops = 
{
	.owner = THIS_MODULE,
	.llseek = aqm0802a_llseek,
	.read = aqm0802a_read,
	.write = aqm0802a_write,
	.unlocked_ioctl = aqm0802a_ioctl,
	.open = aqm0802a_open,
	.release = aqm0802a_release,
};

static int __devinit aqm0802a_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct aqm0802a_device *dev;
	char cmd[64];
	int i;

	printk(KERN_NOTICE "aqm0802a probe\n");
	//printk(KERN_NOTICE "slave addr=%x\n", client->addr);

	dev = kzalloc(sizeof(struct aqm0802a_device), GFP_KERNEL);
	if(dev == NULL)
	{
		printk(KERN_ERR "%s: no memory\n", __func__);
		return -ENOMEM;
	}

	for(i = 0; i < BUFSIZE-1; i++)
		dev->buf[i] = ' ';
	dev->buf[BUFSIZE-1] = 0;
	dev->size = 0;
	dev->client = client;
	i2c_set_clientdata(client, dev);
	cdev_init(&dev->cdev, &aqm0802a_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &aqm0802a_fops;
	cdev_add(&dev->cdev, MKDEV(major, 0), 1);

	cmd[0] = 0x00;
	cmd[1] = 0x38;
	cmd[2] = 0x39;
	cmd[3] = 0x14;
	cmd[4] = 0x78;
	cmd[5] = 0x5e;
	cmd[6] = 0x6c;
	i2c_master_send(client, cmd, 7);
	msleep(250);
	cmd[0] = 0x00;
	cmd[1] = 0x0e;
	cmd[2] = 0x01;
	cmd[3] = 0x06;
	i2c_master_send(client, cmd, 4);
	msleep(50);
	setcontrast(client, 32);
	return 0;
}

static int __devexit aqm0802a_i2c_remove(struct i2c_client *client)
{
	struct aqm0802a_device *dev;
	printk(KERN_NOTICE "aqm0802a remove\n");

	dev = i2c_get_clientdata(client);
	cdev_del(&dev->cdev);
	kfree(dev);
	return 0;
}

static const struct i2c_device_id aqm0802a_i2c_id[] =
{
	{ "aqm0802a", 0 },
	{ }
};

static const unsigned short normal_i2c[] = { 0x3e, I2C_CLIENT_END};

static struct i2c_driver aqm0802a_driver = 
{
	.probe = aqm0802a_i2c_probe,
	.remove = __devexit_p(aqm0802a_i2c_remove),
	.id_table = aqm0802a_i2c_id,
	.driver = {
		.name = "aqm0802a",
	},
	.address_list = normal_i2c,

};

static int aqm0802a_init(void)
{
	int result;
	dev_t dev;
	printk(KERN_NOTICE "aqm0802a_init\n");

	result = alloc_chrdev_region(&dev, 0, 1, "aqm0802a");
	if(result < 0)
	{
		printk(KERN_ERR "%s: can't get major\n", __func__);
		return result;
	}
	major = MAJOR(dev);
	return i2c_add_driver(&aqm0802a_driver);
}

static void aqm0802a_exit(void)
{
	printk(KERN_NOTICE "aqm0802a_exit\n");
	unregister_chrdev_region(MKDEV(major, 0), 1);
	i2c_del_driver(&aqm0802a_driver);
}

module_init(aqm0802a_init)
module_exit(aqm0802a_exit)
MODULE_DESCRIPTION("AQM0802A driver");
MODULE_LICENSE("Dual BSD/GPL");
