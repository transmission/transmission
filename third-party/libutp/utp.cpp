#include <StdAfx.h>

#include "utp.h"
#include "templates.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h> // for UINT_MAX

#ifdef WIN32
#include "win32_inet_ntop.h"

// newer versions of MSVC define these in errno.h
#ifndef ECONNRESET
#define ECONNRESET WSAECONNRESET
#define EMSGSIZE WSAEMSGSIZE
#define ECONNREFUSED WSAECONNREFUSED
#define ETIMEDOUT WSAETIMEDOUT
#endif
#endif

#ifdef POSIX
typedef sockaddr_storage SOCKADDR_STORAGE;
#endif // POSIX

// number of bytes to increase max window size by, per RTT. This is
// scaled down linearly proportional to off_target. i.e. if all packets
// in one window have 0 delay, window size will increase by this number.
// Typically it's less. TCP increases one MSS per RTT, which is 1500
#define MAX_CWND_INCREASE_BYTES_PER_RTT 3000
#define CUR_DELAY_SIZE 3
// experiments suggest that a clock skew of 10 ms per 325 seconds
// is not impossible. Reset delay_base every 13 minutes. The clock
// skew is dealt with by observing the delay base in the other
// direction, and adjusting our own upwards if the opposite direction
// delay base keeps going down
#define DELAY_BASE_HISTORY 13
#define MAX_WINDOW_DECAY 100 // ms

#define REORDER_BUFFER_SIZE 32
#define REORDER_BUFFER_MAX_SIZE 511
#define OUTGOING_BUFFER_MAX_SIZE 511

#define PACKET_SIZE 350

// this is the minimum max_window value. It can never drop below this
#define MIN_WINDOW_SIZE 10

// when window sizes are smaller than one packet_size, this
// will pace the packets to average at the given window size
// if it's not set, it will simply not send anything until
// there's a timeout
#define USE_PACKET_PACING 1

// if we receive 4 or more duplicate acks, we resend the packet
// that hasn't been acked yet
#define DUPLICATE_ACKS_BEFORE_RESEND 3

#define DELAYED_ACK_BYTE_THRESHOLD 2400 // bytes
#define DELAYED_ACK_TIME_THRESHOLD 100 // milliseconds

#define RST_INFO_TIMEOUT 10000
#define RST_INFO_LIMIT 1000
// 29 seconds determined from measuring many home NAT devices
#define KEEPALIVE_INTERVAL 29000


#define SEQ_NR_MASK 0xFFFF
#define ACK_NR_MASK 0xFFFF

#define DIV_ROUND_UP(num, denom) ((num + denom - 1) / denom)

#include "utp_utils.h"
#include "utp_config.h"

#define LOG_UTP if (g_log_utp) utp_log
#define LOG_UTPV if (g_log_utp_verbose) utp_log

uint32 g_current_ms;

// The totals are derived from the following data:
//  45: IPv6 address including embedded IPv4 address
//  11: Scope Id
//   2: Brackets around IPv6 address when port is present
//   6: Port (including colon)
//   1: Terminating null byte
char addrbuf[65];
char addrbuf2[65];
#define addrfmt(x, s) x.fmt(s, sizeof(s))

#if (defined(__SVR4) && defined(__sun))
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

struct PACKED_ATTRIBUTE PackedSockAddr {

	// The values are always stored here in network byte order
	union {
		byte _in6[16];		// IPv6
		uint16 _in6w[8];	// IPv6, word based (for convenience)
		uint32 _in6d[4];	// Dword access
		in6_addr _in6addr;	// For convenience
	} _in;

	// Host byte order
	uint16 _port;

#define _sin4 _in._in6d[3]	// IPv4 is stored where it goes if mapped

#define _sin6 _in._in6
#define _sin6w _in._in6w
#define _sin6d _in._in6d

	byte get_family() const
	{
		return (IN6_IS_ADDR_V4MAPPED(&_in._in6addr) != 0) ? AF_INET : AF_INET6;
	}

	bool operator==(const PackedSockAddr& rhs) const
	{
		if (&rhs == this)
			return true;
		if (_port != rhs._port)
			return false;
		return memcmp(_sin6, rhs._sin6, sizeof(_sin6)) == 0;
	}
	bool operator!=(const PackedSockAddr& rhs) const { return !(*this == rhs); }

	PackedSockAddr(const SOCKADDR_STORAGE* sa, socklen_t len)
	{
		if (sa->ss_family == AF_INET) {
			assert(len >= sizeof(sockaddr_in));
			const sockaddr_in *sin = (sockaddr_in*)sa;
			_sin6w[0] = 0;
			_sin6w[1] = 0;
			_sin6w[2] = 0;
			_sin6w[3] = 0;
			_sin6w[4] = 0;
			_sin6w[5] = 0xffff;
			_sin4 = sin->sin_addr.s_addr;
			_port = ntohs(sin->sin_port);
		} else {
			assert(len >= sizeof(sockaddr_in6));
			const sockaddr_in6 *sin6 = (sockaddr_in6*)sa;
			_in._in6addr = sin6->sin6_addr;
			_port = ntohs(sin6->sin6_port);
		}
	}

	SOCKADDR_STORAGE get_sockaddr_storage(socklen_t *len = NULL) const
	{
		SOCKADDR_STORAGE sa;
		const byte family = get_family();
		if (family == AF_INET) {
			sockaddr_in *sin = (sockaddr_in*)&sa;
			if (len) *len = sizeof(sockaddr_in);
			memset(sin, 0, sizeof(sockaddr_in));
			sin->sin_family = family;
			sin->sin_port = htons(_port);
			sin->sin_addr.s_addr = _sin4;
		} else {
			sockaddr_in6 *sin6 = (sockaddr_in6*)&sa;
			memset(sin6, 0, sizeof(sockaddr_in6));
			if (len) *len = sizeof(sockaddr_in6);
			sin6->sin6_family = family;
			sin6->sin6_addr = _in._in6addr;
			sin6->sin6_port = htons(_port);
		}
		return sa;
	}

	cstr fmt(str s, size_t len) const
	{
		memset(s, 0, len);
		const byte family = get_family();
		str i;
		if (family == AF_INET) {
			inet_ntop(family, (uint32*)&_sin4, s, len);
			i = s;
			while (*++i) {}
		} else {
			i = s;
			*i++ = '[';
			inet_ntop(family, (in6_addr*)&_in._in6addr, i, len-1);
			while (*++i) {}
			*i++ = ']';
		}
		snprintf(i, len - (i-s), ":%u", _port);
		return s;
	}
} ALIGNED_ATTRIBUTE(4);

struct PACKED_ATTRIBUTE RST_Info {
	PackedSockAddr addr;
	uint32 connid;
	uint32 timestamp;
	uint16 ack_nr;
};

// these packet sizes are including the uTP header wich
// is either 20 or 23 bytes depending on version
#define PACKET_SIZE_EMPTY_BUCKET 0
#define PACKET_SIZE_EMPTY 23
#define PACKET_SIZE_SMALL_BUCKET 1
#define PACKET_SIZE_SMALL 373
#define PACKET_SIZE_MID_BUCKET 2
#define PACKET_SIZE_MID 723
#define PACKET_SIZE_BIG_BUCKET 3
#define PACKET_SIZE_BIG 1400
#define PACKET_SIZE_HUGE_BUCKET 4

struct PACKED_ATTRIBUTE PacketFormat {
	// connection ID
	uint32_big connid;
	uint32_big tv_sec;
	uint32_big tv_usec;
	uint32_big reply_micro;
	// receive window size in PACKET_SIZE chunks
	byte windowsize;
	// Type of the first extension header
	byte ext;
	// Flags
	byte flags;
	// Sequence number
	uint16_big seq_nr;
	// Acknowledgment number
	uint16_big ack_nr;
};

struct PACKED_ATTRIBUTE PacketFormatAck {
	PacketFormat pf;
	byte ext_next;
	byte ext_len;
	byte acks[4];
};

struct PACKED_ATTRIBUTE PacketFormatExtensions {
	PacketFormat pf;
	byte ext_next;
	byte ext_len;
	byte extensions[8];
};

struct PACKED_ATTRIBUTE PacketFormatV1 {
	// packet_type (4 high bits)
	// protocol version (4 low bits)
	byte ver_type;
	byte version() const { return ver_type & 0xf; }
	byte type() const { return ver_type >> 4; }
	void set_version(byte v) { ver_type = (ver_type & 0xf0) | (v & 0xf); }
	void set_type(byte t) { ver_type = (ver_type & 0xf) | (t << 4); }

	// Type of the first extension header
	byte ext;
	// connection ID
	uint16_big connid;
	uint32_big tv_usec;
	uint32_big reply_micro;
	// receive window size in bytes
	uint32_big windowsize;
	// Sequence number
	uint16_big seq_nr;
	// Acknowledgment number
	uint16_big ack_nr;
};

struct PACKED_ATTRIBUTE PacketFormatAckV1 {
	PacketFormatV1 pf;
	byte ext_next;
	byte ext_len;
	byte acks[4];
};

struct PACKED_ATTRIBUTE PacketFormatExtensionsV1 {
	PacketFormatV1 pf;
	byte ext_next;
	byte ext_len;
	byte extensions[8];
};

#if (defined(__SVR4) && defined(__sun))
#pragma pack(0)
#else
#pragma pack(pop)
#endif

enum {
	ST_DATA = 0,		// Data packet.
	ST_FIN = 1,			// Finalize the connection. This is the last packet.
	ST_STATE = 2,		// State packet. Used to transmit an ACK with no data.
	ST_RESET = 3,		// Terminate connection forcefully.
	ST_SYN = 4,			// Connect SYN
	ST_NUM_STATES,		// used for bounds checking
};

static const cstr flagnames[] = {
	"ST_DATA","ST_FIN","ST_STATE","ST_RESET","ST_SYN"
};

enum CONN_STATE {
	CS_IDLE = 0,
	CS_SYN_SENT = 1,
	CS_CONNECTED = 2,
	CS_CONNECTED_FULL = 3,
	CS_GOT_FIN = 4,
	CS_DESTROY_DELAY = 5,
	CS_FIN_SENT = 6,
	CS_RESET = 7,
	CS_DESTROY = 8,
};

static const cstr statenames[] = {
	"IDLE","SYN_SENT","CONNECTED","CONNECTED_FULL","GOT_FIN","DESTROY_DELAY","FIN_SENT","RESET","DESTROY"
};

struct OutgoingPacket {
	size_t length;
	size_t payload;
	uint64 time_sent; // microseconds
	uint transmissions:31;
	bool need_resend:1;
	byte data[1];
};

void no_read(void *socket, const byte *bytes, size_t count) {}
void no_write(void *socket, byte *bytes, size_t count) {}
size_t no_rb_size(void *socket) { return 0; }
void no_state(void *socket, int state) {}
void no_error(void *socket, int errcode) {}
void no_overhead(void *socket, bool send, size_t count, int type) {}

UTPFunctionTable zero_funcs = {
	&no_read,
	&no_write,
	&no_rb_size,
	&no_state,
	&no_error,
	&no_overhead,
};

struct SizableCircularBuffer {
	// This is the mask. Since it's always a power of 2, adding 1 to this value will return the size.
	size_t mask;
	// This is the elements that the circular buffer points to
	void **elements;

	void *get(size_t i) { assert(elements); return elements ? elements[i & mask] : NULL; }
	void put(size_t i, void *data) { assert(elements); elements[i&mask] = data; }

	void grow(size_t item, size_t index);
	void ensure_size(size_t item, size_t index) { if (index > mask) grow(item, index); }
	size_t size() { return mask + 1; }
};

static struct UTPGlobalStats _global_stats;

// Item contains the element we want to make space for
// index is the index in the list.
void SizableCircularBuffer::grow(size_t item, size_t index)
{
	// Figure out the new size.
	size_t size = mask + 1;
	do size *= 2; while (index >= size);

	// Allocate the new buffer
	void **buf = (void**)calloc(size, sizeof(void*));

	size--;

	// Copy elements from the old buffer to the new buffer
	for (size_t i = 0; i <= mask; i++) {
		buf[(item - index + i) & size] = get(item - index + i);
	}

	// Swap to the newly allocated buffer
	mask = size;
	free(elements);
	elements = buf;
}

// compare if lhs is less than rhs, taking wrapping
// into account. if lhs is close to UINT_MAX and rhs
// is close to 0, lhs is assumed to have wrapped and
// considered smaller
bool wrapping_compare_less(uint32 lhs, uint32 rhs)
{
	// distance walking from lhs to rhs, downwards
	const uint32 dist_down = lhs - rhs;
	// distance walking from lhs to rhs, upwards
	const uint32 dist_up = rhs - lhs;

	// if the distance walking up is shorter, lhs
	// is less than rhs. If the distance walking down
	// is shorter, then rhs is less than lhs
	return dist_up < dist_down;
}

struct DelayHist {
	uint32 delay_base;

	// this is the history of delay samples,
	// normalized by using the delay_base. These
	// values are always greater than 0 and measures
	// the queuing delay in microseconds
	uint32 cur_delay_hist[CUR_DELAY_SIZE];
	size_t cur_delay_idx;

