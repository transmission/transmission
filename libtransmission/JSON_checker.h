#ifndef JSON_CHECKER_H
#define JSON_CHECKER_H

/* JSON_checker.h */


#include <stddef.h>

/* Windows DLL stuff */
#ifdef _WIN32
#	ifdef JSON_CHECKER_DLL_EXPORTS
#		define JSON_CHECKER_DLL_API __declspec(dllexport)
#	else
#		define JSON_CHECKER_DLL_API __declspec(dllimport)
#   endif
#else
#	define JSON_CHECKER_DLL_API 
#endif

/* Determine the integer type use to parse non-floating point numbers */
#if __STDC_VERSION__ >= 199901L || HAVE_LONG_LONG == 1
typedef long long JSON_int_t;
#define JSON_CHECKER_INTEGER_SSCANF_TOKEN "%lld"
#define JSON_CHECKER_INTEGER_SPRINTF_TOKEN "%lld"
#else 
typedef long JSON_int_t;
#define JSON_CHECKER_INTEGER_SSCANF_TOKEN "%ld"
#define JSON_CHECKER_INTEGER_SPRINTF_TOKEN "%ld"
#endif


#ifdef __cplusplus
extern "C" {
#endif 

typedef enum 
{
    JSON_T_NONE = 0,
    JSON_T_ARRAY_BEGIN,
    JSON_T_ARRAY_END,
    JSON_T_OBJECT_BEGIN,
    JSON_T_OBJECT_END,
    JSON_T_INTEGER,
    JSON_T_FLOAT,
    JSON_T_NULL,
    JSON_T_TRUE,
    JSON_T_FALSE,
    JSON_T_STRING,
    JSON_T_KEY,
} JSON_type;

typedef struct JSON_value_struct {
    union {
        JSON_int_t integer_value;
        
        long double float_value;
        
        struct {
            const char* string_value;
            size_t string_length;
        };
    };
} JSON_value;

/*! \brief JSON parser callback 

    \param ctx The pointer passed to new_JSON_checker.
    \param type An element of JSON_type but not JSON_T_NONE.    
    \param value A representation of the parsed value. This parameter is NULL for
        JSON_T_ARRAY_BEGIN, JSON_T_ARRAY_END, JSON_T_OBJECT_BEGIN, JSON_T_OBJECT_END,
        JSON_T_NULL, JSON_T_TRUE, and SON_T_FALSE.

    \return Non-zero if parsing should continue, else zero.
*/    
typedef int (JSON_checker_callback)(void* ctx, int type, const struct JSON_value_struct* value);

/*! \brief Create a JSON parser object 
    
    \param depth If negative, the parser can parse arbitrary levels of JSON, otherwise
        the depth is the limit
    \param Pointer to a callback. This parameter may be NULL.
    \param Callback context. This parameter may be NULL.
    
    \return The parser object.
*/
JSON_CHECKER_DLL_API extern struct JSON_checker_struct* new_JSON_checker(int depth, JSON_checker_callback*, void* ctx, int allowComments);

/*! \brief Destroy a previously created JSON parser object. */
JSON_CHECKER_DLL_API extern void delete_JSON_checker(struct JSON_checker_struct* jc);

/*! \brief Parse a character.

    \return Non-zero, if all characters passed to this function are part of are valid JSON.
*/
JSON_CHECKER_DLL_API extern int JSON_checker_char(struct JSON_checker_struct* jc, int next_char);

/*! \brief Finalize parsing.

    Call this method once after all input characters have been consumed.
    
    \return Non-zero, if all parsed characters are valid JSON, zero otherwise.
*/
JSON_CHECKER_DLL_API extern int JSON_checker_done(struct JSON_checker_struct* jc);

/*! \brief Determine if a given string is valid JSON white space 

    \return Non-zero if the string is valid, zero otherwise.
*/
JSON_CHECKER_DLL_API extern int JSON_checker_is_legal_white_space_string(const char* s);


#ifdef __cplusplus
}
#endif 
    

#endif /* JSON_CHECKER_H */
