/*
 *
 */
#undef BOOTSTRAP
#include "config.h"
#include "libopenbios/bindings.h"
#include "arch/common/nvram.h"
#include "drivers/drivers.h"
#include "libc/diskio.h"
#include "libc/vsprintf.h"
#include "libopenbios/sys_info.h"
#include "openprom.h"
#include "boot.h"

struct sys_info sys_info;
uint32_t kernel_image;
uint32_t kernel_size;
uint32_t qemu_cmdline;
uint32_t cmdline_size;
char boot_device;
int (*entry)(const void *romvec_ptr, int p2, int p3, int p4, int p5);

static int try_path(const char *path, char *param, const void *romvec)
{
	ucell valid, address, type, size;
	int image_retval = 0;

        push_str(path);
        fword("pathres-resolve-aliases");
        bootpath = pop_fstr_copy();
        printk("Trying %s (%s)\n", path, bootpath);

	/* ELF Boot loader */
	elf_load(&sys_info, path, param, romvec);
	feval("state-valid @");
	valid = POP();
	if (valid)
		goto start_image;

	/* Linux loader (not using Forth) */
	linux_load(&sys_info, path, param);

	/* a.out loader */
	aout_load(&sys_info, path, romvec);
	feval("state-valid @");
	valid = POP();
	if (valid)
		goto start_image;

	return 0;


start_image:
	/* Get the entry point and the type (see forth/debugging/client.fs) */
	feval("saved-program-state >sps.entry @");
	address = POP();
	feval("saved-program-state >sps.file-type @");
	type = POP();
	feval("saved-program-state >sps.file-size @");
	size = POP();

	printk("Jumping to entry point " FMT_ucellx " for type " FMT_ucellx "...\n", address, type);

	switch (type) {
		case 0x0:
			/* Start ELF boot image */
			entry = (void *) address;
			image_retval = entry(romvec, 0, 0, 0, 0);

			break;

		case 0x5:
			/* Start a.out image */
			entry = (void *) address;
			image_retval = entry(romvec, 0, 0, 0, 0);

			break;
	}

	printk("Image returned with return value %#x\n", image_retval);

	return -1;
}


void boot(void)
{
        char *path = pop_fstr_copy(), *param, altpath[256];
        const char *oldpath = path;
        int unit = 0;
        const void *romvec;
	int result;

	if(!path) {
            push_str("boot-device");
            push_str("/options");
            fword("(find-dev)");
            POP();
            fword("get-package-property");
            if (!POP()) {
                path = pop_fstr_copy();
            } else {
                switch (boot_device) {
                case 'a':
                    path = strdup("/obio/SUNW,fdtwo");
                    oldpath = "fd()";
                    break;
                case 'c':
                    path = strdup("disk");
                    oldpath = "sd(0,0,0):d";
                    break;
                default:
                case 'd':
                    path = strdup("cdrom");
                    // FIXME: hardcoding this looks almost definitely wrong.
                    // With sd(0,2,0):b we get to see the solaris kernel though
                    //oldpath = "sd(0,2,0):d";
                    oldpath = "sd(0,2,0):b";
                    unit = 2;
                    break;
                case 'n':
                    path = strdup("net");
                    oldpath = "le()";
                    break;
                }
            }
	}

        obp_arg.boot_dev_ctrl = 0;
        obp_arg.boot_dev_unit = unit;
        obp_arg.dev_partition = 0;
        obp_arg.boot_dev[0] = oldpath[0];
        obp_arg.boot_dev[1] = oldpath[1];
        obp_arg.argv[0] = oldpath;
        obp_arg.argv[1] = (void *)(long)qemu_cmdline;

	param = strchr(path, ' ');
	if(param) {
		*param = '\0';
		param++;
	} else if (cmdline_size) {
            param = (char *)qemu_cmdline;
        } else {
            push_str("boot-args");
            push_str("/options");
            fword("(find-dev)");
            POP();
            fword("get-package-property");
            POP();
            param = pop_fstr_copy();
            obp_arg.argv[1] = param;
        }

        romvec = init_openprom();

        if (kernel_size) {
            printk("[sparc] Kernel already loaded\n");
            entry = (void *) kernel_image;
            entry(romvec, 0, 0, 0, 0);
        }

	printk("[sparc] Booting file '%s' ", path);
	if (param)
		printk("with parameters '%s'\n", param);
	else
		printk("without parameters.\n");

        result = try_path(path, param, romvec);
	if (!result) {
		push_str(path);
		PUSH(':');
		fword("left-split");
		snprintf(altpath, sizeof(altpath), "%s:d", pop_fstr_copy());
		POP();
		POP();
	
		try_path(altpath, param, romvec);
	}

	printk("Unsupported image format\n");

	free(path);
}
