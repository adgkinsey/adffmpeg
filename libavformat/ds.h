/***********************************************************************************
 * File: ds.h
 * Purpose: Provides an implementation of the network messaging protocol for DM
 * Digital Sprite video servers
 *
 * Author: Craig Shaw
 * Date: 18/05/07
 * Version: 0.1
 ***********************************************************************************/
#ifndef __DS_H__
#define __DS_H__

#include "ds_exports.h"

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

/* -------------------------------------- Structures/types -------------------------------------- */
typedef enum _controlMessages
{
    INVALID_MESSAGE_TYPE,            /* 0 */
    TCP_CLI_CONNECT,						/* 1 */    /* Initial connection */
    TCP_SRV_CONNECT_REJECT,				/* 2 */    /* Connection rejected */
    TCP_SRV_CONNECT_REPLY,				/* 3 */    /* Connect accepted - returns DSL configuration */
    TCP_CLI_IMG_LIVE_REQUEST,			/* 4 */    /* Request an image stream */
    TCP_CLI_IMG_PLAY_REQUEST,			/* 5 */	  /* Request a playback stream */
    TCP_SRV_IMG_DATA,						/* 6 */    /* Image stream data */
    TCP_SRV_NUDGE,							/* 7 */    /* Nudge */
    TCP_SRV_DISCONNECT,					/* 8 */    /* Server dropping connection  - ie: unit shutting down etc. */
    UDP_CLI_IMG_CONTROL_REQUEST,		/* 9 */    /* Request to change image stream ie: camera change */
    UDP_SRV_IMG_CONTROL_REPLY,			/* A */    /* Acknowledgement of request */
    UDP_CLI_SYSTEM_STATUS_REQUEST,	/* B */    /* Request for update of DSL configuration */
    UDP_SRV_SYSTEM_STATUS,				/* C */    /* New DSL configuration - may be sent by server if menus change */
    UDP_CLI_TELEMETRY_REQUEST,			/* D */    /* Telemetry star request */
    UDP_CLI_TELEMETRY_COMMAND,			/* E */    /* Telemetry command request */
    UDP_CLI_TELEMETRY_STATUS_REQUEST,/* F */    /* Request for telemetry status update */
    UDP_SRV_TELEMETRY_STATUS,			/* 10*/    /* Ack with new telemetry status */
    UDP_CLI_DISCONNECT,					/* 11*/    /* Polite message notifying of client disconnect */
    UDP_CLI_DISCOVER,						/* 12*/    /* Broadcast to identify units on subnet */
    UDP_SRV_HELLO,							/* 13*/    /* Reply containing basic unit information */
    UDP_CLI_MCI_COMMAND,					/* 14*/    /* Send MCI command */
    UDP_SRV_MCI_REPLY,					/* 15*/    /* Reply from MCI command */
    UDP_SRV_MSG_FAIL,						/* 16*/    /* UDP message recognition failure - returns status code */
    UDP_CLI_EVENT_FILTER_REQUEST,		/* 17*/    /* Request to initialise an event filter. */
    UDP_SRV_EVENT_FILTER_REPLY,		/* 18*/    /* Filter request either accepted, or not accepted. */
    UDP_CLI_EVENTS_REQUEST,				/* 19*/    /* Request to apply filter and retrieve events */
    UDP_SRV_EVENTS_REPLY,				/* 1A*/    /* Reply containing filtered event */
    UDP_CLI_ADMIN_REQUEST,			         /* 1B*/    /* Request from user for admin */
    UDP_SRV_ADMIN_REPLY,			      	   /* 1C*/    /* Reply - contains success or fail reason */
    UDP_CLI_USER_INFO_REQUEST,			      /* 1D*/    /* Request for information on a specific user */
    UDP_SRV_USER_INFO_REPLY,			      /* 1E*/    /* Information on the requested user */
    UDP_CLI_ADD_USER_REQUEST,			      /* 1F*/    /* Request to add a user to the system */
    UDP_SRV_ADD_USER_REPLY,				      /* 20*/    /* Reply to indicate success or failure */
    UDP_CLI_DELETE_USER_REQUEST,			      /* 21*/    /* Request to delete a user from the system */
    UDP_SRV_DELETE_USER_REPLY,				      /* 22*/    /* Reply to delete user request */
    UDP_CLI_CHANGE_PASSWORD_REQUEST,	      /* 23*/    /* Change password */
    UDP_SRV_CHANGE_PASSWORD_REPLY,		   /* 24*/    /* Reply to request */
    UDP_CLI_UPDATE_ACCESS_RIGHTS_REQUEST,  /* 25*/    /* Request to change users realms */
    UDP_SRV_UPDATE_ACCESS_RIGHTS_REPLY,	   /* 26*/    /* Reply to request */
    TCP_CLI_CHANGE_PASSWORD,               /* 27*/    /* send password change*/
    UDP_CLI_CHANGE_OWN_PASSWORD_REQUEST,	/* 28*/    /* Change own password */
    UDP_SRV_CHANGE_OWN_PASSWORD_REPLY,		/* 29*/    /* Reply to request */
    UDP_CLI_EMAIL_REQUEST,				      /* 2A*/    /* Request for email data */
    UDP_SRV_EMAIL_REPLY,				         /* 2B*/    /* Reply with email data */
    UDP_CLI_CHANGE_EMAIL_REQUEST,		      /* 2C*/    /* Request to set new email data */
    UDP_SRV_CHANGE_EMAIL_REPLY,			   /* 2D*/    /* Reply to setting new email data */
    UDP_CLI_CHANGE_SESSION_REQUEST,		   /* 2E*/    /* Request to logon to different unit*/
    TCP_CLI_IMG_DATA_REQUEST,					/* 2F*/    /* request from remote to grab image data */
    TCP_SRV_DATA_DATA,							/* 30*/    /* us sending requested images as data */
    TCP_SRV_NO_DATA,								/* 31*/    /* Sent when finished sending data */
    UDP_CLI_ABORT_DATA_REQUEST,				/* 32*/    /* Cancel data transfer */
    UDP_CLI_EVENT_DATA_REQUEST,				/* 33*/    /* Request to obtain end time of an event */
    UDP_SRV_EVENT_DATA_REPLY,					/* 34*/    /* reply */
    TCP_CLI_CONTROL_CONNECT,			/* 35*/    /* Initial connection for TCP control link */
    TCP_SRV_CONTROL_CONNECT_REJECT,
    TCP_SRV_CONTROL_CONNECT_REPLY,
    UDP_CLI_SERVICE_CHECK,						/* 38*/    /* Sent to check if UDP service working */
    UDP_SRV_SERVICE_CHECK_REPLY,
    UDP_CLI_DRIVE_DETAIL_REQUEST,				/* 3A*/    /* Request for hard drive details */
    UDP_SRV_DRIVE_DETAIL_REPLY,				/* 3B*/    /* Reply with data */
    UDP_CLI_DRIVE_SMART_REQUEST,				/* 3C*/    /* Request for hard drive S.M.A.R.T. details */
    UDP_SRV_DRIVE_SMART_REPLY,					/* 3D*/    /* Reply with S.M.A.R.T. data */
    UDP_CLI_DRIVE_LOG_REQUEST,					/* 3E*/    /* Request for hard drive log details */
    UDP_SRV_DRIVE_LOG_REPLY,					/* 3F*/    /* Reply with log data */
    UDP_CLI_DRIVE_TEST_REQUEST,				/* 40*/    /* Request for hard drive offline test */
    UDP_SRV_DRIVE_TEST_REPLY,					/* 41*/    /* Reply with confirmation of test */
    TCP_SRV_FEATURE_CONNECT_REPLY,			/* 42*/    /* Connect accepted - returns DSL configuration */
    TCP_CLI_ALM_CONNECT,                   /* 43*/    /* Initial alarm connection to PC */
    TCP_SRV_ALM_REJECT,							/* 44*/    /* reject message with reasons not registered, auth required, invalid password, busy etc*/
    TCP_SRV_ALM_ACCEPT,							/* 45*/    /* Client connection accepted - send and alarm msg */
    TCP_CLI_ALM_MSG,								/* 46*/    /* Alarm details */
    TCP_SRV_ALM_ACK,								/* 47*/    /* Server ack of an alarm message */
    UDP_CLI_CONFIG_REQUEST,						/* 48*/    /* Request name/value pairs from unit Get/Send*/
    UDP_SRV_CONFIG_REJECT,						/* 49*/    /* Server denied access to config */
    UDP_CLI_CONFIG_ITEM,                   /* 4A*/    /* has item x of y to determine missing and last item */
    UDP_SRV_CONFIG_ITEM,
    UDP_CLI_RELAY_REQUEST,
    UDP_SRV_RELAY_REPLY,
    UDP_CLI_CHANGE_ACTIVE_FEATURES,
    UDP_SRV_ACTIVE_FEATURES_REPLY,
    UDP_CLI_POS_FILTER_REQUEST,            /* 50*/    /* POS keyworldsearch filter */
    UDP_SRV_POS_TICK,                      /* 51*/    /* sends during search to say still searching */
    UDP_SRV_POS_FILTER_REPLY,              /* 52*/    /* reply to filter */
    UDP_CLI_POS_LINES_REQUEST,             /* 53*/    /* Request for POS lines */
    UDP_SRV_POS_LINES_REPLY,               /* 54*/    /* Reply with POS matches */
    UDP_CLI_POS_DATA_REQUEST,              /* 55*/    /* Request for info on a specific POS event */
    UDP_SRV_POS_DATA_REPLY,                /* 56*/    /* Replies with start & end time etc */
    NET_NUMBER_OF_MESSAGE_TYPES			/* 57*/    /* ALWAYS KEEP AS LAST ITEMS */
} ControlMessageTypes;

