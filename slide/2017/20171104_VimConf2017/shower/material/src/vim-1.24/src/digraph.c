/* vi:ts=4:sw=4
 *
 * VIM - Vi IMitation
 *
 * Code Contributions By:	Bram Moolenaar			mool@oce.nl
 *							Tim Thompson			twitch!tjt
 *							Tony Andrews			onecom!wldrdg!tony 
 *							G. R. (Fred) Walter		watmath!watcgl!grwalter 
 */

#ifdef DIGRAPHS
/*
 * digraph.c: code for digraphs
 */

#include "vim.h"
#include "globals.h"
#include "proto.h"

static void printdigraph __ARGS((u_char *));

u_char	(*digraphnew)[3];			/* pointer to added digraphs */
int		digraphcount = 0;			/* number of added digraphs */

u_char	digraphdefault[][3] = 		/* standard digraphs */
	   {'~', '!', 161,	/* � */
	    'c', '|', 162,	/* � */
	    '$', '$', 163,	/* � */
	    'o', 'x', 164,	/* � */
	    'Y', '-', 165,	/* � */
	    '|', '|', 166,	/* � */
	    'p', 'a', 167,	/* � */
	    '"', '"', 168,	/* � */
	    'c', 'O', 169,	/* � */
		'a', '-', 170,	/* � */
		'<', '<', 171,	/* � */
		'a', '-', 172,	/* � */
		'-', '-', 173,	/* � */
		'r', 'O', 174,	/* � */
		'-', '=', 175,	/* � */
		'~', 'o', 176,	/* � */
		'+', '-', 177,	/* � */
		'2', '2', 178,	/* � */
		'3', '3', 179,	/* � */
		'\'', '\'', 180,	/* � */
		'j', 'u', 181,	/* � */
		'p', 'p', 182,	/* � */
		'~', '.', 183,	/* � */
		',', ',', 184,	/* � */
		'1', '1', 185,	/* � */
		'o', '-', 186,	/* � */
		'>', '>', 187,	/* � */
		'1', '4', 188,	/* � */
		'1', '2', 189,	/* � */
		'3', '4', 190,	/* � */
		'~', '?', 191,	/* � */
		'A', '`', 192,	/* � */
		'A', '\'', 193,	/* � */
		'A', '^', 194,	/* � */
		'A', '~', 195,	/* � */
		'A', '"', 196,	/* � */
		'A', '@', 197,	/* � */
		'A', 'E', 198,	/* � */
		'C', ',', 199,	/* � */
		'E', '`', 200,	/* � */
		'E', '\'', 201,	/* � */
		'E', '^', 202,	/* � */
		'E', '"', 203,	/* � */
		'I', '`', 204,	/* � */
		'I', '\'', 205,	/* � */
		'I', '^', 206,	/* � */
		'I', '"', 207,	/* � */
		'-', 'D', 208,	/* � */
		'N', '~', 209,	/* � */
		'O', '`', 210,	/* � */
		'O', '\'', 211,	/* � */
		'O', '^', 212,	/* � */
		'O', '~', 213,	/* � */
		'O', '"', 214,	/* � */
		'/', '\\', 215,	/* � */
		'O', '/', 216,	/* � */
		'U', '`', 217,	/* � */
		'U', '\'', 218,	/* � */
		'U', '^', 219,	/* � */
		'U', '"', 220,	/* � */
		'Y', '\'', 221,	/* � */
		'I', 'p', 222,	/* � */
		's', 's', 223,	/* � */
		'a', '`', 224,	/* � */
		'a', '\'', 225,	/* � */
		'a', '^', 226,	/* � */
		'a', '~', 227,	/* � */
		'a', '"', 228,	/* � */
		'a', '@', 229,	/* � */
		'a', 'e', 230,	/* � */
		'c', ',', 231,	/* � */
		'e', '`', 232,	/* � */
		'e', '\'', 233,	/* � */
		'e', '^', 234,	/* � */
		'e', '"', 235,	/* � */
		'i', '`', 236,	/* � */
		'i', '\'', 237,	/* � */
		'i', '^', 238,	/* � */
		'i', '"', 239,	/* � */
		'-', 'd', 240,	/* � */
		'n', '~', 241,	/* � */
		'o', '`', 242,	/* � */
		'o', '\'', 243,	/* � */
		'o', '^', 244,	/* � */
		'o', '~', 245,	/* � */
		'o', '"', 246,	/* � */
		':', '-', 247,	/* � */
		'o', '/', 248,	/* � */
		'u', '`', 249,	/* � */
		'u', '\'', 250,	/* � */
		'u', '^', 251,	/* � */
		'u', '"', 252,	/* � */
		'y', '\'', 253,	/* � */
		'i', 'p', 254,	/* � */
		'y', '"', 255,	/* � */
		NUL, NUL, NUL
		};

