#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#include <asm/herring.h>

// static int __init xr68c681_init(void)
// {
//     printk("Init XR68C681 Driver");

//     return 0;
// }

// static void __exit xr68c681_exit(void)
// {
// }

// module_init(xr68c681_init);
// module_exit(xr68c681_exit);

// MODULE_AUTHOR("Colin Maykish <crmaykish@gmail.com>");
// MODULE_DESCRIPTION("XR68C681 DUART Driver");
// MODULE_LICENSE("GPL");
// MODULE_ALIAS("platform:xr68c681");
