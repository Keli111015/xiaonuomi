#ifndef _HUWEN_WAKEAPI_H_
#define _HUWEN_WAKEAPI_H_

extern int huwenSendRecordData(char *buffer,int length);
extern int huwenSendLineInData(char *buffer,int length);

extern int HuwenWakeupTaskInit(int mpo_dp,void *callback,char *filename);

#endif
