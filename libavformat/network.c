#include "network.h"

#ifdef __MINGW32__

int init_winsock( void )
{
    WSADATA         wsadata;
    WORD            version = MAKEWORD(2,2);

    int retVal = WSAStartup( version, &wsadata );

    return (retVal == 0)?retVal:-1;
}

void close_winsock( void )
{
    int ret = 0;

    ret = WSACleanup();
}

#endif /* __MINGW32__ */