
#include "qm.h"

#include "bson/bson.h"

static const char *TAG = "main";

void qm_application_start(void)
{
    uint8_t binary[16] = {0};
    bson_t parent = BSON_INITIALIZER;

    BSON_APPEND_INT32(&parent, "pid", 10290);
    BSON_APPEND_INT32(&parent, "roleid", 1);
    BSON_APPEND_INT32(&parent, "audioId", 1);
    BSON_APPEND_INT32(&parent, "audioDataLength", 16);

    for (int i = 0; i < sizeof(binary); i++)
    {
        binary[i] = i;
    }
    
    BSON_APPEND_BINARY(&parent, "audioData", BSON_SUBTYPE_BINARY, binary, sizeof(binary));

    char *str = bson_as_relaxed_extended_json (&parent, NULL);
    QM_LOGD (TAG, "BSON:%s\n", str); // Prints: { "pid" : 10290, "roleid" : 1, "audioId" : 1, "audioDataLength" : 16, "audioData" : { "$binary" : { "base64" : "AAECAwQFBgcICQoLDA0ODw==", "subType" : "00" } } }
    QM_HEX_LOGD(TAG, "HEX BSON ", parent.padding, parent.len);
    bson_free (str);
    bson_destroy (&parent);
}
