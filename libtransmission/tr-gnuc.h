#ifndef TR_GNUC_H
#define TR_GNUC_H 1

#ifndef UNUSED
 #ifdef __GNUC__
  #define UNUSED __attribute__ ( ( unused ) )
 #else
  #define UNUSED
 #endif
#endif

#ifndef TR_GNUC_PRINTF
 #ifdef __GNUC__
  #define TR_GNUC_PRINTF( fmt,\
                          args ) __attribute__ ( ( format ( printf, fmt,\
                                                            args ) ) )
 #else
  #define TR_GNUC_PRINTF( fmt, args )
 #endif
#endif

#ifndef TR_GNUC_NULL_TERMINATED
 #if __GNUC__ >= 4
  #define TR_GNUC_NULL_TERMINATED __attribute__ ( ( __sentinel__ ) )
 #else
  #define TR_GNUC_NULL_TERMINATED
 #endif
#endif

#if __GNUC__ > 2 || ( __GNUC__ == 2 && __GNUC_MINOR__ >= 96 )
 #define TR_GNUC_CONST __attribute__ ( ( __const ) )
 #define TR_GNUC_PURE __attribute__ ( ( __pure__ ) )
 #define TR_GNUC_MALLOC __attribute__ ( ( __malloc__ ) )
#else
 #define TR_GNUC_CONST
 #define TR_GNUC_PURE
 #define TR_GNUC_MALLOC
#endif

#endif