	// this is the history of delay_base. It's
	// a number that doesn't have an absolute meaning
	// only relative. It doesn't make sense to initialize
	// it to anything other than values relative to
	// what's been seen in the real world.
	uint32 delay_base_hist[DELAY_BASE_HISTORY];
	size_t delay_base_idx;
	// the time when we last stepped the delay_base_idx
	uint32 delay_base_time;

	bool delay_base_initialized;

	void clear()
	{
		delay_base_initialized = false;
		delay_base = 0;
		cur_delay_idx = 0;
		delay_base_idx = 0;
		delay_base_time = g_current_ms;
		for (size_t i = 0; i < CUR_DELAY_SIZE; i++) {
			cur_delay_hist[i] = 0;
		}
		for (size_t i = 0; i < DELAY_BASE_HISTORY; i++) {
			delay_base_hist[i] = 0;
		}
	}

	void shift(const uint32 offset)
	{
		// the offset should never be "negative"
		// assert(offset < 0x10000000);

		// increase all of our base delays by this amount
		// this is used to take clock skew into account
		// by observing the other side's changes in its base_delay
		for (size_t i = 0; i < DELAY_BASE_HISTORY; i++) {
			delay_base_hist[i] += offset;
		}
		delay_base += offset;
	}

	void add_sample(const uint32 sample)
	{
		// The two clocks (in the two peers) are assumed not to
		// progress at the exact same rate. They are assumed to be
		// drifting, which causes the delay samples to contain
		// a systematic error, either they are under-
		// estimated or over-estimated. This is why we update the
		// delay_base every two minutes, to adjust for this.

		// This means the values will keep drifting and eventually wrap.
		// We can cross the wrapping boundry in two directions, either
		// going up, crossing the highest value, or going down, crossing 0.

		// if the delay_base is close to the max value and sample actually
		// wrapped on the other end we would see something like this:
		// delay_base = 0xffffff00, sample = 0x00000400
		// sample - delay_base = 0x500 which is the correct difference

		// if the delay_base is instead close to 0, and we got an even lower
		// sample (that will eventually update the delay_base), we may see
		// something like this:
		// delay_base = 0x00000400, sample = 0xffffff00
		// sample - delay_base = 0xfffffb00
		// this needs to be interpreted as a negative number and the actual
		// recorded delay should be 0.

		// It is important that all arithmetic that assume wrapping
		// is done with unsigned intergers. Signed integers are not guaranteed
		// to wrap the way unsigned integers do. At least GCC takes advantage
		// of this relaxed rule and won't necessarily wrap signed ints.

		// remove the clock offset and propagation delay.
		// delay base is min of the sample and the current
		// delay base. This min-operation is subject to wrapping
		// and care needs to be taken to correctly choose the
		// true minimum.

		// specifically the problem case is when delay_base is very small
		// and sample is very large (because it wrapped past zero), sample
		// needs to be considered the smaller

		if (!delay_base_initialized) {
			// delay_base being 0 suggests that we haven't initialized
			// it or its history with any real measurements yet. Initialize
			// everything with this sample.
			for (size_t i = 0; i < DELAY_BASE_HISTORY; i++) {
				// if we don't have a value, set it to the current sample
				delay_base_hist[i] = sample;
				continue;
			}
			delay_base = sample;
			delay_base_initialized = true;
		}

		if (wrapping_compare_less(sample, delay_base_hist[delay_base_idx])) {
			// sample is smaller than the current delay_base_hist entry
			// update it
			delay_base_hist[delay_base_idx] = sample;
		}

		// is sample lower than delay_base? If so, update delay_base
		if (wrapping_compare_less(sample, delay_base)) {
			// sample is smaller than the current delay_base
			// update it
			delay_base = sample;
		}
		
		// this operation may wrap, and is supposed to
		const uint32 delay = sample - delay_base;
		// sanity check. If this is triggered, something fishy is going on
		// it means the measured sample was greater than 32 seconds!
//		assert(delay < 0x2000000);

		cur_delay_hist[cur_delay_idx] = delay;
		cur_delay_idx = (cur_delay_idx + 1) % CUR_DELAY_SIZE;

		// once every minute
		if (g_current_ms - delay_base_time > 60 * 1000) {
			delay_base_time = g_current_ms;
			delay_base_idx = (delay_base_idx + 1) % DELAY_BASE_HISTORY;
			// clear up the new delay base history spot by initializing
			// it to the current sample, then update it 
			delay_base_hist[delay_base_idx] = sample;
			delay_base = delay_base_hist[0];
			// Assign the lowest delay in the last 2 minutes to delay_base
			for (size_t i = 0; i < DELAY_BASE_HISTORY; i++) {
				if (wrapping_compare_less(delay_base_hist[i], delay_base))
					delay_base = delay_base_hist[i];
			}
		}
	}

	uint32 get_value()
	{
		uint32 value = UINT_MAX;
		for (size_t i = 0; i < CUR_DELAY_SIZE; i++) {
			value = min<uint32>(cur_delay_hist[i], value);
		}
		// value could be UINT_MAX if we have no samples yet...
		return value;
	}
};

struct UTPSocket {
	PackedSockAddr addr;

	size_t idx;

	uint16 reorder_count;
	byte duplicate_ack;

	// the number of bytes we've received but not acked yet
	size_t bytes_since_ack;

	// the number of packets in the send queue. Packets that haven't
	// yet been sent count as well as packets marked as needing resend
	// the oldest un-acked packet in the send queue is seq_nr - cur_window_packets
	uint16 cur_window_packets;

	// how much of the window is used, number of bytes in-flight
	// packets that have not yet been sent do not count, packets
	// that are marked as needing to be re-sent (due to a timeout)
	// don't count either
	size_t cur_window;
	// maximum window size, in bytes
	size_t max_window;
	// SO_SNDBUF setting, in bytes
	size_t opt_sndbuf;
	// SO_RCVBUF setting, in bytes
	size_t opt_rcvbuf;

	// Is a FIN packet in the reassembly buffer?
	bool got_fin:1;
	// Timeout procedure
	bool fast_timeout:1;

	// max receive window for other end, in bytes
	size_t max_window_user;
	// 0 = original uTP header, 1 = second revision
	byte version;
	CONN_STATE state;
	// TickCount when we last decayed window (wraps)
	int32 last_rwin_decay;

	// the sequence number of the FIN packet. This field is only set
	// when we have received a FIN, and the flag field has the FIN flag set.
	// it is used to know when it is safe to destroy the socket, we must have
	// received all packets up to this sequence number first.
	uint16 eof_pkt;

	// All sequence numbers up to including this have been properly received
	// by us
	uint16 ack_nr;
	// This is the sequence number for the next packet to be sent.
	uint16 seq_nr;

	uint16 timeout_seq_nr;

	// This is the sequence number of the next packet we're allowed to
	// do a fast resend with. This makes sure we only do a fast-resend
	// once per packet. We can resend the packet with this sequence number
	// or any later packet (with a higher sequence number).
	uint16 fast_resend_seq_nr;

	uint32 reply_micro;

	// the time when we need to send another ack. If there's
	// nothing to ack, this is a very large number
	uint32 ack_time;

	uint32 last_got_packet;
	uint32 last_sent_packet;
	uint32 last_measured_delay;
	uint32 last_maxed_out_window;

	// the last time we added send quota to the connection
	// when adding send quota, this is subtracted from the
	// current time multiplied by max_window / rtt
	// which is the current allowed send rate.
	int32 last_send_quota;

	// the number of bytes we are allowed to send on
	// this connection. If this is more than one packet
	// size when we run out of data to send, it is clamped
	// to the packet size
	// this value is multiplied by 100 in order to get
	// higher accuracy when dealing with low rates
	int32 send_quota;

	SendToProc *send_to_proc;
	void *send_to_userdata;
	UTPFunctionTable func;
	void *userdata;

	// Round trip time
	uint rtt;
	// Round trip time variance
	uint rtt_var;
	// Round trip timeout
	uint rto;
	DelayHist rtt_hist;
	uint retransmit_timeout;
	// The RTO timer will timeout here.
	uint rto_timeout;
	// When the window size is set to zero, start this timer. It will send a new packet every 30secs.
	uint32 zerowindow_time;

	uint32 conn_seed;
	// Connection ID for packets I receive
	uint32 conn_id_recv;
	// Connection ID for packets I send
	uint32 conn_id_send;
	// Last rcv window we advertised, in bytes
	size_t last_rcv_win;

	DelayHist our_hist;
	DelayHist their_hist;

	// extension bytes from SYN packet
	byte extensions[8];

	SizableCircularBuffer inbuf, outbuf;

#ifdef _DEBUG
	// Public stats, returned by UTP_GetStats().  See utp.h
	UTPStats _stats;
#endif // _DEBUG

	// Calculates the current receive window
	size_t get_rcv_window() const
	{
		// If we don't have a connection (such as during connection
		// establishment, always act as if we have an empty buffer).
		if (!userdata) return opt_rcvbuf;

		// Trim window down according to what's already in buffer.
		const size_t numbuf = func.get_rb_size(userdata);
		assert((int)numbuf >= 0);
		return opt_rcvbuf > numbuf ? opt_rcvbuf - numbuf : 0;
	}

	// Test if we're ready to decay max_window
	// XXX this breaks when spaced by > INT_MAX/2, which is 49
	// days; the failure mode in that case is we do an extra decay
	// or fail to do one when we really shouldn't.
	bool can_decay_win(int32 msec) const
	{
		return msec - last_rwin_decay >= MAX_WINDOW_DECAY;
	}

	// If we can, decay max window, returns true if we actually did so
	void maybe_decay_win()
	{
		if (can_decay_win(g_current_ms)) {
			// TCP uses 0.5
			max_window = (size_t)(max_window * .5);
			last_rwin_decay = g_current_ms;
			if (max_window < MIN_WINDOW_SIZE)
				max_window = MIN_WINDOW_SIZE;
		}
	}

	size_t get_header_size() const
	{
		return (version ? sizeof(PacketFormatV1) : sizeof(PacketFormat));
	}

	size_t get_header_extensions_size() const
	{
		return (version ? sizeof(PacketFormatExtensionsV1) : sizeof(PacketFormatExtensions));
	}

	void sent_ack()
	{
		ack_time = g_current_ms + 0x70000000;
		bytes_since_ack = 0;
	}

	size_t get_udp_mtu() const
	{
		socklen_t len;
		SOCKADDR_STORAGE sa = addr.get_sockaddr_storage(&len);
		return UTP_GetUDPMTU((const struct sockaddr *)&sa, len);
	}

	size_t get_udp_overhead() const
	{
		socklen_t len;
		SOCKADDR_STORAGE sa = addr.get_sockaddr_storage(&len);
		return UTP_GetUDPOverhead((const struct sockaddr *)&sa, len);
	}

	uint64 get_global_utp_bytes_sent() const
	{
		socklen_t len;
		SOCKADDR_STORAGE sa = addr.get_sockaddr_storage(&len);
		return UTP_GetGlobalUTPBytesSent((const struct sockaddr *)&sa, len);
	}

	size_t get_overhead() const
	{
		return get_udp_overhead() + get_header_size();
	}

	void send_data(PacketFormat* b, size_t length, bandwidth_type_t type);

	void send_ack(bool synack = false);

	void send_keep_alive();

	static void send_rst(SendToProc *send_to_proc, void *send_to_userdata,
						 const PackedSockAddr &addr, uint32 conn_id_send,
						 uint16 ack_nr, uint16 seq_nr, byte version);

	void send_packet(OutgoingPacket *pkt);

	bool is_writable(size_t to_write);

	bool flush_packets();

	void write_outgoing_packet(size_t payload, uint flags);

	void update_send_quota();

#ifdef _DEBUG
	void check_invariant();
#endif

	void check_timeouts();

	int ack_packet(uint16 seq);

	size_t selective_ack_bytes(uint base, const byte* mask, byte len, int64& min_rtt);

	void selective_ack(uint base, const byte *mask, byte len);

	void apply_ledbat_ccontrol(size_t bytes_acked, uint32 actual_delay, int64 min_rtt);

	size_t get_packet_size();
};

Array<RST_Info> g_rst_info;
Array<UTPSocket*> g_utp_sockets;

static void UTP_RegisterSentPacket(size_t length) {
	if (length <= PACKET_SIZE_MID) {
		if (length <= PACKET_SIZE_EMPTY) {
			_global_stats._nraw_send[PACKET_SIZE_EMPTY_BUCKET]++;
		} else if (length <= PACKET_SIZE_SMALL) {
			_global_stats._nraw_send[PACKET_SIZE_SMALL_BUCKET]++;
		} else
			_global_stats._nraw_send[PACKET_SIZE_MID_BUCKET]++;
	} else {
		if (length <= PACKET_SIZE_BIG) {
			_global_stats._nraw_send[PACKET_SIZE_BIG_BUCKET]++;
		} else
			_global_stats._nraw_send[PACKET_SIZE_HUGE_BUCKET]++;
	}
}

void send_to_addr(SendToProc *send_to_proc, void *send_to_userdata, const byte *p, size_t len, const PackedSockAddr &addr)
{
	socklen_t tolen;
	SOCKADDR_STORAGE to = addr.get_sockaddr_storage(&tolen);
	UTP_RegisterSentPacket(len);
	send_to_proc(send_to_userdata, p, len, (const struct sockaddr *)&to, tolen);
}

