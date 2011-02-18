#ifndef __UTP_H__
#define __UTP_H__

#include "utypes.h"

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#else
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct UTPSocket;

// Used to set sockopt on a uTP socket to set the version of uTP
// to use for outgoing connections. This can only be called before
// the uTP socket is connected
#define SO_UTPVERSION 99

enum {
	// socket has reveived syn-ack (notification only for outgoing connection completion)
	// this implies writability
	UTP_STATE_CONNECT = 1,

	// socket is able to send more data
	UTP_STATE_WRITABLE = 2,

	// connection closed
	UTP_STATE_EOF = 3,

	// socket is being destroyed, meaning all data has been sent if possible.
	// it is not valid to refer to the socket after this state change occurs
	UTP_STATE_DESTROYING = 4,
};

// Callbacks called by a uTP socket (register with UTP_SetCallbacks)

// The uTP socket layer calls this when bytes have been received from the network.
typedef void UTPOnReadProc(void *userdata, const byte *bytes, size_t count);

// The uTP socket layer calls this to fill the outgoing buffer with bytes.
// The uTP layer takes responsibility that those bytes will be delivered.
typedef void UTPOnWriteProc(void *userdata, byte *bytes, size_t count);

// The uTP socket layer calls this to retrieve number of bytes currently in read buffer
typedef size_t UTPGetRBSize(void *userdata);

// The uTP socket layer calls this whenever the socket becomes writable.
typedef void UTPOnStateChangeProc(void *userdata, int state);

// The uTP socket layer calls this when an error occurs on the socket.
// These errors currently include ECONNREFUSED, ECONNRESET and ETIMEDOUT, but
// could eventually include any BSD socket error.
typedef void UTPOnErrorProc(void *userdata, int errcode);

// The uTP socket layer calls this to report overhead statistics
typedef void UTPOnOverheadProc(void *userdata, bool send, size_t count, int type);

struct UTPFunctionTable {
	UTPOnReadProc *on_read;
	UTPOnWriteProc *on_write;
	UTPGetRBSize *get_rb_size;
	UTPOnStateChangeProc *on_state;
	UTPOnErrorProc *on_error;
	UTPOnOverheadProc *on_overhead;
};


// The uTP socket layer calls this when a new incoming uTP connection is established
// this implies writability
typedef void UTPGotIncomingConnection(void *userdata, struct UTPSocket* s);

// The uTP socket layer calls this to send UDP packets
typedef void SendToProc(void *userdata, const byte *p, size_t len, const struct sockaddr *to, socklen_t tolen);


// Functions which can be called with a uTP socket

// Create a uTP socket
struct UTPSocket *UTP_Create(SendToProc *send_to_proc, void *send_to_userdata,
					  const struct sockaddr *addr, socklen_t addrlen);

// Setup the callbacks - must be done before connect or on incoming connection
void UTP_SetCallbacks(struct UTPSocket *socket, struct UTPFunctionTable *func, void *userdata);

// Valid options include SO_SNDBUF, SO_RCVBUF and SO_UTPVERSION
bool UTP_SetSockopt(struct UTPSocket *socket, int opt, int val);

// Try to connect to a specified host.
void UTP_Connect(struct UTPSocket *socket);

// Process a UDP packet from the network. This will process a packet for an existing connection,
// or create a new connection and call incoming_proc. Returns true if the packet was processed
// in some way, false if the packet did not appear to be uTP.
bool UTP_IsIncomingUTP(UTPGotIncomingConnection *incoming_proc,
					   SendToProc *send_to_proc, void *send_to_userdata,
					   const byte *buffer, size_t len, const struct sockaddr *to, socklen_t tolen);

// Process an ICMP received UDP packet.
bool UTP_HandleICMP(const byte* buffer, size_t len, const struct sockaddr *to, socklen_t tolen);

// Write bytes to the uTP socket.
// Returns true if the socket is still writable.
bool UTP_Write(struct UTPSocket *socket, size_t count);

// Notify the uTP socket of buffer drain
void UTP_RBDrained(struct UTPSocket *socket);

// Call periodically to process timeouts and other periodic events
void UTP_CheckTimeouts(void);

// Retrieves the peer address of the specified socket, stores this address in the
// sockaddr structure pointed to by the addr argument, and stores the length of this
// address in the object pointed to by the addrlen argument.
void UTP_GetPeerName(struct UTPSocket *socket, struct sockaddr *addr, socklen_t *addrlen);

void UTP_GetDelays(struct UTPSocket *socket, int32 *ours, int32 *theirs, uint32 *age);

size_t UTP_GetPacketSize(struct UTPSocket *socket);

#ifdef _DEBUG
struct UTPStats {
	uint64 _nbytes_recv;	// total bytes received
	uint64 _nbytes_xmit;	// total bytes transmitted
	uint32 _rexmit;		// retransmit counter
	uint32 _fastrexmit;	// fast retransmit counter
	uint32 _nxmit;		// transmit counter
	uint32 _nrecv;		// receive counter (total)
	uint32 _nduprecv;	// duplicate receive counter
};

// Get stats for UTP socket
void UTP_GetStats(struct UTPSocket *socket, UTPStats *stats);
#endif

// Close the UTP socket.
// It is not valid to issue commands for this socket after it is closed.
// This does not actually destroy the socket until outstanding data is sent, at which
// point the socket will change to the UTP_STATE_DESTROYING state.
void UTP_Close(struct UTPSocket *socket);

struct UTPGlobalStats {
	uint32 _nraw_recv[5];	// total packets recieved less than 300/600/1200/MTU bytes fpr all connections (global)
	uint32 _nraw_send[5];	// total packets sent less than 300/600/1200/MTU bytes for all connections (global)
};

void UTP_GetGlobalStats(struct UTPGlobalStats *stats);

#ifdef __cplusplus
}
#endif

#endif //__UTP_H__
