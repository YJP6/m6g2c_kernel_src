#include <linux/linux_logo.h>
#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/backing-dev.h>
#include <linux/compat.h>

#include <linux/mtd/mtd.h>
#include <linux/fb.h>

#include <asm/uaccess.h>

#define IMAGE_HEAD_SIZE    	54
#define IMAGE_MAX_PIXEL    	(1024 * 640)				/* occupy memory space, not too big. */

#define IMAGE_MAX_SIZE (IMAGE_HEAD_SIZE+IMAGE_MAX_PIXEL+1024)
#define COLOR_MAX_NUM    	224        	/* linux logo just support 224 colors     		*/
#define IMAGE_MTD_NUM    	4        	/* the logo saved in MTD4 						*/
#define IMAGE_OFFSET    	0x0			/* the logo's offset int the mtd area     		*/

static unsigned char logo_flash_clut224_data[IMAGE_MAX_PIXEL] __initdata = {0};
static unsigned char logo_flash_clut224_clut[COLOR_MAX_NUM * 3] __initdata = {0};

static struct linux_logo logo_flash_clut224 __initdata = {
	.type      = LINUX_LOGO_CLUT224,
	.width     = 0,
	.height    = 0,
	.clutsize  = 0,
	.clut      = logo_flash_clut224_clut,
	.data      = logo_flash_clut224_data,
};