void UTPSocket::send_data(PacketFormat* b, size_t length, bandwidth_type_t type)
{
	// time stamp this packet with local time, the stamp goes into
	// the header of every packet at the 8th byte for 8 bytes :
	// two integers, check packet.h for more
	uint64 time = UTP_GetMicroseconds();

	PacketFormatV1* b1 = (PacketFormatV1*)b;
	if (version == 0) {
		b->tv_sec = (uint32)(time / 1000000);
		b->tv_usec = time % 1000000;
		b->reply_micro = reply_micro;
	} else {
		b1->tv_usec = (uint32)time;
		b1->reply_micro = reply_micro;
	}

	last_sent_packet = g_current_ms;

#ifdef _DEBUG
	_stats._nbytes_xmit += length;
	++_stats._nxmit;
#endif
	if (userdata) {
		size_t n;
		if (type == payload_bandwidth) {
			// if this packet carries payload, just
			// count the header as overhead
			type = header_overhead;
			n = get_overhead();
		} else {
			n = length + get_udp_overhead();
		}
		func.on_overhead(userdata, true, n, type);
	}
#if g_log_utp_verbose
	int flags = version == 0 ? b->flags : b1->type();
	uint16 seq_nr = version == 0 ? b->seq_nr : b1->seq_nr;
	uint16 ack_nr = version == 0 ? b->ack_nr : b1->ack_nr;
	LOG_UTPV("0x%08x: send %s len:%u id:%u timestamp:"I64u" reply_micro:%u flags:%s seq_nr:%u ack_nr:%u",
			 this, addrfmt(addr, addrbuf), (uint)length, conn_id_send, time, reply_micro, flagnames[flags],
			 seq_nr, ack_nr);
#endif
	send_to_addr(send_to_proc, send_to_userdata, (const byte*)b, length, addr);
}

void UTPSocket::send_ack(bool synack)
{
	PacketFormatExtensions pfe;
	zeromem(&pfe);
	PacketFormatExtensionsV1& pfe1 = (PacketFormatExtensionsV1&)pfe;
	PacketFormatAck& pfa = (PacketFormatAck&)pfe1;
	PacketFormatAckV1& pfa1 = (PacketFormatAckV1&)pfe1;

	size_t len;
	last_rcv_win = get_rcv_window();
	if (version == 0) {
		pfa.pf.connid = conn_id_send;
		pfa.pf.ack_nr = (uint16)ack_nr;
		pfa.pf.seq_nr = (uint16)seq_nr;
		pfa.pf.flags = ST_STATE;
		pfa.pf.ext = 0;
		pfa.pf.windowsize = (byte)DIV_ROUND_UP(last_rcv_win, PACKET_SIZE);
		len = sizeof(PacketFormat);
	} else {
		pfa1.pf.set_version(1);
		pfa1.pf.set_type(ST_STATE);
		pfa1.pf.ext = 0;
		pfa1.pf.connid = conn_id_send;
		pfa1.pf.ack_nr = ack_nr;
		pfa1.pf.seq_nr = seq_nr;
		pfa1.pf.windowsize = (uint32)last_rcv_win;
		len = sizeof(PacketFormatV1);
	}

	// we never need to send EACK for connections
	// that are shutting down
	if (reorder_count != 0 && state < CS_GOT_FIN) {
		// if reorder count > 0, send an EACK.
		// reorder count should always be 0
		// for synacks, so this should not be
		// as synack
		assert(!synack);
		if (version == 0) {
			pfa.pf.ext = 1;
			pfa.ext_next = 0;
			pfa.ext_len = 4;
		} else {
			pfa1.pf.ext = 1;
			pfa1.ext_next = 0;
			pfa1.ext_len = 4;
		}
		uint m = 0;

		// reorder count should only be non-zero
		// if the packet ack_nr + 1 has not yet
		// been received
		assert(inbuf.get(ack_nr + 1) == NULL);
		size_t window = min<size_t>(14+16, inbuf.size());
		// Generate bit mask of segments received.
		for (size_t i = 0; i < window; i++) {
			if (inbuf.get(ack_nr + i + 2) != NULL) {
				m |= 1 << i;
				LOG_UTPV("0x%08x: EACK packet [%u]", this, ack_nr + i + 2);
			}
		}
		if (version == 0) {
			pfa.acks[0] = (byte)m;
			pfa.acks[1] = (byte)(m >> 8);
			pfa.acks[2] = (byte)(m >> 16);
			pfa.acks[3] = (byte)(m >> 24);
		} else {
			pfa1.acks[0] = (byte)m;
			pfa1.acks[1] = (byte)(m >> 8);
			pfa1.acks[2] = (byte)(m >> 16);
			pfa1.acks[3] = (byte)(m >> 24);
		}
		len += 4 + 2;
		LOG_UTPV("0x%08x: Sending EACK %u [%u] bits:[%032b]", this, ack_nr, conn_id_send, m);
	} else if (synack) {
		// we only send "extensions" in response to SYN
		// and the reorder count is 0 in that state

		LOG_UTPV("0x%08x: Sending ACK %u [%u] with extension bits", this, ack_nr, conn_id_send);
		if (version == 0) {
			pfe.pf.ext = 2;
			pfe.ext_next = 0;
			pfe.ext_len = 8;
			memset(pfe.extensions, 0, 8);
		} else {
			pfe1.pf.ext = 2;
			pfe1.ext_next = 0;
			pfe1.ext_len = 8;
			memset(pfe1.extensions, 0, 8);
		}
		len += 8 + 2;
	} else {
		LOG_UTPV("0x%08x: Sending ACK %u [%u]", this, ack_nr, conn_id_send);
	}

	sent_ack();
	send_data((PacketFormat*)&pfe, len, ack_overhead);
}

void UTPSocket::send_keep_alive()
{
	ack_nr--;
	LOG_UTPV("0x%08x: Sending KeepAlive ACK %u [%u]", this, ack_nr, conn_id_send);
	send_ack();
	ack_nr++;
}

void UTPSocket::send_rst(SendToProc *send_to_proc, void *send_to_userdata,
						 const PackedSockAddr &addr, uint32 conn_id_send, uint16 ack_nr, uint16 seq_nr, byte version)
{
	PacketFormat pf;
	zeromem(&pf);
	PacketFormatV1& pf1 = (PacketFormatV1&)pf;

	size_t len;
	if (version == 0) {
		pf.connid = conn_id_send;
		pf.ack_nr = ack_nr;
		pf.seq_nr = seq_nr;
		pf.flags = ST_RESET;
		pf.ext = 0;
		pf.windowsize = 0;
		len = sizeof(PacketFormat);
	} else {
		pf1.set_version(1);
		pf1.set_type(ST_RESET);
		pf1.ext = 0;
		pf1.connid = conn_id_send;
		pf1.ack_nr = ack_nr;
		pf1.seq_nr = seq_nr;
		pf1.windowsize = 0;
		len = sizeof(PacketFormatV1);
	}

	LOG_UTPV("%s: Sending RST id:%u seq_nr:%u ack_nr:%u", addrfmt(addr, addrbuf), conn_id_send, seq_nr, ack_nr);
	LOG_UTPV("send %s len:%u id:%u", addrfmt(addr, addrbuf), (uint)len, conn_id_send);
	send_to_addr(send_to_proc, send_to_userdata, (const byte*)&pf1, len, addr);
}

void UTPSocket::send_packet(OutgoingPacket *pkt)
{
	// only count against the quota the first time we
	// send the packet. Don't enforce quota when closing
	// a socket. Only enforce the quota when we're sending
	// at slow rates (max window < packet size)
	size_t max_send = min(max_window, opt_sndbuf, max_window_user);

	if (pkt->transmissions == 0 || pkt->need_resend) {
		cur_window += pkt->payload;
	}

	size_t packet_size = get_packet_size();
	if (pkt->transmissions == 0 && max_send < packet_size) {
		assert(state == CS_FIN_SENT ||
			   (int32)pkt->payload <= send_quota / 100);
		send_quota = send_quota - (int32)(pkt->payload * 100);
	}

	pkt->need_resend = false;

	PacketFormatV1* p1 = (PacketFormatV1*)pkt->data;
	PacketFormat* p = (PacketFormat*)pkt->data;
	if (version == 0) {
		p->ack_nr = ack_nr;
	} else {
		p1->ack_nr = ack_nr;
	}
	pkt->time_sent = UTP_GetMicroseconds();
	pkt->transmissions++;
	sent_ack();
	send_data((PacketFormat*)pkt->data, pkt->length,
		(state == CS_SYN_SENT) ? connect_overhead
		: (pkt->transmissions == 1) ? payload_bandwidth
		: retransmit_overhead);
}

bool UTPSocket::is_writable(size_t to_write)
{
	// return true if it's OK to stuff another packet into the
	// outgoing queue. Since we may be using packet pacing, we
	// might not actually send the packet right away to affect the
	// cur_window. The only thing that happens when we add another
	// packet is that cur_window_packets is increased.
	size_t max_send = min(max_window, opt_sndbuf, max_window_user);

	size_t packet_size = get_packet_size();

	if (cur_window + packet_size >= max_window)
		last_maxed_out_window = g_current_ms;

	// if we don't have enough quota, we can't write regardless
	if (USE_PACKET_PACING) {
		if (send_quota / 100 < (int32)to_write) return false;
	}

	// subtract one to save space for the FIN packet
	if (cur_window_packets >= OUTGOING_BUFFER_MAX_SIZE - 1) return false;

	// if sending another packet would not make the window exceed
	// the max_window, we can write
	if (cur_window + packet_size <= max_send) return true;

	// if the window size is less than a packet, and we have enough
	// quota to send a packet, we can write, even though it would
	// make the window exceed the max size
	// the last condition is needed to not put too many packets
	// in the send buffer. cur_window isn't updated until we flush
	// the send buffer, so we need to take the number of packets
	// into account
	if (USE_PACKET_PACING) {
		if (max_window < to_write &&
			cur_window < max_window &&
			cur_window_packets == 0) {
			return true;
		}
	}

	return false;
}

bool UTPSocket::flush_packets()
{
	size_t packet_size = get_packet_size();

	// send packets that are waiting on the pacer to be sent
	// i has to be an unsigned 16 bit counter to wrap correctly
	// signed types are not guaranteed to wrap the way you expect
	for (uint16 i = seq_nr - cur_window_packets; i != seq_nr; ++i) {
		OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(i);
		if (pkt == 0 || (pkt->transmissions > 0 && pkt->need_resend == false)) continue;
		// have we run out of quota?
		if (!is_writable(pkt->payload)) {
			return true;
		}

		// Nagle check
		// don't send the last packet if we have one packet in-flight
		// and the current packet is still smaller than packet_size.
		if (i != ((seq_nr - 1) & ACK_NR_MASK) ||
			cur_window_packets == 1 ||
			pkt->payload >= packet_size) {
			send_packet(pkt);

			// No need to send another ack if there is nothing to reorder.
			if (reorder_count == 0) {
				sent_ack();
			}
		}
	}
	return false;
}

void UTPSocket::write_outgoing_packet(size_t payload, uint flags)
{
	// Setup initial timeout timer
	if (cur_window_packets == 0) {
		retransmit_timeout = rto;
		rto_timeout = g_current_ms + retransmit_timeout;
		assert(cur_window == 0);
	}

	size_t packet_size = get_packet_size();
	do {
		assert(cur_window_packets < OUTGOING_BUFFER_MAX_SIZE);
		assert(flags == ST_DATA || flags == ST_FIN);

		size_t added = 0;

		OutgoingPacket *pkt = NULL;
		
		if (cur_window_packets > 0) {
			pkt = (OutgoingPacket*)outbuf.get(seq_nr - 1);
		}

		const size_t header_size = get_header_size();
		bool append = true;

		// if there's any room left in the last packet in the window
		// and it hasn't been sent yet, fill that frame first
		if (payload && pkt && !pkt->transmissions && pkt->payload < packet_size) {
			// Use the previous unsent packet
			added = min(payload + pkt->payload, max<size_t>(packet_size, pkt->payload)) - pkt->payload;
			pkt = (OutgoingPacket*)realloc(pkt,
										   (sizeof(OutgoingPacket) - 1) +
										   header_size +
										   pkt->payload + added);
			outbuf.put(seq_nr - 1, pkt);
			append = false;
			assert(!pkt->need_resend);
		} else {
			// Create the packet to send.
			added = payload;
			pkt = (OutgoingPacket*)malloc((sizeof(OutgoingPacket) - 1) +
										  header_size +
										  added);
			pkt->payload = 0;
			pkt->transmissions = 0;
			pkt->need_resend = false;
		}

		if (added) {
			// Fill it with data from the upper layer.
			func.on_write(userdata, pkt->data + header_size + pkt->payload, added);
		}
		pkt->payload += added;
		pkt->length = header_size + pkt->payload;

		last_rcv_win = get_rcv_window();

		PacketFormat* p = (PacketFormat*)pkt->data;
		PacketFormatV1* p1 = (PacketFormatV1*)pkt->data;
		if (version == 0) {
			p->connid = conn_id_send;
			p->ext = 0;
			p->windowsize = (byte)DIV_ROUND_UP(last_rcv_win, PACKET_SIZE);
			p->ack_nr = ack_nr;
			p->flags = flags;
		} else {
			p1->set_version(1);
			p1->set_type(flags);
			p1->ext = 0;
			p1->connid = conn_id_send;
			p1->windowsize = (uint32)last_rcv_win;
			p1->ack_nr = ack_nr;
		}

		if (append) {
			// Remember the message in the outgoing queue.
			outbuf.ensure_size(seq_nr, cur_window_packets);
			outbuf.put(seq_nr, pkt);
			if (version == 0) p->seq_nr = seq_nr;
			else p1->seq_nr = seq_nr;
			seq_nr++;
			cur_window_packets++;
		}

		payload -= added;

	} while (payload);

	flush_packets();
}

