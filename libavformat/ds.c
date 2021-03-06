/*
 * Protocol for AD-Holdings Digital Sprite stream format
 * Copyright (c) 2006-2010 AD-Holdings plc
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <strings.h>

#include "avformat.h"
#include "internal.h"
#include "url.h"
#include "libavutil/avstring.h"
#include "libavutil/bswap.h"

#include "adffmpeg_errors.h"
#include "dsenc.h"
#include "ds.h"

/* -------------------------------------- Constants -------------------------------------- */
#define DS_DEFAULT_PORT                             8234                /* Deafult TCP and UDP port for comms */
#define DS_HEADER_MAGIC_NUMBER                      0xFACED0FF
#define MAX_USER_ID_LENGTH                          30
#define ACCESS_KEY_LENGTH                           36
#define MAC_ADDR_LENGTH                             16
#define UNIT_NAME_LENGTH                            32

#define DS_WRITE_BUFFER_SIZE                        1024
#define DS_READ_BUFFER_SIZE                         1024

#define DS_PLAYBACK_MODE_LIVE                       0x00
#define DS_PLAYBACK_MODE_PLAY                       0x01

#define DS_RESOLUTION_HI                            2
#define DS_RESOLUTION_MED                           1
#define DS_RESOLUTION_LOW                           0


#define SET_FLAG_ZONE_UNKNOWN(flags)    ((flags) |= 0x80)

/* -------------------------------------- Structures/types -------------------------------------- */
typedef struct _dsContext {
    URLContext *        TCPContext; /* Context of the underlying TCP network connection */
} DSContext;

typedef struct _networkMessage {
    MessageHeader       header;
    void *              body;
} NetworkMessage;

typedef enum _imgControlMsgType {
    IMG_LIVE,
    IMG_PLAY,
    IMG_GOTO,
    IMG_DATA,
    IMG_STOP
} ImgControlMsgType;

typedef struct _clientConnectMsg {
    unsigned long           udpPort;
    long                    connectType;        /* ImgControlMsgType enum. long used to avoid sizeof(enum) discrepancies */
    char                    userID[MAX_USER_ID_LENGTH];
    char                    accessKey[ACCESS_KEY_LENGTH];
} ClientConnectMsg;
#define VER_TCP_CLI_CONNECT                     0x00000003
#define SIZEOF_TCP_CLI_CONNECT_IO               (MAX_USER_ID_LENGTH + ACCESS_KEY_LENGTH + 8)      /* Size in bytes of the MessageHeader structure. Can't use sizeof to read/write one of these to network as structure packing may differ based on platform */

typedef enum _netRejectReason {
    REJECT_BUSY,
    REJECT_INVALID_USER_ID,
    REJECT_AUTHENTIFICATION_REQUIRED,
    REJECT_AUTHENTIFICATION_INVALID,
    REJECT_UNAUTHORISED,
    REJECT_OTHER,
    REJECT_PASSWORD_CHANGE_REQUIRED,
    REJECT_OUT_OF_MEMORY,
    REJECT_CORRUPT_USERS_FILES
} NetRejectReason;

typedef struct _srvConnectRejectMsg {
    long                    reason;                /* enum NET_REJECT_REASON */
    long                    timestamp;
    char                    macAddr[MAC_ADDR_LENGTH];
    unsigned long           appVersion;
    unsigned long           minViewerVersion;
} SrvConnectRejectMsg;
#define VER_TCP_SRV_CONNECT_REJECT          0x00000001

typedef enum _realmType {
    REALM_LIVE,
    REALM_PLAYBACK,
    REALM_TELEM,
    REALM_EVENTS,
    REALM_ADMIN,
    REALM_PASSWORD,
    REALM_PW_ONCE,
    REALM_VIEW_ALL,
    REALM_MCI,
    REALM_FILE_EXPORT,
    REALM_WEB,
    REALM_POS,
    NUM_FIXED_REALMS,
} RealmType;

typedef struct _srvConnectReplyMsg {
    long                    numCameras;
    long                    viewableCamMask;
    long                    telemetryCamMask;
    long                    failedCamMask;
    long                    maxMsgInterval;
    int64_t                    timestamp;                          /* TODO: Verify - this was a 'hyper long' before. Assuming 64 bit value. Is this correct? Is solution portable? */
    char                    cameraTitles[16][28];
    long                    unitType;
    unsigned long           applicationVersion;
    long                    videoStandard;
    char                    macAddr[MAC_ADDR_LENGTH];
    char                    unitName[UNIT_NAME_LENGTH];
    long                    numFixedRealms;                        /* Number of FIXED system realms */
    unsigned long            realmFlags[NUM_FIXED_REALMS];        /* Indicates if user is in realm. */
    unsigned long           minimumViewerVersion;
} SrvConnectReplyMsg;
#define VER_TCP_SRV_CONNECT_REPLY           0x00000001

typedef struct _srvFeatureConnectReplyMsg {
    long                    numCameras;
    long                    viewableCamMask;
    long                    telemetryCamMask;
    long                    failedCamMask;
    long                    maxMsgInterval;
    int64_t                    timestamp;                          /* TODO: Verify - this was a 'hyper long' before. Assuming 64 bit value. Is this correct? Is solution portable? */
    char                    cameraTitles[16][28];
    long                    unitType;
    unsigned long           applicationVersion;
    long                    videoStandard;
    char                    macAddr[MAC_ADDR_LENGTH];
    char                    unitName[UNIT_NAME_LENGTH];

    unsigned long           minimumViewerVersion;
    unsigned long             unitFeature01;
    unsigned long             unitFeature02;
    unsigned long             unitFeature03;
    unsigned long             unitFeature04;
    long                    numFixedRealms;                   /* Number of FIXED system realms */
    unsigned long            realmFlags[NUM_FIXED_REALMS];       /* Indicates if user is in realm. */
} SrvFeatureConnectReplyMsg;
#define VER_TCP_SRV_FEATURE_CONNECT_REPLY   0x00000002