typedef struct _dsContext
{
    URLContext *        TCPContext; /* Context of the underlying TCP network connection */
} DSContext;

typedef struct _messageHeader
{
    unsigned long       magicNumber;
    unsigned long       length;
    long                channelID;
    long                sequence;
    unsigned long       messageVersion;
    long                checksum;
    long                messageType;
} MessageHeader;
#define SIZEOF_MESSAGE_HEADER_IO                28      /* Size in bytes of the MessageHeader structure. Can't use sizeof to read/write one of these to network as structure packing may differ based on platform */

typedef struct _networkMessage
{
    MessageHeader       header;
    void *              body;
} NetworkMessage;

typedef enum _imgControlMsgType
{
    IMG_LIVE,
    IMG_PLAY,
    IMG_GOTO,
    IMG_DATA,
    IMG_STOP
} ImgControlMsgType;

typedef struct _clientConnectMsg
{
    unsigned long           udpPort;
    long                    connectType;        /* ImgControlMsgType enum. long used to avoid sizeof(enum) discrepancies */
    char                    userID[MAX_USER_ID_LENGTH];
    char                    accessKey[ACCESS_KEY_LENGTH];
} ClientConnectMsg;
#define VER_TCP_CLI_CONNECT                     0x00000003
#define SIZEOF_TCP_CLI_CONNECT_IO               (MAX_USER_ID_LENGTH + ACCESS_KEY_LENGTH + 8)      /* Size in bytes of the MessageHeader structure. Can't use sizeof to read/write one of these to network as structure packing may differ based on platform */

