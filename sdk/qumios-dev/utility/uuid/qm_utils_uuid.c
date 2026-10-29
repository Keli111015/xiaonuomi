#include "qm_utils_uuid.h"
#include "qm_kernel.h"

char *qm_utils_random_uuid( char uuid[37] )
{
    const char *c = "89ab";
    char *p = uuid;
    int n = 0;
    unsigned int b = 0;

    qm_srandom(qm_now_ms());
    for( n = 0; n < 16; ++n )
    {
        b = qm_random_get(255);
        switch( n )
        {
            case 6:
                sprintf(p, "4%x", b%15 );
            break;
            case 8:
                sprintf(p, "%c%x", c[qm_random_get(strlen(c))], b%15 );
            break;
            default:
                sprintf(p, "%02x", b);
            break;
        }

        p += 2;
        switch( n )
        {
            case 3:
            case 5:
            case 7:
            case 9:
                *p++ = '-';
                break;
        }
    }
    *p = 0;
    return uuid;
}

char *qm_utils_unformatted_uuid( char uuid[33] )
{
    const char *c = "89ab";
    char *p = uuid;
    int n = 0;
    unsigned int b = 0;

    qm_srandom(qm_now_ms());
    for( n = 0; n < 16; ++n )
    {
        b = qm_random_get(255);
        switch( n )
        {
            case 6:
                sprintf(p, "4%x", b%15 );
            break;
            case 8:
                sprintf(p, "%c%x", c[qm_random_get(strlen(c))], b%15 );
            break;
            default:
                sprintf(p, "%02x", b);
            break;
        }

        p += 2;
    }
    *p = 0;
    return uuid;
}