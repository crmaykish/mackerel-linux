#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>

#include <video/AMIX_gpu.h>

#define DRV_NAME "gpu-AMIX"

struct amix_gpu_par {
	void __iomem *regs;
};

static const struct fb_fix_screeninfo amix_gpu_fix_screeninfo = {
    .id = "AMIX GPU",
    .smem_start = 0, //To be filled later
    .smem_len = 0,	//To be filled later
    .type = FB_TYPE_PACKED_PIXELS,
    .visual = FB_VISUAL_PSEUDOCOLOR,
    .line_length = 640/2,
    .mmio_start = 0,	//To be filled later
    .accel = FB_ACCEL_NONE
};  


static struct fb_var_screeninfo amix_gpu_var_screeninfo = {
	.xres =		640,
	.yres =		480,
	.pixclock =	39722,
	.left_margin =	48,
	.right_margin =	16,
	.upper_margin =	33,
	.lower_margin =	10,
	.hsync_len =	96,
	.vsync_len =	2,
	.vmode =	FB_VMODE_NONINTERLACED,
    .bits_per_pixel = 4,

};




static int amix_gpu_blank(int blank, struct fb_info *info)
{
	return 0; //Nothing yet
}
static int amix_gpu_set_par(struct fb_info *info)
{
	return 0; //Nothing to do for now
}

static int amix_gpu_setcolreg(	unsigned regno,
								unsigned red,
								unsigned green,
								unsigned blue,
								unsigned transp,	//Not used
								struct fb_info *info)
{
	struct amix_gpu_par *par = info->par;
	unsigned offset;
	unsigned char reg0;
	unsigned char reg1;
	uint8_t r = red >> 12;
	uint8_t g = green >> 12;
	uint8_t b = blue >> 12;
	if(regno >= 16)
		return -EINVAL;

		
	offset = (regno * 2) + GPU_PALETTE_REG_OFF;
	reg0 = (g << 4) | b;
	reg1 = r;	// >> 12 for scaling 16 bit to 4 bit depth

	writeb(reg0, par->regs + offset);
	writeb(reg1, par->regs + offset + 1);

	return 0;
}
static int amix_gpu_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	if(var->bits_per_pixel != 4)
		return -EINVAL;
	
	if(var->xres > 640 || var->yres > 480)
		return -EINVAL;

	if(var->xres == 0 || var->yres == 0)
		return -EINVAL;

	if(var->xres & 1) //Must be multiple of 2 pixels
		return -EINVAL;


	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	var->red.length = 4;
	var->green.length = 4;
	var->blue.length = 4;
	var->transp.length = 0;

	var->red.offset = 0;
	var->green.offset = 0;
	var->blue.offset = 0;
	var->transp.offset = 0;
	return 0;
}

static void amix_gpu_update_fix(struct fb_info *info)  //For future use when there will be multiple modes
{
	info->fix.line_length = 320;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
}
/* Check if the video mode is supported by the driver */
static inline int check_mode_supported(const struct screen_info *si)
{
	unsigned int type = screen_info_video_type(si);

	/* only VGA in 16 color graphic mode is supported */
	if (type != VIDEO_TYPE_VGAC)
		return -ENODEV;

	if (si->orig_video_mode != 0x12)	/* 640x480/4 (VGA) */
		return -ENODEV;

	return 0;
}


static const struct fb_ops amix_gpu_ops = {
	.owner			= THIS_MODULE,
	//.fb_open        = amix_gpu_open,			//Dont need these right now
	//.fb_release     = amix_gpu_release,
	//.fb_destroy		= amix_gpu_destroy,
	.fb_check_var	= amix_gpu_check_var,
	.fb_set_par		= amix_gpu_set_par,
	.fb_setcolreg 	= amix_gpu_setcolreg,

	.fb_blank 		= amix_gpu_blank,

	FB_DEFAULT_IOMEM_OPS,
};


static struct screen_info amix_default_si = {
    .orig_video_mode = 0x12,   /* 640x480x4 VGA mode equivalent */
    .orig_video_isVGA = VIDEO_TYPE_VGAC,
};

static int amix_gpu_probe(struct platform_device *pdev)
{
	struct screen_info *si;
	struct fb_info *info;
	struct amix_gpu_par *par;
	struct resource *gpu_res;
	int ret = 0;

	si = dev_get_platdata(&pdev->dev);
	if (!si) {
		dev_warn(&pdev->dev, "no platform data, using default mode\n");
    	si = &amix_default_si;
	}
	ret = check_mode_supported(si);
	if (ret)
		return ret;

	printk(KERN_INFO "gpu-AMIX: initializing\n");
	info = framebuffer_alloc(sizeof(struct amix_gpu_par), &pdev->dev);

	if (!info) {
		ret = -ENOMEM;
		goto err_fb_alloc;
	}


	info->fbops = &amix_gpu_ops;
	info->var = amix_gpu_var_screeninfo;
	info->fix = amix_gpu_fix_screeninfo;


	
	par = info->par;

	gpu_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!gpu_res) {
		printk(KERN_ERR "gpu-AMIX: unable to get gpu mem/io resource\n");
		ret = -ENODEV;
		goto err_ioremap;
	}

	info->screen_base = devm_ioremap_resource(&pdev->dev, gpu_res);

	if(IS_ERR(info->screen_base)) {
		ret = PTR_ERR(info->screen_base);
		goto err_ioremap;
	}

	par->regs = info->screen_base + GPU_REG_OFF;

	if(IS_ERR(par->regs)) {
		ret = PTR_ERR(par->regs);
		goto err_ioremap;
	}

	info->fix.smem_start = gpu_res->start;
	info->fix.smem_len   = resource_size(gpu_res);


	dev_info(&pdev->dev, "gpu-AMIX: VRAM mapped to 0x%p\n", info->screen_base);
	dev_info(&pdev->dev, "gpu-AMIX: REGS mapped to 0x%p\n", par->regs);

	
	

	info->flags = FBINFO_HWACCEL_NONE; //For now ...

	ret = fb_alloc_cmap(&info->cmap, 16, 0); // Allocate 16 entries for colormap (4bpp)

	if (ret) {
		printk(KERN_ERR "gpu-AMIX: unable to allocate colormap\n");
		ret = -ENOMEM;
		goto err_alloc_cmap;
	}

	if (amix_gpu_check_var(&info->var, info)) {
		printk(KERN_ERR "gpu-AMIX: unable to validate variable\n");
		ret = -EINVAL;
		goto err_check_var;
	}

	amix_gpu_update_fix(info);


	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR "gpu-AMIX: unable to register framebuffer\n");
		ret = -ENOMEM;
		goto err_check_var;
	}

	dev_info(&pdev->dev, "%s frame buffer device\n", info->fix.id);
	platform_set_drvdata(pdev, info);

	return 0;


 err_check_var:
	fb_dealloc_cmap(&info->cmap);
 err_alloc_cmap:

 err_ioremap:
	framebuffer_release(info);
 err_fb_alloc:
	return ret;

}

static void amix_gpu_remove(struct platform_device *pdev)
{
    struct fb_info *info = platform_get_drvdata(pdev);

    unregister_framebuffer(info);
    fb_dealloc_cmap(&info->cmap);
    framebuffer_release(info);

}




static struct platform_driver amix_gpu_driver = {
	.probe  = amix_gpu_probe,
	.remove = amix_gpu_remove,
	.driver = {
		.name = DRV_NAME,
	},
};


module_platform_driver(amix_gpu_driver);


MODULE_DESCRIPTION("AMIX GPU driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
