
#include "iot_guider.h"

#ifndef IOT_WITHOUT_TLS
static const char *iot_ca_crt = \
{
    \
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIID7TCCAtWgAwIBAgIUW2hy+Z8GXqsduppLsPQn++BCtPswDQYJKoZIhvcNAQEL\r\n"
    "BQAwgYUxCzAJBgNVBAYTAkNOMREwDwYDVQQIDAhaaGVqaWFuZzERMA8GA1UEBwwI\r\n"
    "SGFuZ3pob3UxDTALBgNVBAoMBEVNUVgxDTALBgNVBAsMBEVNUVgxCzAJBgNVBAMM\r\n"
    "AkNBMSUwIwYJKoZIhvcNAQkBFhZ6aGFvbGlqdW5AeGlhb2ppYW5nLmNjMB4XDTIx\r\n"
    "MDQyMTEyNDQyNFoXDTMxMDQxOTEyNDQyNFowgYUxCzAJBgNVBAYTAkNOMREwDwYD\r\n"
    "VQQIDAhaaGVqaWFuZzERMA8GA1UEBwwISGFuZ3pob3UxDTALBgNVBAoMBEVNUVgx\r\n"
    "DTALBgNVBAsMBEVNUVgxCzAJBgNVBAMMAkNBMSUwIwYJKoZIhvcNAQkBFhZ6aGFv\r\n"
    "bGlqdW5AeGlhb2ppYW5nLmNjMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\r\n"
    "AQEAyf2wjINoQ5l1sxjP9LDaJ03gqR9ixdAT555NweJCeE5Hyh0gaNIzI1VJ5V6m\r\n"
    "E9M2lexgGoaheE2mdZJ+XnrZnYpdWTFjs3Lr1lUyxPQs/OlH0aXvYBMrfZvP2+Xi\r\n"
    "RYY1kaEsN2VZhEF9uo7eK7d6vQ41KAHiJFiiYUJVh86ZXaHx7xBYmCuGEDqLkUoz\r\n"
    "UydCvuALp1ruu9mT/iE1k5qFiETk+1EwzkaVFmVFoX/AA9iB53RpDgx6KfNsG/ao\r\n"
    "GCwglYmO7wo4ZmzEGrG252jExu60rCXx3v8JJO+ut91V3mL0kiLZAwhmvUudQBNv\r\n"
    "XKrWHNderoUUXabysIuD3W/GjQIDAQABo1MwUTAdBgNVHQ4EFgQUgvfBWoG4qUok\r\n"
    "gW0f4f1O3TsuFhEwHwYDVR0jBBgwFoAUgvfBWoG4qUokgW0f4f1O3TsuFhEwDwYD\r\n"
    "VR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAHIq1ZmJYxBvqTwxanKIe\r\n"
    "umDNdku+QsPVwZ3X6uYtek5vrlsJcoRkFve4UL2G6xSkQKSwGQ14zKPooUKOtgVi\r\n"
    "EkgoYM5A/vAk6jFB0PQVr0RKVpyT2sSwhOY4EUmMnStGcErA2KyACjKMNiSiEGBd\r\n"
    "Ns5tLZEvjKrbXh9kRdCjBpb+0Y0UQtDY+JJ3v3+J0r/xARPiteZW7omzg0/OsYR6\r\n"
    "ETLzDB2S1G735jQ0vOlGx9Ri3ljumMLiyGAeGTb12F2GOSzX5ljmvZsBRe3PJLmk\r\n"
    "b3Ja4jUtfQFO5hn15Z40BxYWwPb9g3nB8cQltnnoZsA0CEmlbMlRfocqN1yjPcgI\r\n"
    "lg==\r\n"
    "-----END CERTIFICATE-----"

};


#endif  /* #ifndef IOT_WITHOUT_TLS */




const char *iot_ca_get(void)
{
#ifdef IOT_WITHOUT_TLS
    return NULL;
#else
    return iot_ca_crt;
#endif
}