typedef struct _cliImgLiveRequestMsg {
    long                    cameraMask;
    long                    resolution;
} CliImgLiveRequestMsg;
#define VER_TCP_CLI_IMG_LIVE_REQUEST        0x00000001
#define SIZEOF_TCP_CLI_IMG_LIVE_REQUEST_IO  8            /* Size in bytes of the MessageHeader structure. Can't use sizeof to read/write one of these to network as structure packing may differ based on platform */

typedef enum _vcrMode {
    VM_PLAY,
    VM_VIS_REW,
    VM_VIS_FF,
    VM_STOP,
    VM_PLAY_SHUTTLE,
    VM_FINISH
} vcrMode;

typedef struct _cliImgPlayRequestMsg {
    long                    cameraMask;
    long                    mode;                /*    (enum VCR_MODE) */
    long                    pace;
    int64_t                    fromTime;            /*        (time_u)    */
    int64_t                    toTime;                /*        (time_u)    */
} CliImgPlayRequestMsg;
#define VER_TCP_CLI_IMG_PLAY_REQUEST        0x00000001
#define SIZEOF_TCP_CLI_IMG_PLAY_REQUEST_IO  28            /* Size in bytes of the MessageHeader structure. Can't use sizeof to read/write one of these to network as structure packing may differ based on platform */


/* -------------------------------------- Local function declarations -------------------------------------- */
static NetworkMessage *     CreateNetworkMessage( ControlMessageTypes messageType, long channelID );
static void                 FreeNetworkMessage( NetworkMessage **message );

static int DSOpen( URLContext *h, const char *uri, int flags );
static int DSRead( URLContext *h, uint8_t *buf, int size );
static int DSWrite( URLContext *h, const uint8_t *buf, int size );
static int DSClose( URLContext *h );
static int DSConnect( URLContext *h, const char *path, const char *hoststr, const char *auth );
static inline int MessageSize( const NetworkMessage *message );

static int ReceiveNetworkMessage( URLContext *h, NetworkMessage **message );
static int ReadNetworkMessageHeader( URLContext *h, MessageHeader *header );
static int ReadNetworkMessageBody( URLContext * h, NetworkMessage *message );

static int SendNetworkMessage( URLContext *h, NetworkMessage *message );
static void HToNMessageHeader( MessageHeader *header, unsigned char *buf );

static int ReadConnectRejectMessage( URLContext * h, NetworkMessage *message );
static int ReadConnectReplyMessage( URLContext * h, NetworkMessage *message );
static int ReadFeatureConnectReplyMessage( URLContext * h, NetworkMessage *message );

static int GetUserAndPassword( const char * auth, char *user, char *password  );
static int CrackURI( const char *path, int *streamType, int *res, int *cam, time_t *from, time_t *to, int *rate, vcrMode *playMode );
static int DSReadBuffer( URLContext *h, uint8_t *buffer, int size );
static int64_t TimeTolong64( time_t time );
static void NToHMessageHeader( MessageHeader *header );

#if HAVE_BIGENDIAN
#define HTON64(x)               x
#define HTON32(x)               x
#else
#define HTON64(x)               (av_bswap64(x))
#define HTON32(x)               (av_bswap32(x))
#endif


/****************************************************************************************************************
 * Function: CreateNetworkMessage
 * Desc: Allocates and initialises a NetworkMessage for sending to the server based on the given type and channel.
 *       It is the caller's responsibility to release the NetworkMessage created by this function. This should be
 *       done with the FreeNetworkMessage function.
 * Params:
 *  messageType - Type of the message to create
 *  channelID - Channel for which the message is intended
 * Return:
 *   Pointer to new network message on success. NULL on failure
 ****************************************************************************************************************/
static NetworkMessage * CreateNetworkMessage( ControlMessageTypes messageType, long channelID )
{
    NetworkMessage *        newMessage = NULL;
    int                     length = 0;
    int                     version = 0;

    /* Create a new message structure */
    newMessage = av_mallocz( sizeof(NetworkMessage) );

    if( newMessage != NULL ) {
        /* Now allocate the body structure if we've been successful */
        switch( messageType ) {
                /* In normal cases, set whatever member variables we can in here also */
            case TCP_CLI_CONNECT: {
                if( (newMessage->body = av_mallocz( sizeof(ClientConnectMsg) )) != NULL ) {
                    length = SIZEOF_TCP_CLI_CONNECT_IO;
                    version = VER_TCP_CLI_CONNECT;
                }
                else
                    goto fail;
            }
            break;

            case TCP_CLI_IMG_LIVE_REQUEST: {
                if( (newMessage->body = av_mallocz( sizeof(CliImgLiveRequestMsg) )) != NULL ) {
                    length = SIZEOF_TCP_CLI_IMG_LIVE_REQUEST_IO;
                    version = VER_TCP_CLI_IMG_LIVE_REQUEST;
                }
                else
                    goto fail;
            }
            break;

            case TCP_CLI_IMG_PLAY_REQUEST: {
                if( (newMessage->body = av_mallocz( sizeof(CliImgPlayRequestMsg) )) != NULL ) {
                    length = SIZEOF_TCP_CLI_IMG_PLAY_REQUEST_IO;
                    version = VER_TCP_CLI_IMG_PLAY_REQUEST;
                }
                else
                    goto fail;
            }
            break;

            default: { /* Unknown (or unsupported) message type encountered */
                goto fail;
            }
            break;
        }

        /* Set whatever header values we can in here */
        newMessage->header.messageType = messageType;
        newMessage->header.magicNumber = DS_HEADER_MAGIC_NUMBER;
        newMessage->header.channelID = channelID;
        newMessage->header.sequence = 0; /* Currently unsupported at server */
        newMessage->header.checksum = 0; /* As suggested in protocol documentation */
        newMessage->header.messageVersion = version;

        /* Set the length of the remaining data (size of the message header - the magic number + size of the message body) */
        newMessage->header.length = SIZEOF_MESSAGE_HEADER_IO - sizeof(unsigned long) + length;
    }

    return newMessage;

fail:
    /* Release whatever may have been allocated */
    FreeNetworkMessage( &newMessage );

    return NULL;
}