const struct linux_logo * __init_refok fb_find_flash_logo(void)
{    
	struct mtd_info *mtd = NULL;
	struct fb_info *fb_info = registered_fb[0];

	unsigned char head[IMAGE_HEAD_SIZE] = {0};
	unsigned char *image = NULL;
	unsigned char *clut = NULL;
	unsigned char *data = NULL;
	int clutsize = 0;

	int rt        = 0;
	int size      = 0;
	int offset    = 0;
	int width     = 0;
	int height    = 0;
	int count     = 0;
	int compress  = 0;
	int sizeimage = 0;
	int clrused   = 0;

	int i  = 0;
	int j  = 0;
	int fi = 0;
	int li = 0;

	//unsigned char last = 0;
	unsigned int real_width = 0;
	unsigned int logo_index = 0;

	mtd = get_mtd_device(NULL, IMAGE_MTD_NUM);
	if (IS_ERR(mtd)) {
		printk(" %s-%d:can not get mtd device\n", __FILE__, __LINE__);
		goto exit;
	}

	mtd->_read(mtd, IMAGE_OFFSET, ARRAY_SIZE(head), &rt, head);
	if (rt != ARRAY_SIZE(head)) {
		printk("ABING read head error!\n");
		goto put;
	}
	if (head[0] != 'B' && head[1] != 'M') {
		printk(" %s-%d:error image head:%d %d\n", __FILE__, __LINE__, head[0], head[1]);
		goto put;
	}
#if 0
	for (i =0; i < 54; i++) {
		printk(" %.2x", head[i]);
		if ((i+1)%16 == 0)
			printk("\n");
	}
	printk("\n");
#endif

	memcpy((char *)&size, &head[2], 4);
	memcpy((char *)&offset, &head[10], 4);
	memcpy((char *)&width, &head[18], 4);
	memcpy((char *)&height, &head[22], 4);
	memcpy((char *)&count, &head[28], 2);
	memcpy((char *)&compress, &head[30], 4);
	memcpy((char *)&sizeimage, &head[34], 4);
	memcpy((char *)&clrused, &head[46], 4);

	printk("<1> file szie:%d,offset:%d, width:%d, height:%d,count:%d,compress:%d, imagesize:%d, clrused:%d\n",
			size, offset, width, height, count, compress, sizeimage, clrused);

	if (size<0 || size > mtd->size) {
		printk(" %s-%d:the logo file is too big, file size is %d\n", __FILE__, __LINE__, size);
		goto put;
	}

#if 0
	if (offset > (IMAGE_HEAD_SIZE + 1024)) {
		printk(" %s-%d:the offset is too big, offset is %d\n", __FILE__, __LINE__, offset);
		goto put;
	}
#endif

	if (width <= 0 || height <= 0 || (width > fb_info->var.xres) || (height > fb_info->var.yres) ) {
		printk(" %s-%d:pixel is too much:width-%d, height-%d\n", __FILE__, __LINE__, width, height);
		goto put;
	}
	if (count !=8) {
		printk(" %s-%d:the bitwidth should be equal to 8, but it is %d\n", __FILE__, __LINE__, count);
		goto put;
	}
	if (compress != 0) {
		printk(" %s-%d:compress should be 0, but it is %d\n", __FILE__, __LINE__, compress);
		goto put;
	}
	if (clrused > COLOR_MAX_NUM || clrused < 0) {
		printk(" %s-%d:the number of color used should be little than %d, but it is %d\n",
				__FILE__, __LINE__, COLOR_MAX_NUM, clrused);
		goto put;
	}

	image = kmalloc(size, GFP_KERNEL);
	if (!image) {
		printk(" %s-%d:malloc for image failed\n", __FILE__, __LINE__);
		goto put;
	}
	mtd->_read(mtd, IMAGE_OFFSET, size, &rt, image);
	if (rt != size) {
		printk(" %s-%d:read image failed\n", __FILE__, __LINE__);
		goto free_image;
	}

	clut = image + IMAGE_HEAD_SIZE;
	data = image + offset;

	logo_flash_clut224.width = width;
	logo_flash_clut224.height = height;

	/*
	 * in BMP file, the clut's content is BGRX(x is alpha)
	 * but in linux logo,the series is RBG
	 */
	clutsize = 0;
	/* in BMP file, the head of ervery line must be 4 multiples */
	if (width%4 == 0)
		real_width = width;
	else
		real_width = 4 + (width/4)*4;
	printk(" real_width:%d\n", real_width);

	//if (sizeimage == 0) {
	//    sizeimage = real_width * width;
	//}
#if 0
	for (i = 0; i < sizeimage; i++) {

		if ( (i%real_width) >= width)
			continue;

		//fi = data[i]*4;
		fi = data[(height-1-i/real_width)*real_width + i%real_width]*4;
		for (j = 0; j < clutsize; j++) {
			li = j*3;
			if (clut[fi+2] == logo_flash_clut224_clut[li] &&
				clut[fi+1] == logo_flash_clut224_clut[li+1] &&
				clut[fi] == logo_flash_clut224_clut[li+2] )
				break;
		}
		if (j < clutsize) {
			logo_flash_clut224_data[logo_index++] = j + 32;
			last = j+32;
		} else if ( j == clutsize && clutsize < COLOR_MAX_NUM ) {
			logo_flash_clut224_clut[li] = clut[fi+2];
			logo_flash_clut224_clut[li+1] = clut[fi+1];
			logo_flash_clut224_clut[li+2] = clut[fi];
			logo_flash_clut224_data[logo_index++] = j + 32;
			last = j+32;
			clutsize++;
		} else {
			/* if the number of color is more than 224, we must ignor the last ones */
			logo_flash_clut224_data[logo_index++] = last;
		}
	}
#else
	for (j = 0; j< COLOR_MAX_NUM; j++) {
		li = j * 3;
		fi = j * 4;
		logo_flash_clut224_clut[li] = clut[fi+2];
		logo_flash_clut224_clut[li+1] = clut[fi+1];
		logo_flash_clut224_clut[li+2] = clut[fi];
	}

	sizeimage = (sizeimage>=IMAGE_MAX_PIXEL)?IMAGE_MAX_PIXEL:sizeimage;
	for (i = 0; i < sizeimage; i++) {
		if ((i%real_width) >= width)
			continue;
		logo_flash_clut224_data[logo_index++] = data[(height - 1 - i / real_width) * real_width + i % real_width] + 32;
	}
	clutsize = clrused;
#endif

	printk("clrsize:%d\n", clutsize);
	logo_flash_clut224.clutsize = clutsize;

	kfree(image);
	put_mtd_device(mtd);
	return &logo_flash_clut224;

free_image:
	kfree(image);
put:
	put_mtd_device(mtd);
exit:
	return NULL;
}

EXPORT_SYMBOL_GPL(fb_find_flash_logo);

