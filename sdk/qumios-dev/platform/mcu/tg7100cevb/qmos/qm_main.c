#include "qm.h"
#include "qm_types.h"

extern void prodtst_client_generic_hal_init(void);

void qm_main(void)
{
    prodtst_client_generic_hal_init();
    qm_application_start();
}