/****************************************************************************************************************
 * Function: FreeNetworkMessage
 * Desc: Releases the resources associated with a network message created with CreateNetworkMessage()
 * Params:
 *  message - Address of a pointer to a NetworkMessage struct allocated with CreateNetworkMessage
 * Return:
 ****************************************************************************************************************/
static void FreeNetworkMessage( NetworkMessage **message )
{
    /* Simply cascade free all memory allocated */
    if( *message ) {
        if( (*message)->body ) {
            av_free( (*message)->body );
        }

        av_free( *message );
        *message = NULL;
    }
}

/****************************************************************************************************************
 * Function: DSOpen
 * Desc: Opens a connection to a DM Digital Sprite video server and executes the initial connect transaction
 * Params:
 *  h - Pointer to URLContext struct used to store all connection info associated with this connection
 *  uri - The URI indicating the server's location
 *  flags - Flags indicating the type of connection required (URL_WRONLY, etc)
 * Return:
 *   0 on success, non 0 on failure
 ****************************************************************************************************************/
static int DSOpen( URLContext *h, const char *uri, int flags )
{
    char            hostname[1024], hoststr[1024];
    char            auth[1024];
    char            path1[1024];
    char            buf[1024];
    int             port, err;
    const char *    path;
    URLContext *    TCPContext = NULL;
    DSContext *     s = NULL;

    h->is_streamed = 1;

    s = av_malloc( sizeof(DSContext) );
    if (!s) {
        return -ENOMEM;
    }
    h->priv_data = s;

    /* Crack the URL */
    av_url_split( NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port, path1, sizeof(path1), uri );

    if (port > 0) {
        snprintf( hoststr, sizeof(hoststr), "%s:%d", hostname, port );
    }
    else {
        av_strlcpy( hoststr, hostname, sizeof(hoststr) );
    }

    /* Add the URL parameters (if any) */
    if (path1[0] == '\0') {
        path = "/";
    }
    else {
        path = path1;
    }

    /* Assume default port for this protocol if one isn't supplied */
    if (port < 0) {
        port = DS_DEFAULT_PORT;
    }

    /* Form the appropriate TCP URL */
    snprintf(buf, sizeof(buf), "tcp://%s:%d", hostname, port);

    /* Now open a connection to that TCP address */
    if( (err = ffurl_open(&TCPContext, buf, AVIO_FLAG_READ_WRITE, &TCPContext->interrupt_callback, NULL)) < 0 ) {
        goto fail;
    }

    /* Save the TCP context */
    s->TCPContext = TCPContext;

    /* Now initiate connection using the DS protocol */
    if( (err = DSConnect( h, path, hoststr, auth )) < 0 ) {
        goto fail;
    }

    return 0;
fail:
    if( TCPContext ) {
        ffurl_close( TCPContext );
    }

    av_free( s );

    return err;
}

/****************************************************************************************************************
 * Function: DSRead
 * Desc: Reads data from the connection
 * Params:
 *  h - Pointer to URLContext struct used to store all connection info associated with this connection
 *  buf - Buffer to which read data will be written
 *  size - Number of bytes to read into buf
 * Return:
 *   0 on success, non 0 on failure
 ****************************************************************************************************************/
static int DSRead( URLContext *h, uint8_t *buf, int size )
{
    /* All we need to do in here is call the generic read function on our underlying TCP connection */
    DSContext *         context = (DSContext *)h->priv_data;

    if( context != NULL )
        return ffurl_read( context->TCPContext, buf, size );

    return AVERROR(EIO);
}

/****************************************************************************************************************
 * Function: DSWrite
 * Desc: Writes data to the connection
 * Params:
 *  h - Pointer to URLContext struct used to store all connection info associated with this connection
 *  buf - Buffer from which data will be written
 *  size - Number of bytes to write from buf
 * Return:
 *   0 on success, non 0 on failure
 ****************************************************************************************************************/
static int DSWrite( URLContext *h, const uint8_t *buf, int size )
{
    /* All we need to do in here is call the generic write function on our underlying TCP connection */
    DSContext *     context = (DSContext *)h->priv_data;

    if( context != NULL )
        return ffurl_write( context->TCPContext, buf, size );

    return AVERROR(EIO);
}

/****************************************************************************************************************
 * Function: DSClose
 * Desc: Closes the connection to the DM Digital Sprite video server
 * Params:
 *  h - Pointer to URLContext struct used to store all connection info associated with this connection
 * Return:
 *   0 on success, non 0 on failure
 ****************************************************************************************************************/
