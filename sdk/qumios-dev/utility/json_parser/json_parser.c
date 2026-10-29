
#include "qm.h"
#include "json_parser.h"

typedef struct JSON_NV {
    int         nLen;
    int         vLen;
    int         vType;
    char       *pN;
    char       *pV;
} JSON_NV;

char *qm_json_get_object(int type, char *str)
{
    char *pos = 0;
    char ch = (type == JOBJECT) ? '{' : '[';
    while (str != 0 && *str != 0) {
        if (*str == ' ') {
            str++;
            continue;
        }
        pos = (*str == ch) ? str : 0;
        break;
    }
    return pos;
}

char *qm_json_get_next_object(int type, char *str, char **key, int *key_len,
                           char **val, int *val_len, int *val_type)
{
    char    JsonMark[JTYPEMAX][2] = { { '\"', '\"' }, { '{', '}' }, { '[', ']' }, { '0', ' ' } };
    int     iMarkDepth = 0, iValueType = JNONE, iNameLen = 0, iValueLen = 0, total_len = strlen(str);
    char   *p_cName = 0, *p_cValue = 0, *p_cPos = str;
    char   *str_pos_start=NULL, *str_pos_end=NULL;

    if (type == JOBJECT) {
        /*skip '}'*/
        if(*p_cPos == '}'){
            return 0;
        }
        /* check if we skipped to the end of the buffer */
        if (*p_cPos == '\0'){
            return 0;
        }
        
        /* Get Key */
        p_cPos = strchr(p_cPos, '"');
        if (!p_cPos) {
            return 0;
        }
        p_cName = ++p_cPos;
        p_cPos = strchr(p_cPos, '"');
        if (!p_cPos) {
            return 0;
        }
        iNameLen = p_cPos - p_cName;

        /* Get Value */
        p_cPos = strchr(p_cPos, ':');
    }
    while (p_cPos && *p_cPos) {
        if (*p_cPos == '"') {
            iValueType = JSTRING;
            p_cValue = ++p_cPos;
            str_pos_start = p_cValue;
            break;
        } else if (*p_cPos == '{') {
            iValueType = JOBJECT;
            p_cValue = p_cPos++;
            break;
        } else if (*p_cPos == '[') {
            iValueType = JARRAY;
            p_cValue = p_cPos++;
            break;
        } else if (*p_cPos >= '0' && *p_cPos <= '9') {
            iValueType = JNUMBER;
            p_cValue = p_cPos++;
            break;
        } else if (*p_cPos == 't' || *p_cPos == 'T' || *p_cPos == 'f' || *p_cPos == 'F') {
            iValueType = JBOOLEAN;
            p_cValue = p_cPos;
            break;
        }
        p_cPos++;
    }
    while (p_cPos && *p_cPos && iValueType > JNONE) {
        if (iValueType == JBOOLEAN) {
            int     len = strlen(p_cValue);

            if ((*p_cValue == 't' || *p_cValue == 'T') && len >= 4
                && (!strncmp(p_cValue, "true", 4)
                    || !strncmp(p_cValue, "TRUE", 4))) {
                iValueLen = 4;
                p_cPos = p_cValue + iValueLen;
                break;
            } else if ((*p_cValue == 'f' || *p_cValue == 'F') && len >= 5
                       && (!strncmp(p_cValue, "false", 5)
                           || !strncmp(p_cValue, "FALSE", 5))) {
                iValueLen = 5;
                p_cPos = p_cValue + iValueLen;
                break;
            }
        } else if (iValueType == JNUMBER) {
            if (*p_cPos < '0' || *p_cPos > '9') {
                iValueLen = p_cPos - p_cValue;
                break;
            }
        } else if (*p_cPos == JsonMark[iValueType][1]) {
            if (iMarkDepth == 0) {
                iValueLen = p_cPos - p_cValue + (iValueType == JSTRING ? 0 : 1);
                str_pos_end=p_cPos;
                p_cPos++;
                break;
            } else {
                iMarkDepth--;
            }
        } else if (*p_cPos == JsonMark[iValueType][0]) {
            iMarkDepth++;
        }
        p_cPos++;
    }

    if (iValueType == JSTRING) {
        char *cp_pos = NULL;
        char *t_pos = NULL;

        while(str_pos_start < str_pos_end)
        {
            if (*str_pos_start == '\\'){
                unsigned char sequence_length = 1;
                if ((str_pos_end - str_pos_start) < 1){
                    return p_cValue + iValueLen + 1;
                }

                cp_pos = str_pos_start;
                while (cp_pos < (cp_pos + total_len - 1) && *cp_pos != '\0')
                {
                    t_pos = cp_pos + 1;
                    *cp_pos++ = *t_pos;
                }
                
                cp_pos = str_pos_start;
                switch (cp_pos[0])
                {
                    case 'b':
                        cp_pos[0] = '\b';
                        break;
                    case 'f':
                        cp_pos[0] = '\f';
                        break;
                    case 'n':
                        cp_pos[0] = '\n';
                        break;
                    case 'r':
                        cp_pos[0] = '\r';
                        break;
                    case 't':
                        cp_pos[0] = '\t';
                        break;
                    case '\"':
                    case '\\':
                    case '/':
                        cp_pos[0] = str_pos_start[1];
                        break;
                #if 0
                    /* UTF-16 literal */
                    case 'u':
                        sequence_length = utf16_literal_to_utf8(input_pointer, input_end, &output_pointer);
                        if (sequence_length == 0)
                        {
                            /* failed to convert UTF16-literal to UTF-8 */
                            goto fail;
                        }
                        break;
                #endif
                    default:
                        break;
                }
                iValueLen -= sequence_length;
            }
            str_pos_start++;
        }
    }

    if (type == JOBJECT) {
        *key = p_cName;
        *key_len = iNameLen;
    }

    *val = p_cValue;
    *val_len = iValueLen;
    *val_type = iValueType;
    if (iValueType == JSTRING) {
        return p_cValue + iValueLen + 1;
    } else {
        return p_cValue + iValueLen;
    }
}