/*
 * lookup the pair char1, char2 in the digraph tables
 * if no match, return char2
 */
	int
getdigraph(char1, char2)
	int	char1;
	int	char2;
{
	int		i;
	int		retval;

	retval = 0;
	for (i = 0; ; ++i)			/* search added digraphs first */
	{
		if (i == digraphcount)	/* end of added table, search defaults */
		{
			for (i = 0; digraphdefault[i][0] != 0; ++i)
				if (digraphdefault[i][0] == char1 && digraphdefault[i][1] == char2)
				{
					retval = digraphdefault[i][2];
					break;
				}
			break;
		}
		if (digraphnew[i][0] == char1 && digraphnew[i][1] == char2)
		{
			retval = digraphnew[i][2];
			break;
		}
	}

	if (retval == 0)	/* digraph deleted or not found */
		return char2;
	return retval;
}

/*
 * put the digraphs in the argument string in the digraph table
 * format: {c1}{c2} char {c1}{c2} char ...
 */
	void
putdigraph(str)
	char *str;
{
	int		char1, char2, n;
	u_char	(*newtab)[3];
	int		i;

	while (*str)
	{
		skipspace(&str);
		char1 = *str++;
		char2 = *str++;
		if (char1 == 0 || char2 == 0)
			return;
		skipspace(&str);
		if (!isdigit(*str))
		{
			emsg(e_number);
			return;
		}
		n = getdigits(&str);
		if (digraphnew)		/* search the table for existing entry */
		{
			for (i = 0; i < digraphcount; ++i)
				if (digraphnew[i][0] == char1 && digraphnew[i][1] == char2)
				{
					digraphnew[i][2] = n;
					break;
				}
			if (i < digraphcount)
				continue;
		}
		newtab = (u_char (*)[3])alloc(digraphcount * 3 + 3);
		if (newtab)
		{
			memmove(newtab, digraphnew, (size_t)(digraphcount * 3));
			free(digraphnew);
			digraphnew = newtab;
			digraphnew[digraphcount][0] = char1;
			digraphnew[digraphcount][1] = char2;
			digraphnew[digraphcount][2] = n;
			++digraphcount;
		}
	}
}

	void
listdigraphs()
{
	int		i;

	printdigraph(NULL);
	for (i = 0; digraphdefault[i][0]; ++i)
		if (getdigraph(digraphdefault[i][0], digraphdefault[i][1]) == digraphdefault[i][2])
			printdigraph(digraphdefault[i]);
	for (i = 0; i < digraphcount; ++i)
		printdigraph(digraphnew[i]);
	outchar('\n');
	wait_return(TRUE);
}

	static void
printdigraph(p)
	u_char *p;
{
	char		buf[9];
	static int	len;

	if (p == NULL)
		len = 0;
	else if (p[2] != 0)
	{
		if (len > Columns - 11)
		{
			outchar('\n');
			len = 0;
		}
		if (len)
			outstrn("   ");
		sprintf(buf, "%c%c %c %3d", p[0], p[1], p[2], p[2]);
		outstrn(buf);
		len += 11;
	}
}

#endif /* DIGRAPHS */