static int DSClose( URLContext *h )
{
    DSContext * context = (DSContext *)h->priv_data;

    if( context != NULL ) {
        ffurl_close( context->TCPContext );

        av_free( context );
    }

    return 0;
}

/****************************************************************************************************************
 * Function: MessageSize
 * Desc: Calculates the size in bytes of the given NetworkMessage
 * Params:
 *  message - Pointer to the message for which the size is required
 * Return:
 *   The size of the message, -1 if no message passed
 ****************************************************************************************************************/
static inline int MessageSize( const NetworkMessage *message )
{
    if( message )
        return message->header.length + sizeof(unsigned long);

    return -1;
}

/****************************************************************************************************************
 * Function: DSConnect
 * Desc: Attempts to connect to the DM Digital Sprite video server according to its connection protocol
 * Params:
 *  h - Pointer to URLContext struct used to store all connection info associated with this connection
 *  path - TODO: Need to confirm whether these params are actually needed
 *  hoststr -
 *  auth - Authentication credentials
 * Return:
 *   0 on success, non 0 on failure
 ****************************************************************************************************************/
static int DSConnect( URLContext *h, const char *path, const char *hoststr, const char *auth )
{
    NetworkMessage *    sendMessage = NULL;
    NetworkMessage *    recvMessage = NULL;
    int                 retVal = 0;
    int                 isConnecting = 1;
    char *              encCredentials = NULL;
    ClientConnectMsg *  connectMsg = NULL;
    int                 channelID = -2;
    int                 streamType = 0, res = 0, cam = 0;
    time_t              from = 0, to = 0;
    int                 rate = 0;
    vcrMode             playMode = VM_PLAY;
    char                user[MAX_USER_ID_LENGTH];
    char                password[MAX_USER_ID_LENGTH];

    /* Initialise the user and password fields */
    memset( user, 0, sizeof(char) * MAX_USER_ID_LENGTH );
    memset( password, 0, sizeof(char) * MAX_USER_ID_LENGTH );

    /* Extract the username and password */
    if( (retVal = GetUserAndPassword( auth, user, password )) == 0 ) {
        /* Crack the URI to get the control parameters */
        retVal = CrackURI( path, &streamType, &res, &cam, &from, &to, &rate, &playMode );
    }

    while( isConnecting && retVal == 0 ) {
        if( (sendMessage = CreateNetworkMessage( TCP_CLI_CONNECT, channelID )) == NULL ) {
            if( encCredentials != NULL )
                av_free( encCredentials );

            return AVERROR(ENOMEM);
        }

        /* Set up the connection request */
        /* Set the message body up now */
        connectMsg = (ClientConnectMsg *)sendMessage->body;

        connectMsg->udpPort = DS_DEFAULT_PORT;

        if( streamType == DS_PLAYBACK_MODE_PLAY )
            connectMsg->connectType = IMG_PLAY;
        else
            connectMsg->connectType = IMG_LIVE;

        if( encCredentials != NULL ) {
            memcpy( connectMsg->userID, user, strlen(user) );
            memcpy( connectMsg->accessKey, encCredentials, strlen(encCredentials) );
        }

        /* Send the message to the server */
        if( (retVal = SendNetworkMessage( h, sendMessage )) >= 0 ) {
            /* Receive the response */
            if( (retVal = ReceiveNetworkMessage( h, &recvMessage )) >= 0 ) {
                switch( recvMessage->header.messageType ) {
                    case TCP_SRV_CONNECT_REJECT: { /* We expect this first time */
                        /* Extract the info we need to encrypt */
                        SrvConnectRejectMsg     * msg = (SrvConnectRejectMsg *)recvMessage->body;

                        /* What was the reason for the failure? */
                        if( msg->reason == REJECT_AUTHENTIFICATION_REQUIRED ) {
                            channelID = recvMessage->header.channelID;

                            /* Encrypt the username / password */
                            if( strlen( user ) > 0 && strlen( password ) > 0 ) {
                                if( (encCredentials = EncryptPasswordString( user, password, msg->timestamp, msg->macAddr, msg->appVersion )) == NULL )
                                    retVal = AVERROR(ENOMEM);
                            }
                            else {
                                /* If we haven't got a user and password string then we have to notify the client */
                                retVal = ADFFMPEG_DS_ERROR_AUTH_REQUIRED;
                            }
                        }
                        else if( msg->reason == REJECT_AUTHENTIFICATION_INVALID ) { /* Supplied credentials are invalid */
                            retVal = ADFFMPEG_DS_ERROR_INVALID_CREDENTIALS;
                        }
                        else { /* Fail */
                            retVal = AVERROR(EIO);
                            isConnecting = 0;
                        }
                    }
                    break;

                    case TCP_SRV_FEATURE_CONNECT_REPLY:
                    case TCP_SRV_CONNECT_REPLY: {
                        /* Great, we're connected - we just need to send a IMG_LIVE_REQUEST to the server to start the streaming */
                        NetworkMessage *        imgRequestMsg = NULL;

                        if( streamType == DS_PLAYBACK_MODE_LIVE ) {
                            if( (imgRequestMsg = CreateNetworkMessage( TCP_CLI_IMG_LIVE_REQUEST, channelID )) ) {
                                CliImgLiveRequestMsg *      msgBody = (CliImgLiveRequestMsg *)imgRequestMsg->body;

                                msgBody->cameraMask = cam;
                                msgBody->resolution = res;
                            }
                        }
                        else if( streamType == DS_PLAYBACK_MODE_PLAY ) {
                            if( (imgRequestMsg = CreateNetworkMessage( TCP_CLI_IMG_PLAY_REQUEST, channelID )) ) {
                                CliImgPlayRequestMsg *      msgBody = (CliImgPlayRequestMsg *)imgRequestMsg->body;

                                msgBody->cameraMask = cam;
                                msgBody->fromTime = TimeTolong64( from );
                                msgBody->toTime = TimeTolong64( to );
                                msgBody->pace = rate;
                                msgBody->mode = playMode;
                            }
                        }
                        else
                            retVal = AVERROR(EIO);

                        if( retVal == 0 ) {
                            /* Fire the request message off */
                            retVal = SendNetworkMessage( h, imgRequestMsg );
                        }

                        isConnecting = 0;
                    }
                    break;

                    /* Anything other than a connect reply is failure */
                    default: {
                        retVal = -1;
                        isConnecting = 0;
                    }
                    break;
                }
            }
            else
                isConnecting = 0;
        }
        else
            isConnecting = 0;


        /* We can release the messages now */
        FreeNetworkMessage( &sendMessage );
        FreeNetworkMessage( &recvMessage );
    }

    return retVal;
}