void UTPSocket::update_send_quota()
{
	int dt = g_current_ms - last_send_quota;
	if (dt == 0) return;
	last_send_quota = g_current_ms;
	size_t add = max_window * dt * 100 / (rtt_hist.delay_base?rtt_hist.delay_base:50);
	if (add > max_window * 100 && add > MAX_CWND_INCREASE_BYTES_PER_RTT * 100) add = max_window;
	send_quota += (int32)add;
//	LOG_UTPV("0x%08x: UTPSocket::update_send_quota dt:%d rtt:%u max_window:%u quota:%d",
//			 this, dt, rtt, (uint)max_window, send_quota / 100);
}

#ifdef _DEBUG
void UTPSocket::check_invariant()
{
	if (reorder_count > 0) {
		assert(inbuf.get(ack_nr + 1) == NULL);
	}

	size_t outstanding_bytes = 0;
	for (int i = 0; i < cur_window_packets; ++i) {
		OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(seq_nr - i - 1);
		if (pkt == 0 || pkt->transmissions == 0 || pkt->need_resend) continue;
		outstanding_bytes += pkt->payload;
	}
	assert(outstanding_bytes == cur_window);
}
#endif

void UTPSocket::check_timeouts()
{
#ifdef _DEBUG
	check_invariant();
#endif

	// this invariant should always be true
	assert(cur_window_packets == 0 || outbuf.get(seq_nr - cur_window_packets));

	LOG_UTPV("0x%08x: CheckTimeouts timeout:%d max_window:%u cur_window:%u quota:%d "
			 "state:%s cur_window_packets:%u bytes_since_ack:%u ack_time:%d",
			 this, (int)(rto_timeout - g_current_ms), (uint)max_window, (uint)cur_window,
			 send_quota / 100, statenames[state], cur_window_packets,
			 (uint)bytes_since_ack, (int)(g_current_ms - ack_time));

	update_send_quota();
	flush_packets();


	if (USE_PACKET_PACING) {
		// In case the new send quota made it possible to send another packet
		// Mark the socket as writable. If we don't use pacing, the send
		// quota does not affect if the socket is writeable
		// if we don't use packet pacing, the writable event is triggered
		// whenever the cur_window falls below the max_window, so we don't
		// need this check then
		if (state == CS_CONNECTED_FULL && is_writable(get_packet_size())) {
			state = CS_CONNECTED;
			LOG_UTPV("0x%08x: Socket writable. max_window:%u cur_window:%u quota:%d packet_size:%u",
					 this, (uint)max_window, (uint)cur_window, send_quota / 100, (uint)get_packet_size());
			func.on_state(userdata, UTP_STATE_WRITABLE);
		}
	}

	switch (state) {
	case CS_SYN_SENT:
	case CS_CONNECTED_FULL:
	case CS_CONNECTED:
	case CS_FIN_SENT: {

		// Reset max window...
		if ((int)(g_current_ms - zerowindow_time) >= 0 && max_window_user == 0) {
			max_window_user = PACKET_SIZE;
		}

		if ((int)(g_current_ms - rto_timeout) >= 0 &&
			(!(USE_PACKET_PACING) || cur_window_packets > 0) &&
			rto_timeout > 0) {

			/*
			OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(seq_nr - cur_window_packets);
			
			// If there were a lot of retransmissions, force recomputation of round trip time
			if (pkt->transmissions >= 4)
				rtt = 0;
			*/

			// Increase RTO
			const uint new_timeout = retransmit_timeout * 2;
			if (new_timeout >= 30000 || (state == CS_SYN_SENT && new_timeout > 6000)) {
				// more than 30 seconds with no reply. kill it.
				// if we haven't even connected yet, give up sooner. 6 seconds
				// means 2 tries at the following timeouts: 3, 6 seconds
				if (state == CS_FIN_SENT)
					state = CS_DESTROY;
				else
					state = CS_RESET;
				func.on_error(userdata, ETIMEDOUT);
				goto getout;
			}

			retransmit_timeout = new_timeout;
			rto_timeout = g_current_ms + new_timeout;

			// On Timeout
			duplicate_ack = 0;

			// rate = min_rate
			max_window = get_packet_size();
			send_quota = max<int32>((int32)max_window * 100, send_quota);

			// every packet should be considered lost
			for (int i = 0; i < cur_window_packets; ++i) {
				OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(seq_nr - i - 1);
				if (pkt == 0 || pkt->transmissions == 0 || pkt->need_resend) continue;
				pkt->need_resend = true;
				assert(cur_window >= pkt->payload);
				cur_window -= pkt->payload;
			}

			// used in parse_log.py
			LOG_UTP("0x%08x: Packet timeout. Resend. seq_nr:%u. timeout:%u max_window:%u",
					this, seq_nr - cur_window_packets, retransmit_timeout, (uint)max_window);

			fast_timeout = true;
			timeout_seq_nr = seq_nr;

			if (cur_window_packets > 0) {
				OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(seq_nr - cur_window_packets);
				assert(pkt);
				send_quota = max<int32>((int32)pkt->length * 100, send_quota);

				// Re-send the packet.
				send_packet(pkt);
			}
		}

		// Mark the socket as writable
		if (state == CS_CONNECTED_FULL && is_writable(get_packet_size())) {
			state = CS_CONNECTED;
			LOG_UTPV("0x%08x: Socket writable. max_window:%u cur_window:%u quota:%d packet_size:%u",
					 this, (uint)max_window, (uint)cur_window, send_quota / 100, (uint)get_packet_size());
			func.on_state(userdata, UTP_STATE_WRITABLE);
		}

		if (state >= CS_CONNECTED && state <= CS_FIN_SENT) {
			// Send acknowledgment packets periodically, or when the threshold is reached
			if (bytes_since_ack > DELAYED_ACK_BYTE_THRESHOLD ||
				(int)(g_current_ms - ack_time) >= 0) {
				send_ack();
			}

			if ((int)(g_current_ms - last_sent_packet) >= KEEPALIVE_INTERVAL) {
				send_keep_alive();
			}
		}

		break;
	}

	// Close?
	case CS_GOT_FIN:
	case CS_DESTROY_DELAY:
		if ((int)(g_current_ms - rto_timeout) >= 0) {
			state = (state == CS_DESTROY_DELAY) ? CS_DESTROY : CS_RESET;
			if (cur_window_packets > 0 && userdata) {
				func.on_error(userdata, ECONNRESET);
			}
		}
		break;
	// prevent warning
	case CS_IDLE:
	case CS_RESET:
	case CS_DESTROY:
		break;
	}

	getout:

	// make sure we don't accumulate quota when we don't have
	// anything to send
	int32 limit = max<int32>((int32)max_window / 2, 5 * (int32)get_packet_size()) * 100;
	if (send_quota > limit) send_quota = limit;
}

// returns:
// 0: the packet was acked.
// 1: it means that the packet had already been acked
// 2: the packet has not been sent yet
int UTPSocket::ack_packet(uint16 seq)
{
	OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(seq);

	// the packet has already been acked (or not sent)
	if (pkt == NULL) {
		LOG_UTPV("0x%08x: got ack for:%u (already acked, or never sent)", this, seq);
		return 1;
	}

	// can't ack packets that haven't been sent yet!
	if (pkt->transmissions == 0) {
		LOG_UTPV("0x%08x: got ack for:%u (never sent, pkt_size:%u need_resend:%u)",
				 this, seq, (uint)pkt->payload, pkt->need_resend);
		return 2;
	}

	LOG_UTPV("0x%08x: got ack for:%u (pkt_size:%u need_resend:%u)",
			 this, seq, (uint)pkt->payload, pkt->need_resend);

	outbuf.put(seq, NULL);

	// if we never re-sent the packet, update the RTT estimate
	if (pkt->transmissions == 1) {
		// Estimate the round trip time.
		const uint32 ertt = (uint32)((UTP_GetMicroseconds() - pkt->time_sent) / 1000);
		if (rtt == 0) {
			// First round trip time sample
			rtt = ertt;
			rtt_var = ertt / 2;
			// sanity check. rtt should never be more than 6 seconds
//			assert(rtt < 6000);
		} else {
			// Compute new round trip times
			const int delta = (int)rtt - ertt;
			rtt_var = rtt_var + (int)(abs(delta) - rtt_var) / 4;
			rtt = rtt - rtt/8 + ertt/8;
			// sanity check. rtt should never be more than 6 seconds
//			assert(rtt < 6000);
			rtt_hist.add_sample(ertt);
		}
		rto = max<uint>(rtt + rtt_var * 4, 500);
		LOG_UTPV("0x%08x: rtt:%u avg:%u var:%u rto:%u",
				 this, ertt, rtt, rtt_var, rto);
	}
	retransmit_timeout = rto;
	rto_timeout = g_current_ms + rto;
	// if need_resend is set, this packet has already
	// been considered timed-out, and is not included in
	// the cur_window anymore
	if (!pkt->need_resend) {
		assert(cur_window >= pkt->payload);
		cur_window -= pkt->payload;
	}
	free(pkt);
	return 0;
}

// count the number of bytes that were acked by the EACK header
size_t UTPSocket::selective_ack_bytes(uint base, const byte* mask, byte len, int64& min_rtt)
{
	if (cur_window_packets == 0) return 0;

	size_t acked_bytes = 0;
	int bits = len * 8;

	do {
		uint v = base + bits;

		// ignore bits that haven't been sent yet
		// see comment in UTPSocket::selective_ack
		if (((seq_nr - v - 1) & ACK_NR_MASK) >= (uint16)(cur_window_packets - 1))
			continue;

		// ignore bits that represents packets we haven't sent yet
		// or packets that have already been acked
		OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(v);
		if (!pkt || pkt->transmissions == 0)
			continue;

		// Count the number of segments that were successfully received past it.
		if (bits >= 0 && mask[bits>>3] & (1 << (bits & 7))) {
			assert((int)(pkt->payload) >= 0);
			acked_bytes += pkt->payload;
			min_rtt = min<int64>(min_rtt, UTP_GetMicroseconds() - pkt->time_sent);
			continue;
		}
	} while (--bits >= -1);
	return acked_bytes;
}

enum { MAX_EACK = 128 };