int qm_json_parse_name_value(char *p_cJsonStr, int iStrLen, json_parse_cb pfnCB, void *p_CBData)
{
    int restore = 0;
    char    *pos = 0, *key = 0, *val = 0;
    int     klen = 0, vlen = 0, vtype = 0;
    char    last_char = 0;
    int     ret = JSON_RESULT_ERR;

    if (p_cJsonStr == NULL || iStrLen == 0 || pfnCB == NULL) {
        return ret;
    }

    if (iStrLen != strlen(p_cJsonStr)) {
        // QM_LOGD("LOG_TAG", "Backup last_char since %d != %d", iStrLen, (int)strlen(p_cJsonStr));
        restore = 1;
        backup_json_str_last_char(p_cJsonStr, iStrLen, last_char);
    }

    json_object_for_each_kv(p_cJsonStr, pos, key, klen, val, vlen, vtype) {
        if (key && klen && val && vlen) {
            ret = JSON_RESULT_OK;
            if (JSON_PARSE_FINISH == pfnCB(key, klen, val, vlen, vtype, p_CBData)) {
                break;
            }
        }
    }

    if (iStrLen == strlen(p_cJsonStr) && restore) {
        // QM_LOGD("LOG_TAG", "restore last_char", iStrLen, (int)strlen(p_cJsonStr));
        restore_json_str_last_char(p_cJsonStr, iStrLen, last_char);
    }

    return ret;
}

static int json_get_value_by_name_cb(char *p_cName, int iNameLen, char *p_cValue, int iValueLen, int iValueType,
                              void *p_CBData)
{
    JSON_NV     *p_stNameValue = (JSON_NV *)p_CBData;

#if (JSON_DEBUG == 1)
    int         i;

    if (p_cName) {
        json_debug("Name:");
        for (i = 0; i < iNameLen; i++) {
            json_debug("%c", *(p_cName + i));
        }
    }

    if (p_cValue) {
        json_debug("Value:");
        for (i = 0; i < iValueLen; i++) {
            json_debug("%c", *(p_cValue + i));
        }
    }
#endif

    if (!strncmp(p_cName, p_stNameValue->pN, p_stNameValue->nLen)) {
        p_stNameValue->pV = p_cValue;
        p_stNameValue->vLen = iValueLen;
        p_stNameValue->vType = iValueType;
        return JSON_PARSE_FINISH;
    } else {
        return JSON_PARSE_OK;
    }
}

char *qm_json_get_value_by_name(char *p_cJsonStr, int iStrLen, char *p_cName, int *p_iValueLen, int *p_iValueType)
{
    JSON_NV     stNV;

    memset(&stNV, 0, sizeof(stNV));
    stNV.pN = p_cName;
    stNV.nLen = strlen(p_cName);
    if (JSON_RESULT_OK == qm_json_parse_name_value(p_cJsonStr, iStrLen, json_get_value_by_name_cb, (void *)&stNV)) {
        if (p_iValueLen) {
            *p_iValueLen = stNV.vLen;
        }
        if (p_iValueType) {
            *p_iValueType = stNV.vType;
        }
    }
    return stNV.pV;
}