static int GetUserAndPassword( const char * auth, char *user, char *password  )
{
    int             retVal = 0;
    char *          authStr = NULL;
    char *          token = NULL;
    const char *    delim = ":";
    int             count = 0;

    if( auth != NULL ) {
        if( strcmp( auth, "" ) != 0 ) {
            retVal = AVERROR_INVALIDDATA;

            /* Copy the string as strtok needs to modify as it goes */
            if( (authStr = (char*)av_mallocz( strlen(auth) + 1 )) != NULL ) {
                strcpy( authStr, auth );

                /* Now split it into the user and password */
                token = strtok( authStr, delim );

                while( token != NULL ) {
                    count++;

                    if( count == 1 ) {
                        if( strlen(token) <= MAX_USER_ID_LENGTH ) {
                            strcpy( user, token );

                            if( strlen(token) < MAX_USER_ID_LENGTH )
                                user[strlen(token)] = '\0';
                        }
                    }
                    else if( count == 2 ) {
                        /* TODO: Verify whether checking against the length of the max user id is ok. Ultimately, the password is hashed before transmission
                           so the length is not imperative here. Maybe server defines a maximum length though? */
                        if( strlen(token) <= MAX_USER_ID_LENGTH ) {
                            strcpy( password, token );

                            if( strlen(token) < MAX_USER_ID_LENGTH )
                                password[strlen(token)] = '\0';

                            retVal = 0;
                        }
                    }
                    else { /* There shouldn't be more than 2 tokens, better flag an error */
                        retVal = AVERROR_INVALIDDATA;
                    }

                    token = strtok( NULL, delim );
                }

                av_free( authStr );
                authStr = NULL;
            }
            else
                retVal = AVERROR(ENOMEM);
        }
    }

    return retVal;
}

static int CrackURI( const char *path, int *streamType, int *res, int *cam, time_t *from, time_t *to, int *rate, vcrMode *playMode )
{
    int             retVal = AVERROR_INVALIDDATA;
    const char *    delim = "?&";
    char *          pathStr = NULL;
    char *          token = NULL;

    *res = DS_RESOLUTION_HI;
    *streamType = DS_PLAYBACK_MODE_LIVE;
    *cam = 1;

    if( path != NULL ) {
        /* Take a copy of the path string so that strtok can modify it */
        if( (pathStr = (char*)av_mallocz( strlen(path) + 1 )) != NULL ) {
            strcpy( pathStr, path );

            retVal = 0;

            token = strtok( pathStr, delim );

            while( token != NULL ) {
                char *          name = NULL;
                char *          value = NULL;

                /* Now look inside this token string for a name value pair separated by an = */
                if( (value = strstr(token, "=")) != NULL ) {
                    value++;

                    name = token;
                    name[(value-1) - token] = '\0';

                    if( name != NULL && value != NULL ) {
                        /* Which parameter have we got? */
                        if( av_strcasecmp(name, "res" ) == 0 ) {
                            if( strcmp( value, "hi" ) == 0 )
                                *res = DS_RESOLUTION_HI;
                            else if( strcmp( value, "med" ) == 0 )
                                *res = DS_RESOLUTION_MED;
                            else if( strcmp( value, "low" ) == 0 )
                                *res = DS_RESOLUTION_LOW;
                        }
                        else if( av_strcasecmp(name, "stream" ) == 0 ) {
                            if( av_strcasecmp(value, "live" ) == 0 )
                                *streamType = DS_PLAYBACK_MODE_LIVE;
                            else if( av_strcasecmp(value, "play" ) == 0 )
                                *streamType = DS_PLAYBACK_MODE_PLAY;
                        }
                        else if( av_strcasecmp(name, "cam" ) == 0 ) {
                            *cam = atoi( value );
                        }
                        else if( av_strcasecmp(name, "from" ) == 0 ) {
                            *from = (time_t)atoi( value );
                        }
                        else if( av_strcasecmp(name, "to" ) == 0 ) {
                            *to = (time_t)atoi( value );
                        }
                        else if( av_strcasecmp(name, "rate" ) == 0 ) {
                            *rate = atoi( value );
                        }
                        else if( av_strcasecmp(name, "mode" ) == 0 ) {
                            if( av_strcasecmp(value, "play" ) == 0 )
                                *playMode = VM_PLAY;
                            else if( av_strcasecmp(value, "rwd" ) == 0 )
                                *playMode = VM_VIS_REW;
                            else if( av_strcasecmp(value, "fwd" ) == 0 )
                                *playMode = VM_VIS_FF;
                            else if( av_strcasecmp(value, "stop" ) == 0 )
                                *playMode = VM_STOP;
                            else if( av_strcasecmp(value, "shuttle" ) == 0 )
                                *playMode = VM_PLAY_SHUTTLE;
                            else if( av_strcasecmp(value, "finish" ) == 0 )
                                *playMode = VM_FINISH;
                        }
                    }
                }

                token = strtok( NULL, delim );
            }

            av_free( pathStr );
            pathStr = NULL;
        }
        else
            retVal = AVERROR(ENOMEM);
    }

    return retVal;
}