void UTPSocket::selective_ack(uint base, const byte *mask, byte len)
{
	if (cur_window_packets == 0) return;

	// the range is inclusive [0, 31] bits
	int bits = len * 8 - 1;

	int count = 0;

	// resends is a stack of sequence numbers we need to resend. Since we
	// iterate in reverse over the acked packets, at the end, the top packets
	// are the ones we want to resend
	int resends[MAX_EACK];
	int nr = 0;

	LOG_UTPV("0x%08x: Got EACK [%032b] base:%u", this, *(uint32*)mask, base);
	do {
		// we're iterating over the bits from higher sequence numbers
		// to lower (kind of in reverse order, wich might not be very
		// intuitive)
		uint v = base + bits;

		// ignore bits that haven't been sent yet
		// and bits that fall below the ACKed sequence number
		// this can happen if an EACK message gets
		// reordered and arrives after a packet that ACKs up past
		// the base for thie EACK message

		// this is essentially the same as:
		// if v >= seq_nr || v <= seq_nr - cur_window_packets
		// but it takes wrapping into account

		// if v == seq_nr the -1 will make it wrap. if v > seq_nr
		// it will also wrap (since it will fall further below 0)
		// and be > cur_window_packets.
		// if v == seq_nr - cur_window_packets, the result will be
		// seq_nr - (seq_nr - cur_window_packets) - 1
		// == seq_nr - seq_nr + cur_window_packets - 1
		// == cur_window_packets - 1 which will be caught by the
		// test. If v < seq_nr - cur_window_packets the result will grow
		// fall furhter outside of the cur_window_packets range.

		// sequence number space:
		//
		//     rejected <   accepted   > rejected 
		// <============+--------------+============>
		//              ^              ^
		//              |              |
		//        (seq_nr-wnd)         seq_nr

		if (((seq_nr - v - 1) & ACK_NR_MASK) >= (uint16)(cur_window_packets - 1))
			continue;

		// this counts as a duplicate ack, even though we might have
		// received an ack for this packet previously (in another EACK
		// message for instance)
		bool bit_set = bits >= 0 && mask[bits>>3] & (1 << (bits & 7));

		// if this packet is acked, it counts towards the duplicate ack counter
		if (bit_set) count++;

		// ignore bits that represents packets we haven't sent yet
		// or packets that have already been acked
		OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(v);
		if (!pkt || pkt->transmissions == 0) {
			LOG_UTPV("0x%08x: skipping %u. pkt:%08x transmissions:%u %s",
					 this, v, pkt, pkt?pkt->transmissions:0, pkt?"(not sent yet?)":"(already acked?)");
			continue;
		}

		// Count the number of segments that were successfully received past it.
		if (bit_set) {
			// the selective ack should never ACK the packet we're waiting for to decrement cur_window_packets
			assert((v & outbuf.mask) != ((seq_nr - cur_window_packets) & outbuf.mask));
			ack_packet(v);
			continue;
		}

		// Resend segments
		// if count is less than our re-send limit, we haven't seen enough
		// acked packets in front of this one to warrant a re-send.
		// if count == 0, we're still going through the tail of zeroes
		if (((v - fast_resend_seq_nr) & ACK_NR_MASK) <= OUTGOING_BUFFER_MAX_SIZE &&
			count >= DUPLICATE_ACKS_BEFORE_RESEND &&
			duplicate_ack < DUPLICATE_ACKS_BEFORE_RESEND) {
			// resends is a stack, and we're mostly interested in the top of it
			// if we're full, just throw away the lower half
			if (nr >= MAX_EACK - 2) {
				memmove(resends, &resends[MAX_EACK/2], MAX_EACK/2 * sizeof(resends[0]));
				nr -= MAX_EACK / 2;
			}
			resends[nr++] = v;
			LOG_UTPV("0x%08x: no ack for %u", this, v);
		} else {
			LOG_UTPV("0x%08x: not resending %u count:%d dup_ack:%u fast_resend_seq_nr:%u",
					 this, v, count, duplicate_ack, fast_resend_seq_nr);
		}
	} while (--bits >= -1);

	if (((base - 1 - fast_resend_seq_nr) & ACK_NR_MASK) <= OUTGOING_BUFFER_MAX_SIZE &&
		count >= DUPLICATE_ACKS_BEFORE_RESEND) {
		// if we get enough duplicate acks to start
		// resending, the first packet we should resend
		// is base-1
		resends[nr++] = (base - 1) & ACK_NR_MASK;
	} else {
		LOG_UTPV("0x%08x: not resending %u count:%d dup_ack:%u fast_resend_seq_nr:%u",
				 this, base - 1, count, duplicate_ack, fast_resend_seq_nr);
	}

	bool back_off = false;
	int i = 0;
	while (nr > 0) {
		uint v = resends[--nr];
		// don't consider the tail of 0:es to be lost packets
		// only unacked packets with acked packets after should
		// be considered lost
		OutgoingPacket *pkt = (OutgoingPacket*)outbuf.get(v);

		// this may be an old (re-ordered) packet, and some of the
		// packets in here may have been acked already. In which
		// case they will not be in the send queue anymore
		if (!pkt) continue;

		// used in parse_log.py
		LOG_UTP("0x%08x: Packet %u lost. Resending", this, v);

		// On Loss
		back_off = true;
#ifdef _DEBUG
		++_stats._rexmit;
#endif
		send_packet(pkt);
		fast_resend_seq_nr = v + 1;

		// Re-send max 4 packets.
		if (++i >= 4) break;
	}

	if (back_off)
		maybe_decay_win();

	duplicate_ack = count;
}

void UTPSocket::apply_ledbat_ccontrol(size_t bytes_acked, uint32 actual_delay, int64 min_rtt)
{
	// the delay can never be greater than the rtt. The min_rtt
	// variable is the RTT in microseconds
	
	assert(min_rtt >= 0);
	int32 our_delay = min<uint32>(our_hist.get_value(), uint32(min_rtt));
	assert(our_delay != INT_MAX);
	assert(our_delay >= 0);

	SOCKADDR_STORAGE sa = addr.get_sockaddr_storage();
	UTP_DelaySample((sockaddr*)&sa, our_delay / 1000);

	// This test the connection under heavy load from foreground
	// traffic. Pretend that our delays are very high to force the
	// connection to use sub-packet size window sizes
	//our_delay *= 4;

	// target is microseconds
	int target = CCONTROL_TARGET;
	if (target <= 0) target = 100000;

	double off_target = target - our_delay;

	// this is the same as:
	//
	//    (min(off_target, target) / target) * (bytes_acked / max_window) * MAX_CWND_INCREASE_BYTES_PER_RTT
	//
	// so, it's scaling the max increase by the fraction of the window this ack represents, and the fraction
	// of the target delay the current delay represents.
	// The min() around off_target protects against crazy values of our_delay, which may happen when th
	// timestamps wraps, or by just having a malicious peer sending garbage. This caps the increase
	// of the window size to MAX_CWND_INCREASE_BYTES_PER_RTT per rtt.
	// as for large negative numbers, this direction is already capped at the min packet size further down
	// the min around the bytes_acked protects against the case where the window size was recently
	// shrunk and the number of acked bytes exceeds that. This is considered no more than one full
	// window, in order to keep the gain within sane boundries.

	assert(bytes_acked > 0);
	double window_factor = (double)min(bytes_acked, max_window) / (double)max(max_window, bytes_acked);
	double delay_factor = off_target / target;
	double scaled_gain = MAX_CWND_INCREASE_BYTES_PER_RTT * window_factor * delay_factor;

	// since MAX_CWND_INCREASE_BYTES_PER_RTT is a cap on how much the window size (max_window)
	// may increase per RTT, we may not increase the window size more than that proportional
	// to the number of bytes that were acked, so that once one window has been acked (one rtt)
	// the increase limit is not exceeded
	// the +1. is to allow for floating point imprecision
	assert(scaled_gain <= 1. + MAX_CWND_INCREASE_BYTES_PER_RTT * (int)min(bytes_acked, max_window) / (double)max(max_window, bytes_acked));

	if (scaled_gain > 0 && g_current_ms - last_maxed_out_window > 300) {
		// if it was more than 300 milliseconds since we tried to send a packet
		// and stopped because we hit the max window, we're most likely rate
		// limited (which prevents us from ever hitting the window size)
		// if this is the case, we cannot let the max_window grow indefinitely
		scaled_gain = 0;
	}

	if (scaled_gain + max_window < MIN_WINDOW_SIZE) {
		max_window = MIN_WINDOW_SIZE;
	} else {
		max_window = (size_t)(max_window + scaled_gain);
	}

	// make sure that the congestion window is below max
	// make sure that we don't shrink our window too small
	max_window = clamp<size_t>(max_window, MIN_WINDOW_SIZE, opt_sndbuf);

	// used in parse_log.py
	LOG_UTP("0x%08x: actual_delay:%u our_delay:%d their_delay:%u off_target:%d max_window:%u "
			"delay_base:%u delay_sum:%d target_delay:%d acked_bytes:%u cur_window:%u "
			"scaled_gain:%f rtt:%u rate:%u quota:%d wnduser:%u rto:%u timeout:%d get_microseconds:"I64u" "
			"cur_window_packets:%u packet_size:%u their_delay_base:%u their_actual_delay:%u",
			this, actual_delay, our_delay / 1000, their_hist.get_value() / 1000,
			(int)off_target / 1000, (uint)(max_window),  our_hist.delay_base,
			(our_delay + their_hist.get_value()) / 1000, target / 1000, (uint)bytes_acked,
			(uint)(cur_window - bytes_acked), (float)(scaled_gain), rtt,
			(uint)(max_window * 1000 / (rtt_hist.delay_base?rtt_hist.delay_base:50)),
			send_quota / 100, (uint)max_window_user, rto, (int)(rto_timeout - g_current_ms),
			UTP_GetMicroseconds(), cur_window_packets, (uint)get_packet_size(),
			their_hist.delay_base, their_hist.delay_base + their_hist.get_value());
}

static void UTP_RegisterRecvPacket(UTPSocket *conn, size_t len)
{
#ifdef _DEBUG
	++conn->_stats._nrecv;
	conn->_stats._nbytes_recv += len;
#endif

	if (len <= PACKET_SIZE_MID) {
		if (len <= PACKET_SIZE_EMPTY) {
			_global_stats._nraw_recv[PACKET_SIZE_EMPTY_BUCKET]++;
		} else if (len <= PACKET_SIZE_SMALL) {
			_global_stats._nraw_recv[PACKET_SIZE_SMALL_BUCKET]++;
		} else 
			_global_stats._nraw_recv[PACKET_SIZE_MID_BUCKET]++;
	} else {
		if (len <= PACKET_SIZE_BIG) {
			_global_stats._nraw_recv[PACKET_SIZE_BIG_BUCKET]++;
		} else 
			_global_stats._nraw_recv[PACKET_SIZE_HUGE_BUCKET]++;
	}
}

// returns the max number of bytes of payload the uTP
// connection is allowed to send
size_t UTPSocket::get_packet_size()
{
	int header_size = version == 1
		? sizeof(PacketFormatV1)
		: sizeof(PacketFormat);

	size_t mtu = get_udp_mtu();

	if (DYNAMIC_PACKET_SIZE_ENABLED) {
		SOCKADDR_STORAGE sa = addr.get_sockaddr_storage();
		size_t max_packet_size = UTP_GetPacketSize((sockaddr*)&sa);
		return min(mtu - header_size, max_packet_size);
	}
	else
	{
		return mtu - header_size;
	}
}

