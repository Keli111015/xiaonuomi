#include "qm.h"
#include "qm_wdg.h"

#include <stddef.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <kernel.h>
#include <sys/byteorder.h>
#include <sys/ioctl.h>
#include <sys/crc.h>
#include <fcntl.h>
#include <nuttx/timers/watchdog.h>

/* Config WatchDog*/
static int fd_wd;
struct board_ctl {
	unsigned long req;
	unsigned long val;
};

static struct board_ctl wdog_ctls[] = {
	{WDIOC_SETTIMEOUT, 0},
	{WDIOC_START, 0},
};

static const struct {
	int *fd;
	const char *dev_path;
	int oflag;
	struct board_ctl *ctls;
	uint32_t ctls_count;
} hw_drivers[] = {
	{&fd_wd, "/dev/watchdog0", O_RDONLY, wdog_ctls, QM_ARRAY_SIZE(wdog_ctls)},
};

int32_t qm_wdg_init(qm_wdg_dev_t *wdg)
{
	int err = 0;
    if(wdg == NULL){
        return -QM_EINVAL;
    }

	for (int i = 0; i < ARRAY_SIZE(hw_drivers); i++) {
		int fd = open(hw_drivers[i].dev_path, hw_drivers[i].oflag);
		if (fd < 0) {
			printk("%s open failed\n", hw_drivers[i].dev_path);
			return -QM_EIO;
		}

		*hw_drivers[i].fd = fd;
		for (int j = 0; j < hw_drivers[i].ctls_count; j++) {

            if(hw_drivers[i].ctls[j].req == WDIOC_SETTIMEOUT){
                hw_drivers[i].ctls[j].val = wdg->config.timeout;
            }

			err = ioctl(fd, hw_drivers[i].ctls[j].req, hw_drivers[i].ctls[j].val);
			if (err < 0) {
				printk("%s ioctl req %ld val %ld err:%d\n", hw_drivers[i].dev_path,
				       hw_drivers[i].ctls[j].req, hw_drivers[i].ctls[j].val, err);
				return -QM_EIO;
			}
		}
	}

	return QM_EOK;
}


void qm_wdg_reload(qm_wdg_dev_t *wdg)
{
    int ret = QM_EOK;
    if(wdg == NULL){
        return;
    }

	ret = ioctl(fd_wd, WDIOC_KEEPALIVE, 0);
	if (ret < 0) {
		printk("wd feed fail %d\n", ret);
	}
}

//暂不实现
int32_t qm_wdg_deinit(qm_wdg_dev_t *wdg)
{
    return -QM_EINVAL;
}
