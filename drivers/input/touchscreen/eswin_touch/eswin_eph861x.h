

#ifndef __LINUX_PLATFORM_DATA_ESWIN_EPH_H
#define __LINUX_PLATFORM_DATA_ESWIN_EPH_H

bool debug_log_flag = false;
#define ts_info(fmt, arg...) \
		pr_info("[ESWIN-INF][%s:%d] "fmt"\n", __func__, __LINE__, ##arg)
#define	ts_err(fmt, arg...) \
		pr_err("[ESWIN-ERR][%s:%d] "fmt"\n", __func__, __LINE__, ##arg)
#define ts_debug(fmt, arg...) \
		{if (debug_log_flag) \
		pr_info("[ESWIN-DBG][%s:%d] "fmt"\n", __func__, __LINE__, ##arg);}



#endif /* __LINUX_PLATFORM_DATA_ESWIN_EPH_ */