// Process an incoming packet
// syn is true if this is the first packet received. It will cut off parsing
// as soon as the header is done
size_t UTP_ProcessIncoming(UTPSocket *conn, const byte *packet, size_t len, bool syn = false)
{
	UTP_RegisterRecvPacket(conn, len);

	g_current_ms = UTP_GetMilliseconds();

	conn->update_send_quota();

	const PacketFormat *pf = (PacketFormat*)packet;
	const PacketFormatV1 *pf1 = (PacketFormatV1*)packet;
	const byte *packet_end = packet + len;

	uint16 pk_seq_nr;
	uint16 pk_ack_nr;
	uint8 pk_flags;
	if (conn->version == 0) {
		pk_seq_nr = pf->seq_nr;
		pk_ack_nr = pf->ack_nr;
		pk_flags = pf->flags;
	} else {
		pk_seq_nr = pf1->seq_nr;
		pk_ack_nr = pf1->ack_nr;
		pk_flags = pf1->type();
	}

	if (pk_flags >= ST_NUM_STATES) return 0;

	LOG_UTPV("0x%08x: Got %s. seq_nr:%u ack_nr:%u state:%s version:%u timestamp:"I64u" reply_micro:%u",
			 conn, flagnames[pk_flags], pk_seq_nr, pk_ack_nr, statenames[conn->state], conn->version,
			 conn->version == 0?(uint64)(pf->tv_sec) * 1000000 + pf->tv_usec:uint64(pf1->tv_usec),
			 conn->version == 0?(uint32)(pf->reply_micro):(uint32)(pf1->reply_micro));

	// mark receipt time
	uint64 time = UTP_GetMicroseconds();

	// RSTs are handled earlier, since the connid matches the send id not the recv id
	assert(pk_flags != ST_RESET);

	// TODO: maybe send a ST_RESET if we're in CS_RESET?

	const byte *selack_ptr = NULL;

	// Unpack UTP packet options
	// Data pointer
	const byte *data = (const byte*)pf + conn->get_header_size();
	if (conn->get_header_size() > len) {
		LOG_UTPV("0x%08x: Invalid packet size (less than header size)", conn);
		return 0;
	}
	// Skip the extension headers
	uint extension = conn->version == 0 ? pf->ext : pf1->ext;
	if (extension != 0) {
		do {
			// Verify that the packet is valid.
			data += 2;

			if ((int)(packet_end - data) < 0 || (int)(packet_end - data) < data[-1]) {
				LOG_UTPV("0x%08x: Invalid len of extensions", conn);
				return 0;
			}

			switch(extension) {
			case 1: // Selective Acknowledgment
				selack_ptr = data;
				break;
			case 2: // extension bits
				if (data[-1] != 8) {
					LOG_UTPV("0x%08x: Invalid len of extension bits header", conn);
					return 0;
				}
				memcpy(conn->extensions, data, 8);
				LOG_UTPV("0x%08x: got extension bits:%02x%02x%02x%02x%02x%02x%02x%02x", conn,
					conn->extensions[0], conn->extensions[1], conn->extensions[2], conn->extensions[3],
					conn->extensions[4], conn->extensions[5], conn->extensions[6], conn->extensions[7]);
			}
			extension = data[-2];
			data += data[-1];
		} while (extension);
	}

	if (conn->state == CS_SYN_SENT) {
		// if this is a syn-ack, initialize our ack_nr
		// to match the sequence number we got from
		// the other end
		conn->ack_nr = (pk_seq_nr - 1) & SEQ_NR_MASK;
	}

	g_current_ms = UTP_GetMilliseconds();
	conn->last_got_packet = g_current_ms;

	if (syn) {
		return 0;
	}

	// seqnr is the number of packets past the expected
	// packet this is. ack_nr is the last acked, seq_nr is the
	// current. Subtracring 1 makes 0 mean "this is the next
	// expected packet".
	const uint seqnr = (pk_seq_nr - conn->ack_nr - 1) & SEQ_NR_MASK;

	// Getting an invalid sequence number?
	if (seqnr >= REORDER_BUFFER_MAX_SIZE) {
		if (seqnr >= (SEQ_NR_MASK + 1) - REORDER_BUFFER_MAX_SIZE && pk_flags != ST_STATE) {
			conn->ack_time = g_current_ms + min<uint>(conn->ack_time - g_current_ms, DELAYED_ACK_TIME_THRESHOLD);
		}
		LOG_UTPV("    Got old Packet/Ack (%u/%u)=%u!", pk_seq_nr, conn->ack_nr, seqnr);
		return 0;
	}

	// Process acknowledgment
	// acks is the number of packets that was acked
	int acks = (pk_ack_nr - (conn->seq_nr - 1 - conn->cur_window_packets)) & ACK_NR_MASK;

	// this happens when we receive an old ack nr
	if (acks > conn->cur_window_packets) acks = 0;

	// if we get the same ack_nr as in the last packet
	// increase the duplicate_ack counter, otherwise reset
	// it to 0
	if (conn->cur_window_packets > 0) {
		if (pk_ack_nr == ((conn->seq_nr - conn->cur_window_packets - 1) & ACK_NR_MASK) &&
			conn->cur_window_packets > 0) {
			//++conn->duplicate_ack;
		} else {
			conn->duplicate_ack = 0;
		}

		// TODO: if duplicate_ack == DUPLICATE_ACK_BEFORE_RESEND
		// and fast_resend_seq_nr <= ack_nr + 1
		//    resend ack_nr + 1
	}

	// figure out how many bytes were acked
	size_t acked_bytes = 0;

	// the minimum rtt of all acks
	// this is the upper limit on the delay we get back
	// from the other peer. Our delay cannot exceed
	// the rtt of the packet. If it does, clamp it.
	// this is done in apply_ledbat_ccontrol()
	int64 min_rtt = INT64_MAX;

	for (int i = 0; i < acks; ++i) {
		int seq = conn->seq_nr - conn->cur_window_packets + i;
		OutgoingPacket *pkt = (OutgoingPacket*)conn->outbuf.get(seq);
		if (pkt == 0 || pkt->transmissions == 0) continue;
		assert((int)(pkt->payload) >= 0);
		acked_bytes += pkt->payload;
		min_rtt = min<int64>(min_rtt, UTP_GetMicroseconds() - pkt->time_sent);
	}
	
	// count bytes acked by EACK
	if (selack_ptr != NULL) {
		acked_bytes += conn->selective_ack_bytes((pk_ack_nr + 2) & ACK_NR_MASK,
												 selack_ptr, selack_ptr[-1], min_rtt);
	}

	LOG_UTPV("0x%08x: acks:%d acked_bytes:%u seq_nr:%d cur_window:%u cur_window_packets:%u relative_seqnr:%u max_window:%u min_rtt:%u rtt:%u",
			 conn, acks, (uint)acked_bytes, conn->seq_nr, (uint)conn->cur_window, conn->cur_window_packets,
			 seqnr, (uint)conn->max_window, (uint)(min_rtt / 1000), conn->rtt);

	uint64 p;

	if (conn->version == 0) {
		p = uint64(pf->tv_sec) * 1000000 + pf->tv_usec;
	} else {
		p = pf1->tv_usec;
	}

	conn->last_measured_delay = g_current_ms;

	// get delay in both directions
	// record the delay to report back
	const uint32 their_delay = (uint32)(p == 0 ? 0 : time - p);
	conn->reply_micro = their_delay;
	uint32 prev_delay_base = conn->their_hist.delay_base;
	if (their_delay != 0) conn->their_hist.add_sample(their_delay);

	// if their new delay base is less than their previous one
	// we should shift our delay base in the other direction in order
	// to take the clock skew into account
	if (prev_delay_base != 0 &&
		wrapping_compare_less(conn->their_hist.delay_base, prev_delay_base)) {
		// never adjust more than 10 milliseconds
		if (prev_delay_base - conn->their_hist.delay_base <= 10000) {
			conn->our_hist.shift(prev_delay_base - conn->their_hist.delay_base);
		}
	}

	const uint32 actual_delay = conn->version==0
		?(pf->reply_micro==INT_MAX?0:uint32(pf->reply_micro))
		:(uint32(pf1->reply_micro)==INT_MAX?0:uint32(pf1->reply_micro));

	// if the actual delay is 0, it means the other end
	// hasn't received a sample from us yet, and doesn't
	// know what it is. We can't update out history unless
	// we have a true measured sample
	prev_delay_base = conn->our_hist.delay_base;
	if (actual_delay != 0) conn->our_hist.add_sample(actual_delay);

	// if our new delay base is less than our previous one
	// we should shift the other end's delay base in the other
	// direction in order to take the clock skew into account
	// This is commented out because it creates bad interactions
	// with our adjustment in the other direction. We don't really
	// need our estimates of the other peer to be very accurate
	// anyway. The problem with shifting here is that we're more
	// likely shift it back later because of a low latency. This
	// second shift back would cause us to shift our delay base
	// which then get's into a death spiral of shifting delay bases
/*	if (prev_delay_base != 0 &&
		wrapping_compare_less(conn->our_hist.delay_base, prev_delay_base)) {
		// never adjust more than 10 milliseconds
		if (prev_delay_base - conn->our_hist.delay_base <= 10000) {
			conn->their_hist.Shift(prev_delay_base - conn->our_hist.delay_base);
		}
	}
*/

	// if the delay estimate exceeds the RTT, adjust the base_delay to
	// compensate
	if (conn->our_hist.get_value() > uint32(min_rtt)) {
		conn->our_hist.shift(conn->our_hist.get_value() - min_rtt);
	}

	// only apply the congestion controller on acks
	// if we don't have a delay measurement, there's
	// no point in invoking the congestion control
	if (actual_delay != 0 && acked_bytes >= 1)
		conn->apply_ledbat_ccontrol(acked_bytes, actual_delay, min_rtt);

	// sanity check, the other end should never ack packets
	// past the point we've sent
	if (acks <= conn->cur_window_packets) {
		conn->max_window_user = conn->version == 0
			? pf->windowsize * PACKET_SIZE : pf1->windowsize;

		// If max user window is set to 0, then we startup a timer
		// That will reset it to 1 after 15 seconds.
		if (conn->max_window_user == 0)
			// Reset max_window_user to 1 every 15 seconds.
			conn->zerowindow_time = g_current_ms + 15000;

		// Respond to connect message
		// Switch to CONNECTED state.
		if (conn->state == CS_SYN_SENT) {
			conn->state = CS_CONNECTED;
			conn->func.on_state(conn->userdata, UTP_STATE_CONNECT);

		// We've sent a fin, and everything was ACKed (including the FIN),
		// it's safe to destroy the socket. cur_window_packets == acks
		// means that this packet acked all the remaining packets that
		// were in-flight.
		} else if (conn->state == CS_FIN_SENT && conn->cur_window_packets == acks) {
			conn->state = CS_DESTROY;
		}

		// Update fast resend counter
		if (wrapping_compare_less(conn->fast_resend_seq_nr, (pk_ack_nr + 1) & ACK_NR_MASK))
			conn->fast_resend_seq_nr = pk_ack_nr + 1;

		LOG_UTPV("0x%08x: fast_resend_seq_nr:%u", conn, conn->fast_resend_seq_nr);

		for (int i = 0; i < acks; ++i) {
			int ack_status = conn->ack_packet(conn->seq_nr - conn->cur_window_packets);
			// if ack_status is 0, the packet was acked.
			// if acl_stauts is 1, it means that the packet had already been acked
			// if it's 2, the packet has not been sent yet
			// We need to break this loop in the latter case. This could potentially
			// happen if we get an ack_nr that does not exceed what we have stuffed
			// into the outgoing buffer, but does exceed what we have sent
			if (ack_status == 2) {
#ifdef _DEBUG
				OutgoingPacket* pkt = (OutgoingPacket*)conn->outbuf.get(conn->seq_nr - conn->cur_window_packets);
				assert(pkt->transmissions == 0);
#endif
				break;
			}
			conn->cur_window_packets--;
		}
#ifdef _DEBUG
		if (conn->cur_window_packets == 0) assert(conn->cur_window == 0);
#endif

		// packets in front of this may have been acked by a
		// selective ack (EACK). Keep decreasing the window packet size
		// until we hit a packet that is still waiting to be acked
		// in the send queue
		// this is especially likely to happen when the other end
		// has the EACK send bug older versions of uTP had
		while (conn->cur_window_packets > 0 && !conn->outbuf.get(conn->seq_nr - conn->cur_window_packets))
			conn->cur_window_packets--;

#ifdef _DEBUG
		if (conn->cur_window_packets == 0) assert(conn->cur_window == 0);
#endif

		// this invariant should always be true
		assert(conn->cur_window_packets == 0 || conn->outbuf.get(conn->seq_nr - conn->cur_window_packets));

		// flush Nagle
		if (conn->cur_window_packets == 1) {
			OutgoingPacket *pkt = (OutgoingPacket*)conn->outbuf.get(conn->seq_nr - 1);
			// do we still have quota?
			if (pkt->transmissions == 0 &&
				(!(USE_PACKET_PACING) || conn->send_quota / 100 >= (int32)pkt->length)) {
				conn->send_packet(pkt);

				// No need to send another ack if there is nothing to reorder.
				if (conn->reorder_count == 0) {
					conn->sent_ack();
				}
			}
		}

		// Fast timeout-retry
		if (conn->fast_timeout) {
			LOG_UTPV("Fast timeout %u,%u,%u?", (uint)conn->cur_window, conn->seq_nr - conn->timeout_seq_nr, conn->timeout_seq_nr);
			// if the fast_resend_seq_nr is not pointing to the oldest outstanding packet, it suggests that we've already
			// resent the packet that timed out, and we should leave the fast-timeout mode.
			if (((conn->seq_nr - conn->cur_window_packets) & ACK_NR_MASK) != conn->fast_resend_seq_nr) {
				conn->fast_timeout = false;
			} else {
				// resend the oldest packet and increment fast_resend_seq_nr
				// to not allow another fast resend on it again
				OutgoingPacket *pkt = (OutgoingPacket*)conn->outbuf.get(conn->seq_nr - conn->cur_window_packets);
				if (pkt && pkt->transmissions > 0) {
					LOG_UTPV("0x%08x: Packet %u fast timeout-retry.", conn, conn->seq_nr - conn->cur_window_packets);
#ifdef _DEBUG
					++conn->_stats._fastrexmit;
#endif
					conn->fast_resend_seq_nr++;
					conn->send_packet(pkt);
				}
			}
		}
	}

	// Process selective acknowledgent
	if (selack_ptr != NULL) {
		conn->selective_ack(pk_ack_nr + 2, selack_ptr, selack_ptr[-1]);
	}

	// this invariant should always be true
	assert(conn->cur_window_packets == 0 || conn->outbuf.get(conn->seq_nr - conn->cur_window_packets));

	LOG_UTPV("0x%08x: acks:%d acked_bytes:%u seq_nr:%u cur_window:%u cur_window_packets:%u quota:%d",
			 conn, acks, (uint)acked_bytes, conn->seq_nr, (uint)conn->cur_window, conn->cur_window_packets,
			 conn->send_quota / 100);

	// In case the ack dropped the current window below
	// the max_window size, Mark the socket as writable
	if (conn->state == CS_CONNECTED_FULL && conn->is_writable(conn->get_packet_size())) {
		conn->state = CS_CONNECTED;
		LOG_UTPV("0x%08x: Socket writable. max_window:%u cur_window:%u quota:%d packet_size:%u",
				 conn, (uint)conn->max_window, (uint)conn->cur_window, conn->send_quota / 100, (uint)conn->get_packet_size());
		conn->func.on_state(conn->userdata, UTP_STATE_WRITABLE);
	}

	if (pk_flags == ST_STATE) {
		// This is a state packet only.
		return 0;
	}

	// The connection is not in a state that can accept data?
	if (conn->state != CS_CONNECTED &&
		conn->state != CS_CONNECTED_FULL &&
		conn->state != CS_FIN_SENT) {
		return 0;
	}

	// Is this a finalize packet?
	if (pk_flags == ST_FIN && !conn->got_fin) {
		LOG_UTPV("Got FIN eof_pkt:%u", pk_seq_nr);
		conn->got_fin = true;
		conn->eof_pkt = pk_seq_nr;
		// at this point, it is possible for the
		// other end to have sent packets with
		// sequence numbers higher than seq_nr.
		// if this is the case, our reorder_count
		// is out of sync. This case is dealt with
		// when we re-order and hit the eof_pkt.
		// we'll just ignore any packets with
		// sequence numbers past this
	}

	// Getting an in-order packet?
	if (seqnr == 0) {
		size_t count = packet_end - data;
		if (count > 0 && conn->state != CS_FIN_SENT) {
			LOG_UTPV("0x%08x: Got Data len:%u (rb:%u)", conn, (uint)count, (uint)conn->func.get_rb_size(conn->userdata));
			// Post bytes to the upper layer
			conn->func.on_read(conn->userdata, data, count);
		}
		conn->ack_nr++;
		conn->bytes_since_ack += count;

		// Check if the next packet has been received too, but waiting
		// in the reorder buffer.
		for (;;) {

			if (conn->got_fin && conn->eof_pkt == conn->ack_nr) {
				if (conn->state != CS_FIN_SENT) {
					conn->state = CS_GOT_FIN;
					conn->rto_timeout = g_current_ms + min<uint>(conn->rto * 3, 60);

					LOG_UTPV("0x%08x: Posting EOF", conn);
					conn->func.on_state(conn->userdata, UTP_STATE_EOF);
				}

				// if the other end wants to close, ack immediately
				conn->send_ack();

				// reorder_count is not necessarily 0 at this point.
				// even though it is most of the time, the other end
				// may have sent packets with higher sequence numbers
				// than what later end up being eof_pkt
				// since we have received all packets up to eof_pkt
				// just ignore the ones after it.
				conn->reorder_count = 0;
			}

			// Quick get-out in case there is nothing to reorder
			if (conn->reorder_count == 0)
				break;

			// Check if there are additional buffers in the reorder buffers
			// that need delivery.
			byte *p = (byte*)conn->inbuf.get(conn->ack_nr+1);
			if (p == NULL)
				break;
			conn->inbuf.put(conn->ack_nr+1, NULL);
			count = *(uint*)p;
			if (count > 0 && conn->state != CS_FIN_SENT) {
				// Pass the bytes to the upper layer
				conn->func.on_read(conn->userdata, p + sizeof(uint), count);
			}
			conn->ack_nr++;
			conn->bytes_since_ack += count;

			// Free the element from the reorder buffer
			free(p);
			assert(conn->reorder_count > 0);
			conn->reorder_count--;
		}

		// start the delayed ACK timer
		conn->ack_time = g_current_ms + min<uint>(conn->ack_time - g_current_ms, DELAYED_ACK_TIME_THRESHOLD);
	} else {
		// Getting an out of order packet.
		// The packet needs to be remembered and rearranged later.

		// if we have received a FIN packet, and the EOF-sequence number
		// is lower than the sequence number of the packet we just received
		// something is wrong.
		if (conn->got_fin && pk_seq_nr > conn->eof_pkt) {
			LOG_UTPV("0x%08x: Got an invalid packet sequence number, past EOF "
				"reorder_count:%u len:%u (rb:%u)",
				conn, conn->reorder_count, (uint)(packet_end - data), (uint)conn->func.get_rb_size(conn->userdata));
			return 0;
		}

		// if the sequence number is entirely off the expected
		// one, just drop it. We can't allocate buffer space in
		// the inbuf entirely based on untrusted input
		if (seqnr > 0x3ff) {
			LOG_UTPV("0x%08x: Got an invalid packet sequence number, too far off "
				"reorder_count:%u len:%u (rb:%u)",
				conn, conn->reorder_count, (uint)(packet_end - data), (uint)conn->func.get_rb_size(conn->userdata));
			return 0;
		}

		// we need to grow the circle buffer before we
		// check if the packet is already in here, so that
		// we don't end up looking at an older packet (since
		// the indices wraps around).
		conn->inbuf.ensure_size(pk_seq_nr + 1, seqnr + 1);

		// Has this packet already been received? (i.e. a duplicate)
		// If that is the case, just discard it.
		if (conn->inbuf.get(pk_seq_nr) != NULL) {
#ifdef _DEBUG
			++conn->_stats._nduprecv;
#endif
			return 0;
		}

		// Allocate memory to fit the packet that needs to re-ordered
		byte *mem = (byte*)malloc((packet_end - data) + sizeof(uint));
		*(uint*)mem = (uint)(packet_end - data);
		memcpy(mem + sizeof(uint), data, packet_end - data);

		// Insert into reorder buffer and increment the count
		// of # of packets to be reordered.
		// we add one to seqnr in order to leave the last
		// entry empty, that way the assert in send_ack
		// is valid. we have to add one to seqnr too, in order
		// to make the circular buffer grow around the correct
		// point (which is conn->ack_nr + 1).
		assert(conn->inbuf.get(pk_seq_nr) == NULL);
		assert((pk_seq_nr & conn->inbuf.mask) != ((conn->ack_nr+1) & conn->inbuf.mask));
		conn->inbuf.put(pk_seq_nr, mem);
		conn->reorder_count++;

		LOG_UTPV("0x%08x: Got out of order data reorder_count:%u len:%u (rb:%u)",
			conn, conn->reorder_count, (uint)(packet_end - data), (uint)conn->func.get_rb_size(conn->userdata));

		// Setup so the partial ACK message will get sent immediately.
		conn->ack_time = g_current_ms + min<uint>(conn->ack_time - g_current_ms, 1);
	}

	// If ack_time or ack_bytes indicate that we need to send and ack, send one
	// here instead of waiting for the timer to trigger
	LOG_UTPV("bytes_since_ack:%u ack_time:%d",
			 (uint)conn->bytes_since_ack, (int)(g_current_ms - conn->ack_time));
	if (conn->state == CS_CONNECTED || conn->state == CS_CONNECTED_FULL) {
		if (conn->bytes_since_ack > DELAYED_ACK_BYTE_THRESHOLD ||
			(int)(g_current_ms - conn->ack_time) >= 0) {
			conn->send_ack();
		}
	}
	return (size_t)(packet_end - data);
}