typedef enum _netRejectReason
{
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

typedef struct _srvConnectRejectMsg
{
    long					reason;				/* enum NET_REJECT_REASON */
    long					timestamp;
    char					macAddr[MAC_ADDR_LENGTH];
    unsigned long           appVersion;
    unsigned long           minViewerVersion;
} SrvConnectRejectMsg;
#define VER_TCP_SRV_CONNECT_REJECT          0x00000001

typedef enum _realmType
{
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

typedef struct _srvConnectReplyMsg
{
    long					numCameras;
    long					viewableCamMask;
    long					telemetryCamMask;
    long					failedCamMask;
    long					maxMsgInterval;
    int64_t			        timestamp;                          /* TODO: Verify - this was a 'hyper long' before. Assuming 64 bit value. Is this correct? Is solution portable? */
    char					cameraTitles[16][28];
    long                    unitType;
    unsigned long           applicationVersion;
    long                    videoStandard;
    char                    macAddr[MAC_ADDR_LENGTH];
    char                    unitName[UNIT_NAME_LENGTH];
    long					numFixedRealms;	                    /* Number of FIXED system realms */
    unsigned long		    realmFlags[NUM_FIXED_REALMS];	    /* Indicates if user is in realm. */
    unsigned long           minimumViewerVersion;
} SrvConnectReplyMsg;
#define VER_TCP_SRV_CONNECT_REPLY           0x00000001

typedef struct _srvFeatureConnectReplyMsg
{
    long					numCameras;
    long					viewableCamMask;
    long					telemetryCamMask;
    long					failedCamMask;
    long					maxMsgInterval;
    int64_t			        timestamp;                          /* TODO: Verify - this was a 'hyper long' before. Assuming 64 bit value. Is this correct? Is solution portable? */
    char					cameraTitles[16][28];
    long                    unitType;
    unsigned long           applicationVersion;
    long                    videoStandard;
    char                    macAddr[MAC_ADDR_LENGTH];
    char                    unitName[UNIT_NAME_LENGTH];

    unsigned long           minimumViewerVersion;
    unsigned long 		    unitFeature01;
    unsigned long 		    unitFeature02;
    unsigned long 		    unitFeature03;
    unsigned long 		    unitFeature04;
    long					numFixedRealms;	               /* Number of FIXED system realms */
    unsigned long		    realmFlags[NUM_FIXED_REALMS];	   /* Indicates if user is in realm. */
} SrvFeatureConnectReplyMsg;
#define VER_TCP_SRV_FEATURE_CONNECT_REPLY   0x00000002

typedef struct _cliImgLiveRequestMsg
{
	long					cameraMask;
	long					resolution;
} CliImgLiveRequestMsg;
#define VER_TCP_CLI_IMG_LIVE_REQUEST        0x00000001
#define SIZEOF_TCP_CLI_IMG_LIVE_REQUEST_IO  8            /* Size in bytes of the MessageHeader structure. Can't use sizeof to read/write one of these to network as structure packing may differ based on platform */

typedef enum _vcrMode
{
	VM_PLAY,
    VM_VIS_REW,
    VM_VIS_FF,
    VM_STOP,
    VM_PLAY_SHUTTLE,
	VM_FINISH
} vcrMode;

typedef struct _cliImgPlayRequestMsg
{
	long					cameraMask;
	long					mode;			    /*	(enum VCR_MODE) */
	long					pace;
	int64_t				    fromTime;		    /*		(time_u)	*/
	int64_t				    toTime;		        /*		(time_u)	*/
} CliImgPlayRequestMsg;
#define VER_TCP_CLI_IMG_PLAY_REQUEST        0x00000001
#define SIZEOF_TCP_CLI_IMG_PLAY_REQUEST_IO  28            /* Size in bytes of the MessageHeader structure. Can't use sizeof to read/write one of these to network as structure packing may differ based on platform */

#ifdef WORDS_BIGENDIAN
#define NTOH64(x)               (x)
#define HTON64(x)               (x)

#define NTOH32(x)               (x)
#define HTON32(x)               (x)

#define NTOH16(x)               (x)
#define HTON16(x)               (x)
#else
#define NTOH64(x)               (av_bswap64(x))
#define HTON64(x)               (av_bswap64(x))

#define NTOH32(x)               (av_bswap32(x))
#define HTON32(x)               (av_bswap32(x))

#define NTOH16(x)               (av_bswap16(x))
#define HTON16(x)               (av_bswap16(x))

#endif /* WORDS_BIGENDIAN */

/* -------------------------------------- Function declarations -------------------------------------- */
extern NetworkMessage *     CreateNetworkMessage( ControlMessageTypes messageType, long channelID );
extern void                 FreeNetworkMessage( NetworkMessage **message );
extern void                 NToHMessageHeader( MessageHeader *header );


#endif /* __DS_H__ */
