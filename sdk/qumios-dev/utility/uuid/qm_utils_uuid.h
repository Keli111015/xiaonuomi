#ifndef _QM_UTILS_UUID_H_
#define _QM_UTILS_UUID_H_

#ifdef __cplusplus
extern "C" {
#endif

char *qm_utils_random_uuid( char uuid[37] );
char *qm_utils_unformatted_uuid( char uuid[33] );


#ifdef __cplusplus
}
#endif


#endif
