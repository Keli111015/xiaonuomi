#ifndef LJYD_CLOUD_LOCAL_H
#define LJYD_CLOUD_LOCAL_H
/* Copy to ljyd_cloud_local.h in your SDK and enter your own product values.
 * SECERT is the upstream SDK spelling; do not rename it. */
#define CONFIG_XJIANG_CLOUD_PID 0
#define CONFIG_XJIANG_CLOUD_SECERT "REPLACE_WITH_YOUR_OWN_PRODUCT_SECRET"
#if CONFIG_XJIANG_CLOUD_PID == 0
#error "Set your own product ID and secret in this local, untracked file"
#endif
#endif