/****************************************************************************************************************
 * Function: ReceiveNetworkMessage
 * Desc: Reads the next NetworkMessage from the connection. New message is allocated and must be release by the
 *       caller with FreeNetworkMessage()
 * Params:
 *  h - Pointer to URLContext struct used to store all connection info associated with this connection
 *  message - Address of pointer to network message. This will be allocated by this function
 * Return:
 *   0 on success, non 0 on failure
 ****************************************************************************************************************/
static int ReceiveNetworkMessage( URLContext *h, NetworkMessage **message )
{
    int     retVal = 0;

    /* Allocate a new NetworkMessage struct */
    *message = av_mallocz( sizeof(NetworkMessage) );

    if( *message == NULL )
        return AVERROR(ENOMEM);

    if( (retVal = ReadNetworkMessageHeader( h, &(*message)->header )) != 0 ) {
        FreeNetworkMessage( message );
        return retVal;
    }

    if( (retVal = ReadNetworkMessageBody( h, *message )) != 0 ) {
        FreeNetworkMessage( message );
        return retVal;
    }

    return 0;
}

static int DSReadBuffer( URLContext *h, uint8_t *buffer, int size )
{
    int         ret;
    int         totalRead = 0;

    if( buffer != NULL && size > 0 ) {
        while( size - totalRead != 0 ) {
            ret = DSRead( h, buffer, size - totalRead );

            if( ret < 0 )
                return ret;
            else
                totalRead += ret;
        }
    }

    return totalRead;
}

static int ReadNetworkMessageHeader( URLContext *h, MessageHeader *header )
{
    /* Read the header in a piece at a time... */
    if( DSReadBuffer( h, (uint8_t *)&header->magicNumber, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&header->length, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&header->channelID, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&header->sequence, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&header->messageVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&header->checksum, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&header->messageType, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    /* Now adjust the endianess */
    NToHMessageHeader( header );

    return 0;
}

static void NToHMessageHeader( MessageHeader *header )
{
    if( header ) {
        header->magicNumber = av_be2ne32(header->magicNumber);
        header->length = av_be2ne32(header->length);
        header->channelID = av_be2ne32(header->channelID);
        header->sequence = av_be2ne32(header->sequence);
        header->messageVersion = av_be2ne32(header->messageVersion);
        header->checksum = av_be2ne32(header->checksum);
        header->messageType = av_be2ne32(header->messageType);
    }
}

static int ReadNetworkMessageBody( URLContext * h, NetworkMessage *message )
{
    int         retVal = 0;

    if( message != NULL && message->body == NULL ) {
        /* Read based on the type of message we have */
        switch( message->header.messageType ) {
            case TCP_SRV_CONNECT_REJECT: {
                retVal = ReadConnectRejectMessage( h, message );
            }
            break;

            case TCP_SRV_FEATURE_CONNECT_REPLY: {
                retVal = ReadFeatureConnectReplyMessage( h, message );
            }
            break;

            case TCP_SRV_CONNECT_REPLY: {
                retVal = ReadConnectReplyMessage( h, message );
            }
            break;

            default:
                /* We shouldn't get into this state so we'd better return an error here... */
                retVal = AVERROR(EIO);
                break;
        }

    }

    return retVal;
}

static int ReadConnectRejectMessage( URLContext * h, NetworkMessage *message )
{
    SrvConnectRejectMsg *       bodyPtr = NULL;

    /* Allocate the message body */
    if( (message->body = av_malloc( sizeof(SrvConnectRejectMsg) )) == NULL )
        return AVERROR(ENOMEM);

    /* Now read from the stream into the message */
    bodyPtr = (SrvConnectRejectMsg *)message->body;

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->reason, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->timestamp, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->macAddr, MAC_ADDR_LENGTH ) != MAC_ADDR_LENGTH )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->appVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->minViewerVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    /* Correct the byte ordering */
    bodyPtr->reason = av_be2ne32(bodyPtr->reason);
    bodyPtr->timestamp = av_be2ne32(bodyPtr->timestamp);
    bodyPtr->appVersion = av_be2ne32(bodyPtr->appVersion);
    bodyPtr->minViewerVersion = av_be2ne32(bodyPtr->minViewerVersion);

    return 0;
}