inline bool UTP_IsV1(PacketFormatV1 const* pf)
{
	return pf->version() == 1 && pf->type() < ST_NUM_STATES && pf->ext < 3;
}

void UTP_Free(UTPSocket *conn)
{
	LOG_UTPV("0x%08x: Killing socket", conn);

	conn->func.on_state(conn->userdata, UTP_STATE_DESTROYING);
	UTP_SetCallbacks(conn, NULL, NULL);

	assert(conn->idx < g_utp_sockets.GetCount());
	assert(g_utp_sockets[conn->idx] == conn);

	// Unlink object from the global list
	assert(g_utp_sockets.GetCount() > 0);

	UTPSocket *last = g_utp_sockets[g_utp_sockets.GetCount() - 1];

	assert(last->idx < g_utp_sockets.GetCount());
	assert(g_utp_sockets[last->idx] == last);

	last->idx = conn->idx;
	
	g_utp_sockets[conn->idx] = last;

	// Decrease the count
	g_utp_sockets.SetCount(g_utp_sockets.GetCount() - 1);

	// Free all memory occupied by the socket object.
	for (size_t i = 0; i <= conn->inbuf.mask; i++) {
		free(conn->inbuf.elements[i]);
	}
	for (size_t i = 0; i <= conn->outbuf.mask; i++) {
		free(conn->outbuf.elements[i]);
	}
	free(conn->inbuf.elements);
	free(conn->outbuf.elements);

	// Finally free the socket object
	free(conn);
}


// Public functions:
///////////////////////////////////////////////////////////////////////////////

// Create a UTP socket
UTPSocket *UTP_Create(SendToProc *send_to_proc, void *send_to_userdata, const struct sockaddr *addr, socklen_t addrlen)
{
	UTPSocket *conn = (UTPSocket*)calloc(1, sizeof(UTPSocket));

	g_current_ms = UTP_GetMilliseconds();

	UTP_SetCallbacks(conn, NULL, NULL);
	conn->our_hist.clear();
	conn->their_hist.clear();
	conn->rto = 3000;
	conn->rtt_var = 800;
	conn->seq_nr = 1;
	conn->ack_nr = 0;
	conn->max_window_user = 255 * PACKET_SIZE;
	conn->addr = PackedSockAddr((const SOCKADDR_STORAGE*)addr, addrlen);
	conn->send_to_proc = send_to_proc;
	conn->send_to_userdata = send_to_userdata;
	conn->ack_time = g_current_ms + 0x70000000;
	conn->last_got_packet = g_current_ms;
	conn->last_sent_packet = g_current_ms;
	conn->last_measured_delay = g_current_ms + 0x70000000;
	conn->last_rwin_decay = int32(g_current_ms) - MAX_WINDOW_DECAY;
	conn->last_send_quota = g_current_ms;
	conn->send_quota = PACKET_SIZE * 100;
	conn->cur_window_packets = 0;
	conn->fast_resend_seq_nr = conn->seq_nr;

	// default to version 1
	UTP_SetSockopt(conn, SO_UTPVERSION, 1);

	// we need to fit one packet in the window
	// when we start the connection
	conn->max_window = conn->get_packet_size();
	conn->state = CS_IDLE;

	conn->outbuf.mask = 15;
	conn->inbuf.mask = 15;

	conn->outbuf.elements = (void**)calloc(16, sizeof(void*));
	conn->inbuf.elements = (void**)calloc(16, sizeof(void*));

	conn->idx = g_utp_sockets.Append(conn);

	LOG_UTPV("0x%08x: UTP_Create", conn);

	return conn;
}

void UTP_SetCallbacks(UTPSocket *conn, UTPFunctionTable *funcs, void *userdata)
{
	assert(conn);

	if (funcs == NULL) {
		funcs = &zero_funcs;
	}
	conn->func = *funcs;
	conn->userdata = userdata;
}

bool UTP_SetSockopt(UTPSocket* conn, int opt, int val)
{
	assert(conn);

	switch (opt) {
	case SO_SNDBUF:
		assert(val >= 1);
		conn->opt_sndbuf = val;
		return true;
	case SO_RCVBUF:
		conn->opt_rcvbuf = val;
		return true;
	case SO_UTPVERSION:
		assert(conn->state == CS_IDLE);
		if (conn->state != CS_IDLE) {
			// too late
			return false;
		}
		if (conn->version == 1 && val == 0) {
			conn->reply_micro = INT_MAX;
			conn->opt_rcvbuf = 200 * 1024;
			conn->opt_sndbuf = OUTGOING_BUFFER_MAX_SIZE * PACKET_SIZE;
		} else if (conn->version == 0 && val == 1) {
			conn->reply_micro = 0;
			conn->opt_rcvbuf = 3 * 1024 * 1024 + 512 * 1024;
			conn->opt_sndbuf = conn->opt_rcvbuf;
		}
		conn->version = val;
		return true;
	}

	return false;
}

// Try to connect to a specified host.
// 'initial' is the number of data bytes to send in the connect packet.
void UTP_Connect(UTPSocket *conn)
{
	assert(conn);

	assert(conn->state == CS_IDLE);
	assert(conn->cur_window_packets == 0);
	assert(conn->outbuf.get(conn->seq_nr) == NULL);
	assert(sizeof(PacketFormatV1) == 20);

	conn->state = CS_SYN_SENT;

	g_current_ms = UTP_GetMilliseconds();

	// Create and send a connect message
	uint32 conn_seed = UTP_Random();

	// we identify newer versions by setting the
	// first two bytes to 0x0001
	if (conn->version > 0) {
		conn_seed &= 0xffff;
	}

	// used in parse_log.py
	LOG_UTP("0x%08x: UTP_Connect conn_seed:%u packet_size:%u (B) "
			"target_delay:%u (ms) delay_history:%u "
			"delay_base_history:%u (minutes)",
			conn, conn_seed, PACKET_SIZE, CCONTROL_TARGET / 1000,
			CUR_DELAY_SIZE, DELAY_BASE_HISTORY);

	// Setup initial timeout timer.
	conn->retransmit_timeout = 3000;
	conn->rto_timeout = g_current_ms + conn->retransmit_timeout;
	conn->last_rcv_win = conn->get_rcv_window();

	conn->conn_seed = conn_seed;
	conn->conn_id_recv = conn_seed;
	conn->conn_id_send = conn_seed+1;
	// if you need compatibiltiy with 1.8.1, use this. it increases attackability though.
	//conn->seq_nr = 1;
	conn->seq_nr = UTP_Random();

	// Create the connect packet.
	const size_t header_ext_size = conn->get_header_extensions_size();

	OutgoingPacket *pkt = (OutgoingPacket*)malloc(sizeof(OutgoingPacket) - 1 + header_ext_size);

	PacketFormatExtensions* p = (PacketFormatExtensions*)pkt->data;
	PacketFormatExtensionsV1* p1 = (PacketFormatExtensionsV1*)pkt->data;

	memset(p, 0, header_ext_size);
	// SYN packets are special, and have the receive ID in the connid field,
	// instead of conn_id_send.
	if (conn->version == 0) {
		p->pf.connid = conn->conn_id_recv;
		p->pf.ext = 2;
		p->pf.windowsize = (byte)DIV_ROUND_UP(conn->last_rcv_win, PACKET_SIZE);
		p->pf.seq_nr = conn->seq_nr;
		p->pf.flags = ST_SYN;
		p->ext_next = 0;
		p->ext_len = 8;
		memset(p->extensions, 0, 8);
	} else {
		p1->pf.set_version(1);
		p1->pf.set_type(ST_SYN);
		p1->pf.ext = 2;
		p1->pf.connid = conn->conn_id_recv;
		p1->pf.windowsize = (uint32)conn->last_rcv_win;
		p1->pf.seq_nr = conn->seq_nr;
		p1->ext_next = 0;
		p1->ext_len = 8;
		memset(p1->extensions, 0, 8);
	}
	pkt->transmissions = 0;
	pkt->length = header_ext_size;
	pkt->payload = 0;

	//LOG_UTPV("0x%08x: Sending connect %s [%u].",
	//		 conn, addrfmt(conn->addr, addrbuf), conn_seed);

	// Remember the message in the outgoing queue.
	conn->outbuf.ensure_size(conn->seq_nr, conn->cur_window_packets);
	conn->outbuf.put(conn->seq_nr, pkt);
	conn->seq_nr++;
	conn->cur_window_packets++;

	conn->send_packet(pkt);
}

