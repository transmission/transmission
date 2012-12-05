/* $XConsortium: wildmat.c,v 1.2 94/04/13 18:40:59 rws Exp $ */
/*
**
**  Do shell-style pattern matching for ?, \, [], and * characters.
**  Might not be robust in face of malformed patterns; e.g., "foo[a-"
**  could cause a segmentation violation. It is 8bit clean.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
**  Rich $alz is now <rsalz@bbn.com>.
**  April, 1991:  Replaced mutually-recursive calls with in-line code
**  for the star character.
**
**  Special thanks to Lars Mathiesen <thorinn@diku.dk> for the ABORT code.
**  This can greatly speed up failing wildcard patterns. For example:
**	pattern: -*-*-*-*-*-*-12-*-*-*-m-*-*-*
**	text 1:	 -adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1
**	text 2:	 -adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1
**  Text 1 matches with 51 calls, while text 2 fails with 54 calls. Without
**  the ABORT, then it takes 22310 calls to fail. Ugh. The following
**  explanation is from Lars:
**  The precondition that must be fulfilled is that DoMatch will consume
**  at least one character in text. This is true if *p is neither '*' nor
**  '\0'.)  The last return has ABORT instead of false to avoid quadratic
**  behaviour in cases like pattern "*a*b*c*d" with text "abcxxxxx". With
**  false, each star-loop has to run to the end of the text; with ABORT
**  only the last one does.
**
**  Once the control of one instance of DoMatch enters the star-loop, that
**  instance will return either true or ABORT, and any calling instance
**  will therefore return immediately after (without calling recursively
**  again). In effect, only one star-loop is ever active. It would be
**  possible to modify the code to maintain this context explicitly,
**  eliminating all recursive calls at the cost of some complication and
**  loss of clarity (and the ABORT stuff seems to be unclear enough by
**  itself). I think it would be unwise to try to get this into a
**  released version unless you have a good test data base to try it out
**  on.
*/

#include "transmission.h"
#include "utils.h"

#define ABORT -1

    /* What character marks an inverted character class? */
#define NEGATE_CLASS		'^'
    /* Is "*" a common pattern? */
#define OPTIMIZE_JUST_STAR
    /* Do tar (1) matching rules, which ignore a trailing slash? */
#undef MATCH_TAR_PATTERN


/*
**  Match text and p, return true, false, or ABORT.
*/
static int
DoMatch (const char * text, const char * p)
{
    register int	last;
    register int	matched;
    register int	reverse;

    for (; *p; text++, p++) {
	if (*text == '\0' && *p != '*')
	    return ABORT;
	switch (*p) {
	case '\\':
	    /* Literal match with following character. */
	    p++;
	    /* FALLTHROUGH */
	default:
	    if (*text != *p)
		return false;
	    continue;
	case '?':
	    /* Match anything. */
	    continue;
	case '*':
	    while (*++p == '*')
		/* Consecutive stars act just like one. */
		continue;
	    if (*p == '\0')
		/* Trailing star matches everything. */
		return true;
	    while (*text)
		if ((matched = DoMatch (text++, p)) != false)
		    return matched;
	    return ABORT;
	case '[':
	    reverse = p[1] == NEGATE_CLASS ? true : false;
	    if (reverse)
		/* Inverted character class. */
		p++;
	    for (last = 0400, matched = false; *++p && *p != ']'; last = *p)
		/* This next line requires a good C compiler. */
		if (*p == '-' ? *text <= *++p && *text >= last : *text == *p)
		    matched = true;
	    if (matched == reverse)
		return false;
	    continue;
	}
    }

#ifdef	MATCH_TAR_PATTERN
    if (*text == '/')
	return true;
#endif	/* MATCH_TAR_ATTERN */
    return *text == '\0';
}


/* User-level routine. returns whether or not 'text' and 'p' matched */
bool
tr_wildmat (const char * text, const char * p)
{
    if (p[0] == '*' && p[1] == '\0')
	return true;

    return DoMatch (text, p) == true;
}