static int ReadConnectReplyMessage( URLContext * h, NetworkMessage *message )
{
    SrvConnectReplyMsg *        bodyPtr = NULL;

    /* Allocate memory in which to store the message body */
    if( (message->body = av_malloc( sizeof(SrvConnectReplyMsg) )) == NULL )
        return AVERROR(ENOMEM);

    bodyPtr = (SrvConnectReplyMsg *)message->body;

    /* Now read the message body, a field at a time */
    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->numCameras, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->viewableCamMask, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->telemetryCamMask, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->failedCamMask, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->maxMsgInterval, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->timestamp, sizeof(int64_t) ) != sizeof(int64_t) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->cameraTitles, (16 * 28) ) != (16 * 28) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->unitType, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->applicationVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->videoStandard, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->macAddr, MAC_ADDR_LENGTH ) != MAC_ADDR_LENGTH )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->unitName, UNIT_NAME_LENGTH ) != UNIT_NAME_LENGTH )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->numFixedRealms, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    bodyPtr->numFixedRealms = av_be2ne32(bodyPtr->numFixedRealms);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->realmFlags, (sizeof(unsigned long) * bodyPtr->numFixedRealms) ) != (sizeof(unsigned long) * bodyPtr->numFixedRealms) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->minimumViewerVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    /* Correct the byte ordering */
    bodyPtr->numCameras = av_be2ne32(bodyPtr->numCameras);
    bodyPtr->viewableCamMask = av_be2ne32(bodyPtr->viewableCamMask);
    bodyPtr->telemetryCamMask = av_be2ne32(bodyPtr->telemetryCamMask);
    bodyPtr->failedCamMask = av_be2ne32(bodyPtr->failedCamMask);
    bodyPtr->maxMsgInterval = av_be2ne32(bodyPtr->maxMsgInterval);
    bodyPtr->unitType = av_be2ne32(bodyPtr->unitType);
    bodyPtr->applicationVersion = av_be2ne32(bodyPtr->applicationVersion);
    bodyPtr->videoStandard = av_be2ne32(bodyPtr->videoStandard);
    bodyPtr->numFixedRealms = av_be2ne32(bodyPtr->numFixedRealms);
    bodyPtr->minimumViewerVersion = av_be2ne32(bodyPtr->minimumViewerVersion);

    return 0;
}

static int ReadFeatureConnectReplyMessage( URLContext * h, NetworkMessage *message )
{
    SrvFeatureConnectReplyMsg *        bodyPtr = NULL;

    if( (message->body = av_malloc( sizeof(SrvFeatureConnectReplyMsg) )) == NULL )
        return AVERROR(ENOMEM);

    bodyPtr = (SrvFeatureConnectReplyMsg *)message->body;

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->numCameras, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->viewableCamMask, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->telemetryCamMask, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->failedCamMask, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->maxMsgInterval, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->timestamp, sizeof(int64_t) ) != sizeof(int64_t) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->cameraTitles, (16 * 28) ) != (16 * 28) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->unitType, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->applicationVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->videoStandard, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->macAddr, MAC_ADDR_LENGTH ) != MAC_ADDR_LENGTH )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->unitName, UNIT_NAME_LENGTH ) != UNIT_NAME_LENGTH )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->minimumViewerVersion, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->unitFeature01, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->unitFeature02, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->unitFeature03, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->unitFeature04, sizeof(unsigned long) ) != sizeof(unsigned long) )
        return AVERROR(EIO);

    if( DSReadBuffer( h, (uint8_t *)&bodyPtr->numFixedRealms, sizeof(long) ) != sizeof(long) )
        return AVERROR(EIO);

    bodyPtr->numFixedRealms = av_be2ne32(bodyPtr->numFixedRealms);

    if( DSReadBuffer( h, (uint8_t *)bodyPtr->realmFlags, (sizeof(unsigned long) * bodyPtr->numFixedRealms) ) != (sizeof(unsigned long) * bodyPtr->numFixedRealms) )
        return AVERROR(EIO);

    /* Correct the byte ordering */
    bodyPtr->numCameras = av_be2ne32(bodyPtr->numCameras);
    bodyPtr->viewableCamMask = av_be2ne32(bodyPtr->viewableCamMask);
    bodyPtr->telemetryCamMask = av_be2ne32(bodyPtr->telemetryCamMask);
    bodyPtr->failedCamMask = av_be2ne32(bodyPtr->failedCamMask);
    bodyPtr->maxMsgInterval = av_be2ne32(bodyPtr->maxMsgInterval);
    bodyPtr->unitType = av_be2ne32(bodyPtr->unitType);
    bodyPtr->applicationVersion = av_be2ne32(bodyPtr->applicationVersion);
    bodyPtr->videoStandard = av_be2ne32(bodyPtr->videoStandard);
    bodyPtr->minimumViewerVersion = av_be2ne32(bodyPtr->minimumViewerVersion);
    bodyPtr->unitFeature01 = av_be2ne32(bodyPtr->unitFeature01);
    bodyPtr->unitFeature02 = av_be2ne32(bodyPtr->unitFeature02);
    bodyPtr->unitFeature03 = av_be2ne32(bodyPtr->unitFeature03);
    bodyPtr->unitFeature04 = av_be2ne32(bodyPtr->unitFeature04);

    return 0;
}

static void HToNMessageHeader( MessageHeader *header, unsigned char *buf )
{
    MessageHeader           tempHeader;
    int                     bufIdx = 0;

    if( header != NULL && buf != NULL ) {
        /* Set whatever header values we can in here */
        tempHeader.magicNumber = av_be2ne32(header->magicNumber);
        memcpy( &buf[bufIdx], &tempHeader.magicNumber, sizeof(unsigned long) );
        bufIdx += sizeof(unsigned long);

        tempHeader.length = av_be2ne32(header->length);
        memcpy( &buf[bufIdx], &tempHeader.length, sizeof(unsigned long) );
        bufIdx += sizeof(unsigned long);

        tempHeader.channelID = av_be2ne32(header->channelID);
        memcpy( &buf[bufIdx], &tempHeader.channelID, sizeof(long) );
        bufIdx += sizeof(long);

        tempHeader.sequence = av_be2ne32(header->sequence); /* Currently unsupported at server */
        memcpy( &buf[bufIdx], &tempHeader.sequence, sizeof(long) );
        bufIdx += sizeof(long);

        tempHeader.messageVersion = av_be2ne32(header->messageVersion);
        memcpy( &buf[bufIdx], &tempHeader.messageVersion, sizeof(unsigned long) );
        bufIdx += sizeof(unsigned long);

        tempHeader.checksum = av_be2ne32(header->checksum); /* As suggested in protocol documentation */
        memcpy( &buf[bufIdx], &tempHeader.checksum, sizeof(long) );
        bufIdx += sizeof(long);

        tempHeader.messageType = av_be2ne32(header->messageType);
        memcpy( &buf[bufIdx], &tempHeader.messageType, sizeof(long) );
        bufIdx += sizeof(long);
    }
}

