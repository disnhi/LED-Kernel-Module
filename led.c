
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/timer.h>

#define BUFFER_LENGTH 256


static short size_of_message;
static char message[256] = {0};
static int numOpen = 0;
char *msg_ptr; // Tracks the user's written message into device
char *pic; // Tracks current light bulb ASCII picture

static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static int dev_release(struct inode *, struct file *);

struct GpioRegisters { // To set the GPIO of our LED
	uint32_t GPFSEL[6];
	uint32_t reserved1;
	uint32_t GPSET[2];
	uint32_t reserved2;
	uint32_t GPCLR[2];
};

struct GpioRegisters *s_pGpioRegisters;

static void SetGPIOFunc(int GPIO, int funcCode) { // Sets the GPIO function of our gpio (which is 18)
	int registerIndex = GPIO / 10;
	int bit = (GPIO % 10) * 3;
	
	unsigned oldVal = s_pGpioRegisters->GPFSEL[registerIndex];
	unsigned mask = 0b111 << bit;
	
	s_pGpioRegisters->GPFSEL[registerIndex] = (oldVal & ~mask) | ((funcCode << bit) & mask);
}

static void SetGPIOOutputVal(int GPIO, bool outputVal){ // Sets the output
	
	if(outputVal)
	{
		s_pGpioRegisters->GPSET[GPIO/32] = (1 << (GPIO % 32));
	}
	else
	{
		s_pGpioRegisters->GPCLR[GPIO/21] = (1 << (GPIO % 32));
	}
}

static int major;
dev_t devNo;

struct class *ledClass;
static const int LedGpioPin = 18; // GPIO pin that the LED is plugged into

#define PERIPH_BASE 0x3f000000
#define GPIO_BASE (PERIPH_BASE + 0x200000)

static int dev_open(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static int dev_release(struct inode *, struct file *);

static int dev_open(struct inode *inodeLED, struct file *fileLED) {
	numOpen ++;
	printk(KERN_INFO "LED: Device has been opened %d time(s)\n", numOpen);
	
	return 0;
}

static ssize_t device_read(struct file *filp, char *buff, size_t length, loff_t *offset)
{
	const char *s_ptr;
	int error_code = 0;
	ssize_t len = 0;
	
	s_ptr = "(.)\n = \n"; // automatically set the led read as off
	
	len = min( (unsigned long)(strlen(s_ptr) - *offset) , (unsigned long)(length) );
	if (len <= 0)
	{
		return 0;
	}
	
	char on[] = "on\n"; 
	char off[] = "off\n";
	
	//printk(KERN_INFO "msg_ptr IS:%s", msg_ptr);

	
	if (msg_ptr == NULL) { 
		// if user didn't write to device, 
		// then the LED is still automatically off
		error_code = copy_to_user(buff, s_ptr + *offset, len);
	}
	
	// User wrote to device, so we need to see what they 
	// wrote and adjust ASCII image accordingly
	else { 
	
		// Only changes ASCII image if the user wrote either "on" or "off" to device. If not, ASCII image doesn't change
		if (strcmp(msg_ptr,on) == 0) { 
			// user wrote the LED to turn on, so matching ASCII image with on light bulb
			s_ptr = "(*)\n = \n";
		}
		else if(strcmp(msg_ptr,off) == 0) { 
			// means off, so matching ASCII image
			s_ptr = "(.)\n = \n";
		}
		else { 
			// user wrote something other than "on" or "off", so the physical LED 
			// didn't change state, so ASCII pic remains the same
			s_ptr = pic;
		}

		error_code = copy_to_user(buff, s_ptr + *offset, len); //prints to user
		
	}
	
	pic = kmalloc(len, GFP_KERNEL);
	printk(KERN_INFO "pic len:%d", strlen(pic));
	pic = s_ptr;
	
	*offset += len;
	return len;
}

static ssize_t dev_write(struct file *filp, const char *buff, size_t length, loff_t *offset) {
	// buff is the variable for the data from user
	
	printk(KERN_INFO "LED: Received %zu characters from the user\n", length);
	
	msg_ptr = kmalloc(length, GFP_KERNEL); // allocates space for user's input with the length of user input
	copy_from_user(msg_ptr, buff, length);
	
	msg_ptr[length] = '\0'; //null ptr
	
	//printk(KERN_INFO "user input is:%s", msg_ptr);
	//printk(KERN_INFO "length of user input:%d\n", strlen(msg_ptr)); //need to do strlen bc sizeof gives you block size or something?
	
	// need the new space and null ptr to match with user input
	char on[] = "on\n\0"; 
	char off[] = "off\n\0";
	
	if (strcmp(msg_ptr,on) == 0) { 
		// turns on the LED if the user wrote "on"
		SetGPIOFunc(LedGpioPin, 0b001); 
		SetGPIOOutputVal(LedGpioPin, true);
	}
	else if (strcmp(msg_ptr,off) == 0) {
		// turns off the LED if the user wrote "off"
		SetGPIOFunc(LedGpioPin, 0); 
		static bool on = false;
		on = !on;
		SetGPIOOutputVal(LedGpioPin, on);	
	}

	return strlen(msg_ptr);
}

static int dev_release(struct inode *inodeLED, struct file *fileLED) {
	printk(KERN_INFO "LED: Device successfully closed\n");
	return 0;
}

static struct file_operations fops = {
	.open = dev_open,
	.read = device_read,
	.write = dev_write,
	.release = dev_release,
};

static int __init start(void)
{
	
	struct device *pDev;
	printk(KERN_ALERT "LED char-dev module loaded!\n");
	major = register_chrdev(0, "led", &fops);
	
	if(major < 0)
	{
		printk(KERN_ALERT "Failed to register char-dev file!\n");
		return major;
	}
	
	devNo = MKDEV(major, 0); //create a dev_t
	
	// Create /sys/class/led in preparation for creating /dev/led
	ledClass = class_create(THIS_MODULE, "led");
	if (IS_ERR(ledClass)) {
		printk(KERN_WARNING "\nCan't create class");
		unregister_chrdev_region(major, "led");
		return -1;
	}
	
	// Create /dev/led for this char dev
	if (IS_ERR(pDev = device_create(ledClass, NULL, devNo, NULL, "led"))) {
		printk(KERN_WARNING "led.ko can't create device /dev/led\n");
		class_destroy(ledClass);
		unregister_chrdev_region(devNo,1);
		return -1;
	}
	
	
	printk("New char-dev 'led' created with major %d and minor %d\n", major, 0);
	
	// Setting up the connections with GPIO (memory map I/O)
	s_pGpioRegisters = (struct GpioRegisters *)ioremap(GPIO_BASE, sizeof(struct GpioRegisters));
	
	return 0;

}


static void __exit end(void)
{
	printk(KERN_ALERT "Module Unloaded!\n");
	SetGPIOFunc(LedGpioPin, 0);
	iounmap(s_pGpioRegisters);
	
	
	device_destroy(ledClass,devNo);
	class_destroy(ledClass);
	unregister_chrdev(major, "led");
	
}


module_init(start);
module_exit(end);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nhi");
MODULE_DESCRIPTION("led");