bool UTP_IsIncomingUTP(UTPGotIncomingConnection *incoming_proc,
					   SendToProc *send_to_proc, void *send_to_userdata,
					   const byte *buffer, size_t len, const struct sockaddr *to, socklen_t tolen)
{
	const PackedSockAddr addr((const SOCKADDR_STORAGE*)to, tolen);

	if (len < sizeof(PacketFormat) && len < sizeof(PacketFormatV1)) {
		LOG_UTPV("recv %s len:%u too small", addrfmt(addr, addrbuf), (uint)len);
		return false;
	}

	const PacketFormat* p = (PacketFormat*)buffer;
	const PacketFormatV1* p1 = (PacketFormatV1*)buffer;

	const byte version = UTP_IsV1(p1);
	const uint32 id = (version == 0) ? p->connid : uint32(p1->connid);

	if (version == 0 && len < sizeof(PacketFormat)) {
		LOG_UTPV("recv %s len:%u version:%u too small", addrfmt(addr, addrbuf), (uint)len, version);
		return false;
	}

	if (version == 1 && len < sizeof(PacketFormatV1)) {
		LOG_UTPV("recv %s len:%u version:%u too small", addrfmt(addr, addrbuf), (uint)len, version);
		return false;
	}

	LOG_UTPV("recv %s len:%u id:%u", addrfmt(addr, addrbuf), (uint)len, id);

	const PacketFormat *pf = (PacketFormat*)p;
	const PacketFormatV1 *pf1 = (PacketFormatV1*)p;

	if (version == 0) {
		LOG_UTPV("recv id:%u seq_nr:%u ack_nr:%u", id, (uint)pf->seq_nr, (uint)pf->ack_nr);
	} else {
		LOG_UTPV("recv id:%u seq_nr:%u ack_nr:%u", id, (uint)pf1->seq_nr, (uint)pf1->ack_nr);
	}

	const byte flags = version == 0 ? pf->flags : pf1->type();

	for (size_t i = 0; i < g_utp_sockets.GetCount(); i++) {
		UTPSocket *conn = g_utp_sockets[i];
		//LOG_UTPV("Examining UTPSocket %s for %s and (seed:%u s:%u r:%u) for %u",
		//		addrfmt(conn->addr, addrbuf), addrfmt(addr, addrbuf2), conn->conn_seed, conn->conn_id_send, conn->conn_id_recv, id);
		if (conn->addr != addr)
			continue;

		if (flags == ST_RESET && (conn->conn_id_send == id || conn->conn_id_recv == id)) {
			LOG_UTPV("0x%08x: recv RST for existing connection", conn);
			if (!conn->userdata || conn->state == CS_FIN_SENT) {
				conn->state = CS_DESTROY;
			} else {
				conn->state = CS_RESET;
			}
			if (conn->userdata) {
				conn->func.on_overhead(conn->userdata, false, len + conn->get_udp_overhead(),
									   close_overhead);
				const int err = conn->state == CS_SYN_SENT ?
					ECONNREFUSED :
					ECONNRESET;
				conn->func.on_error(conn->userdata, err);
			}
			return true;
		} else if (flags != ST_SYN && conn->conn_id_recv == id) {
			LOG_UTPV("0x%08x: recv processing", conn);
			const size_t read = UTP_ProcessIncoming(conn, buffer, len);
			if (conn->userdata) {
				conn->func.on_overhead(conn->userdata, false,
					(len - read) + conn->get_udp_overhead(),
					header_overhead);
			}
			return true;
		}
	}

	if (flags == ST_RESET) {
		LOG_UTPV("recv RST for unknown connection");
		return true;
	}

	const uint32 seq_nr = version == 0 ? pf->seq_nr : pf1->seq_nr;
	if (flags != ST_SYN) {
		for (size_t i = 0; i < g_rst_info.GetCount(); i++) {
			if (g_rst_info[i].connid != id)
				continue;
			if (g_rst_info[i].addr != addr)
				continue;
			if (seq_nr != g_rst_info[i].ack_nr)
				continue;
			g_rst_info[i].timestamp = UTP_GetMilliseconds();
			LOG_UTPV("recv not sending RST to non-SYN (stored)");
			return true;
		}
		if (g_rst_info.GetCount() > RST_INFO_LIMIT) {
			LOG_UTPV("recv not sending RST to non-SYN (limit at %u stored)", (uint)g_rst_info.GetCount());
			return true;
		}
		LOG_UTPV("recv send RST to non-SYN (%u stored)", (uint)g_rst_info.GetCount());
		RST_Info &r = g_rst_info.Append();
		r.addr = addr;
		r.connid = id;
		r.ack_nr = seq_nr;
		r.timestamp = UTP_GetMilliseconds();

		UTPSocket::send_rst(send_to_proc, send_to_userdata, addr, id, seq_nr, UTP_Random(), version);
		return true;
	}

	if (incoming_proc) {
		LOG_UTPV("Incoming connection from %s uTP version:%u", addrfmt(addr, addrbuf), version);

		// Create a new UTP socket to handle this new connection
		UTPSocket *conn = UTP_Create(send_to_proc, send_to_userdata, to, tolen);
		// Need to track this value to be able to detect duplicate CONNECTs
		conn->conn_seed = id;
		// This is value that identifies this connection for them.
		conn->conn_id_send = id;
		// This is value that identifies this connection for us.
		conn->conn_id_recv = id+1;
		conn->ack_nr = seq_nr;
		conn->seq_nr = UTP_Random();
		conn->fast_resend_seq_nr = conn->seq_nr;

		UTP_SetSockopt(conn, SO_UTPVERSION, version);
		conn->state = CS_CONNECTED;

		const size_t read = UTP_ProcessIncoming(conn, buffer, len, true);

		LOG_UTPV("0x%08x: recv send connect ACK", conn);
		conn->send_ack(true);

		incoming_proc(send_to_userdata, conn);

		// we report overhead after incoming_proc, because the callbacks are setup now
		if (conn->userdata) {
			// SYN
			conn->func.on_overhead(conn->userdata, false, (len - read) + conn->get_udp_overhead(),
								   header_overhead);
			// SYNACK
			conn->func.on_overhead(conn->userdata, true, conn->get_overhead(),
								   ack_overhead);
		}
	}

	return true;
}

bool UTP_HandleICMP(const byte* buffer, size_t len, const struct sockaddr *to, socklen_t tolen)
{
	const PackedSockAddr addr((const SOCKADDR_STORAGE*)to, tolen);

	// Want the whole packet so we have connection ID
	if (len < sizeof(PacketFormat)) {
		return false;
	}

	const PacketFormat* p = (PacketFormat*)buffer;
	const PacketFormatV1* p1 = (PacketFormatV1*)buffer;

	const byte version = UTP_IsV1(p1);
	const uint32 id = (version == 0) ? p->connid : uint32(p1->connid);

	for (size_t i = 0; i < g_utp_sockets.GetCount(); ++i) {
		UTPSocket *conn = g_utp_sockets[i];
		if (conn->addr == addr &&
			conn->conn_id_recv == id) {
			// Don't pass on errors for idle/closed connections
			if (conn->state != CS_IDLE) {
				if (!conn->userdata || conn->state == CS_FIN_SENT) {
					LOG_UTPV("0x%08x: icmp packet causing socket destruction", conn);
					conn->state = CS_DESTROY;
				} else {
					conn->state = CS_RESET;
				}
				if (conn->userdata) {
					const int err = conn->state == CS_SYN_SENT ?
						ECONNREFUSED :
						ECONNRESET;
					LOG_UTPV("0x%08x: icmp packet causing error on socket:%d", conn, err);
					conn->func.on_error(conn->userdata, err);
				}
			}
			return true;
		}
	}
	return false;
}

// Write bytes to the UTP socket.
// Returns true if the socket is still writable.
bool UTP_Write(UTPSocket *conn, size_t bytes)
{
	assert(conn);

#ifdef g_log_utp_verbose
	size_t param = bytes;
#endif

	if (conn->state != CS_CONNECTED) {
		LOG_UTPV("0x%08x: UTP_Write %u bytes = false (not CS_CONNECTED)", conn, (uint)bytes);
		return false;
	}

	g_current_ms = UTP_GetMilliseconds();

	conn->update_send_quota();

	// don't send unless it will all fit in the window
	size_t packet_size = conn->get_packet_size();
	size_t num_to_send = min<size_t>(bytes, packet_size);
	while (conn->is_writable(num_to_send)) {
		// Send an outgoing packet.
		// Also add it to the outgoing of packets that have been sent but not ACKed.

		if (num_to_send == 0) {
			LOG_UTPV("0x%08x: UTP_Write %u bytes = true", conn, (uint)param);
			return true;
		}
		bytes -= num_to_send;

		LOG_UTPV("0x%08x: Sending packet. seq_nr:%u ack_nr:%u wnd:%u/%u/%u rcv_win:%u size:%u quota:%d cur_window_packets:%u",
				 conn, conn->seq_nr, conn->ack_nr,
				 (uint)(conn->cur_window + num_to_send),
				 (uint)conn->max_window, (uint)conn->max_window_user,
				 (uint)conn->last_rcv_win, num_to_send, conn->send_quota / 100,
				 conn->cur_window_packets);
		conn->write_outgoing_packet(num_to_send, ST_DATA);
		num_to_send = min<size_t>(bytes, packet_size);
	}

	// mark the socket as not being writable.
	conn->state = CS_CONNECTED_FULL;
	LOG_UTPV("0x%08x: UTP_Write %u bytes = false", conn, (uint)bytes);
	return false;
}

void UTP_RBDrained(UTPSocket *conn)
{
	assert(conn);

	const size_t rcvwin = conn->get_rcv_window();

	if (rcvwin > conn->last_rcv_win) {
		// If last window was 0 send ACK immediately, otherwise should set timer
		if (conn->last_rcv_win == 0) {
			conn->send_ack();
		} else {
			conn->ack_time = g_current_ms + min<uint>(conn->ack_time - g_current_ms, DELAYED_ACK_TIME_THRESHOLD);
		}
	}
}

void UTP_CheckTimeouts()
{
	g_current_ms = UTP_GetMilliseconds();

	for (size_t i = 0; i < g_rst_info.GetCount(); i++) {
		if ((int)(g_current_ms - g_rst_info[i].timestamp) >= RST_INFO_TIMEOUT) {
			g_rst_info.MoveUpLast(i);
			i--;
		}
	}
	if (g_rst_info.GetCount() != g_rst_info.GetAlloc()) {
		g_rst_info.Compact();
	}

	for (size_t i = 0; i != g_utp_sockets.GetCount(); i++) {
		UTPSocket *conn = g_utp_sockets[i];
		conn->check_timeouts();

		// Check if the object was deleted
		if (conn->state == CS_DESTROY) {
			LOG_UTPV("0x%08x: Destroying", conn);
			UTP_Free(conn);
			i--;
		}
	}
}

size_t UTP_GetPacketSize(UTPSocket *socket)
{
	return socket->get_packet_size();
}

void UTP_GetPeerName(UTPSocket *conn, struct sockaddr *addr, socklen_t *addrlen)
{
	assert(conn);

	socklen_t len;
	const SOCKADDR_STORAGE sa = conn->addr.get_sockaddr_storage(&len);
	*addrlen = min(len, *addrlen);
	memcpy(addr, &sa, *addrlen);
}

void UTP_GetDelays(UTPSocket *conn, int32 *ours, int32 *theirs, uint32 *age)
{
	assert(conn);

	if (ours) *ours = conn->our_hist.get_value();
	if (theirs) *theirs = conn->their_hist.get_value();
	if (age) *age = g_current_ms - conn->last_measured_delay;
}

#ifdef _DEBUG
void UTP_GetStats(UTPSocket *conn, UTPStats *stats)
{
	assert(conn);

	*stats = conn->_stats;
}
#endif // _DEBUG

void UTP_GetGlobalStats(UTPGlobalStats *stats)
{
	*stats = _global_stats;
}

// Close the UTP socket.
// It is not valid for the upper layer to refer to socket after it is closed.
// Data will keep to try being delivered after the close.
void UTP_Close(UTPSocket *conn)
{
	assert(conn);

	assert(conn->state != CS_DESTROY_DELAY && conn->state != CS_FIN_SENT && conn->state != CS_DESTROY);

	LOG_UTPV("0x%08x: UTP_Close in state:%s", conn, statenames[conn->state]);

	switch(conn->state) {
	case CS_CONNECTED:
	case CS_CONNECTED_FULL:
		conn->state = CS_FIN_SENT;
		conn->write_outgoing_packet(0, ST_FIN);
		break;

	case CS_SYN_SENT:
		conn->rto_timeout = UTP_GetMilliseconds() + min<uint>(conn->rto * 2, 60);
	case CS_GOT_FIN:
		conn->state = CS_DESTROY_DELAY;
		break;

	default:
		conn->state = CS_DESTROY;
		break;
	}
}