/****************************************************************************************************************
 * Function: SendNetworkMessage
 * Desc: Sends the given message over the connection.
 * Params:
 *  h - Pointer to URLContext struct used to store all connection info associated with this connection
 *  message - Pointer to the message to be sent
 * Return:
 *   0 on success, non 0 on failure
 ****************************************************************************************************************/
static int SendNetworkMessage( URLContext *h, NetworkMessage *message )
{
    unsigned char       messageBuffer[DS_WRITE_BUFFER_SIZE];
    int                 bufIdx = SIZEOF_MESSAGE_HEADER_IO;

    /* 0 the buffer */
    memset( messageBuffer, 0, DS_WRITE_BUFFER_SIZE );

    /* Write the header into the buffer */
    HToNMessageHeader( &message->header, messageBuffer );

    /* Now write the rest of the message to the buffer based on its type */
    switch( message->header.messageType ) {
        case TCP_CLI_CONNECT: {
            ClientConnectMsg        tempMsg;

            tempMsg.udpPort = HTON32(((ClientConnectMsg *)message->body)->udpPort);
            memcpy( &messageBuffer[bufIdx], &tempMsg.udpPort, sizeof(unsigned long) );
            bufIdx += sizeof(unsigned long);

            tempMsg.connectType = HTON32(((ClientConnectMsg *)message->body)->connectType);
            memcpy( &messageBuffer[bufIdx], &tempMsg.connectType, sizeof(long) );
            bufIdx += sizeof(long);

            memcpy( &messageBuffer[bufIdx], ((ClientConnectMsg *)message->body)->userID, MAX_USER_ID_LENGTH );
            bufIdx += MAX_USER_ID_LENGTH;

            memcpy( &messageBuffer[bufIdx], ((ClientConnectMsg *)message->body)->accessKey, ACCESS_KEY_LENGTH );
            bufIdx += ACCESS_KEY_LENGTH;
        }
        break;

        case TCP_CLI_IMG_LIVE_REQUEST: {
            long            temp;

            temp = HTON32( ((CliImgLiveRequestMsg*)message->body)->cameraMask );
            memcpy( &messageBuffer[bufIdx], &temp, sizeof(long) );
            bufIdx += sizeof(long);

            temp = HTON32( ((CliImgLiveRequestMsg*)message->body)->resolution );
            memcpy( &messageBuffer[bufIdx], &temp, sizeof(long) );
            bufIdx += sizeof(long);
        }
        break;

        case TCP_CLI_IMG_PLAY_REQUEST: {
            long            temp;
            int64_t          tempBig;

            temp = HTON32( ((CliImgPlayRequestMsg*)message->body)->cameraMask );
            memcpy( &messageBuffer[bufIdx], &temp, sizeof(long) );
            bufIdx += sizeof(long);

            temp = HTON32( ((CliImgPlayRequestMsg*)message->body)->mode );
            memcpy( &messageBuffer[bufIdx], &temp, sizeof(long) );
            bufIdx += sizeof(long);

            temp = HTON32( ((CliImgPlayRequestMsg*)message->body)->pace );
            memcpy( &messageBuffer[bufIdx], &temp, sizeof(long) );
            bufIdx += sizeof(long);

            tempBig = HTON64( ((CliImgPlayRequestMsg*)message->body)->fromTime );
            memcpy( &messageBuffer[bufIdx], &tempBig, sizeof(int64_t) );
            bufIdx += sizeof(int64_t);

            tempBig = HTON64( ((CliImgPlayRequestMsg*)message->body)->toTime );
            memcpy( &messageBuffer[bufIdx], &tempBig, sizeof(int64_t) );
            bufIdx += sizeof(int64_t);
        }
        break;
    }

    /* Write to output stream - remember to add on the 4 bytes for the magic number which precedes the length */
    return DSWrite( h, messageBuffer, message->header.length + sizeof(unsigned long) );
}

static int64_t TimeTolong64( time_t time )
{
    int64_t          timeOut = 0;
    unsigned short  flags = 0;
    unsigned short  ms = 0;
    uint8_t *       bufPtr = NULL;

    /* For now, we're saying we don't know the time zone */
    SET_FLAG_ZONE_UNKNOWN(flags);

    bufPtr = (uint8_t*)&timeOut;

    memcpy( bufPtr, &flags, sizeof(unsigned short) );
    bufPtr += sizeof(unsigned short);

    memcpy( bufPtr, &ms, sizeof(unsigned short) );
    bufPtr += sizeof(unsigned short);

    memcpy( bufPtr, &time, sizeof(time_t) );

    return timeOut;
}


URLProtocol ff_dm_protocol = {
    .name                = "dm",
    .url_open            = DSOpen,
    .url_read            = DSRead,
    .url_write           = DSWrite,
    .url_close           = DSClose,
};
