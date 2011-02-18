#define CCONTROL_TARGET (100 * 1000) // us
#define RATE_CHECK_INTERVAL 10000 // ms
#define DYNAMIC_PACKET_SIZE_ENABLED false
#define DYNAMIC_PACKET_SIZE_FACTOR 2
// This should return the global number of bytes sent, used for determining dynamic
// packet size based on rate

#warning implement this in libtransmission
uint64 UTP_GetGlobalUTPBytesSent(const struct sockaddr *remote, socklen_t remotelen) { return 0; }

enum bandwidth_type_t {
	payload_bandwidth, connect_overhead,
	close_overhead, ack_overhead,
	header_overhead, retransmit_overhead
};

#ifdef WIN32
#define I64u "%I64u"
#else
#define I64u "%Lu"
#endif
#ifdef WIN32
#define snprintf _snprintf
#endif

#define g_log_utp 0
#define g_log_utp_verbose 0
void utp_log(char const* fmt, ...)
{
	/*
	printf("[%u] ", UTP_GetMilliseconds());
	va_list vl;
	va_start(vl, fmt);
	vprintf(fmt, vl);
	va_end(vl);
	puts("");
	fflush(stdout);
	*/
};
