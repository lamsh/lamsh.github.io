/* vi:ts=4:sw=4
 *
 * VIM - Vi IMproved
 *
 * Code Contributions By:	Bram Moolenaar			mool@oce.nl
 *							Tim Thompson			twitch!tjt
 *							Tony Andrews			onecom!wldrdg!tony 
 *							G. R. (Fred) Walter		watmath!watcgl!grwalter 
 */

/*
 * cmdline.c: functions for reading in the command line and executing it
 */

#include "vim.h"
#include "globals.h"
#include "proto.h"
#include "param.h"
#include "cmdtab.h"
#include "ops.h"			/* included because we call functions in ops.c */
#include "fcntl.h"			/* for chdir() */

#ifdef LATTICE
# define mktemp(a)	tmpnam(a)
#endif

/*
 * the history list of alternate files
 */
#define NUMALTFILES 20

static char    *altfiles[NUMALTFILES];	/* alternate files */
static char    *saltfiles[NUMALTFILES];	/* alternate files without path */
static linenr_t altlnum[NUMALTFILES];	/* line # in alternate file */
static linenr_t doecmdlnum = 0;			/* line # in new file for doecmd() */

/*
 * variables shared between getcmdline() and redrawcmdline()
 */
static int		 cmdlen;		/* number of chars on command line */
static int		 cmdpos;		/* current cursor position */
static int		 cmdslen;		/* lenght of command line on screen */
static int		 cmdspos;		/* cursor position on screen */
static int		 cmdfirstc; 	/* ':', '/' or '?' */
static u_char	*cmdbuff;		/* pointer to command line buffer */

/*
 * The next two variables contain the bounds of any range given in a command.
 * They are set by docmdline().
 */
static linenr_t 	line1, line2;

static int			forceit;
static int			regname;
static int			quitmore = 0;
static int  		cmd_numfiles = -1;	  /* number of files found by
													filename completion */

static void		putcmdline __ARGS((int, u_char *));
static void		cmdchecklen __ARGS((void));
static void		cursorcmd __ARGS((void));
static int		ccheck_abbr __ARGS((int));
static u_char	*DoOneCmd __ARGS((u_char *));
static void		dobang __ARGS((int, u_char *));
static int		autowrite __ARGS((void));
static int		dowrite __ARGS((u_char *, int));
static int		doecmd __ARGS((char *, char *));
static void		doshell __ARGS((char *));
static void		dofilter __ARGS((u_char *, int, int));
static void		domake __ARGS((char *));
static int		doarglist __ARGS((char *));
static int		check_readonly __ARGS((void));
static int		check_changed __ARGS((int));
static int		check_more __ARGS((int));
static void		setaltfname __ARGS((char *, char *, linenr_t, int));
static void		nextwild __ARGS((u_char *, int));
static void		showmatches __ARGS((char *, int));
static char		*addstar __ARGS((char *, int));
static linenr_t get_address __ARGS((u_char **));
static void		do_align __ARGS((linenr_t, linenr_t, int, int));

extern char		*mktemp __ARGS((char *));

extern int global_busy, global_wait;	/* shared with csearch.c, message.c */

/*
 * getcmdline() - accept a command line starting with ':', '!', '/', or '?'
 *
 * For searches the optional matching '?' or '/' is removed.
 */

	int
getcmdline(firstc, buff)
	int			firstc; 	/* either ':', '/', or '?' */
	u_char		*buff;	 	/* buffer for command string */
{
	register u_char 	c;
			 int		nextc = 0;
	register int		i;
			 int		retval;
			 int		hiscnt;				/* current history line in use */
	static	 char 		**history = NULL;	/* history table */
	static	 int		hislen = 0; 		/* actual lengt of history table */
			 int		newlen;				/* new length of history table */
	static	 int		hisidx = -1;		/* last entered entry */
			 char		**temp;
			 char		*lookfor = NULL;	/* string to match */
			 int		j = -1;
			 int		gotesc = FALSE;		/* TRUE when last char typed was <ESC> */

/*
 * set some variables for redrawcmd()
 */
	cmdfirstc = firstc;
	cmdbuff = buff;
	cmdlen = cmdpos = 0;
	cmdslen = cmdspos = 1;
	State = CMDLINE;
	gotocmdline(TRUE, firstc);

/*
 * if size of history table changed, reallocate it
 */
	newlen = (int)p_hi;
	if (newlen != hislen)						/* history length changed */
	{
		if (newlen)
			temp = (char **)lalloc((u_long)(newlen * sizeof(char *)), TRUE);
		else
			temp = NULL;
		if (newlen == 0 || temp != NULL)
		{
			if (newlen > hislen)			/* array becomes bigger */
			{
				for (i = 0; i <= hisidx; ++i)
					temp[i] = history[i];
				j = i;
				for ( ; i <= newlen - (hislen - hisidx); ++i)
					temp[i] = NULL;
				for ( ; j < hislen; ++i, ++j)
					temp[i] = history[j];
			}
			else							/* array becomes smaller */
			{
				j = hisidx;
				for (i = newlen - 1; ; --i)
				{
					if (i >= 0)
						temp[i] = history[j];	/* copy newest entries */
					else
						free(history[j]);		/* remove older entries */
					if (--j < 0)
						j = hislen - 1;
					if (j == hisidx)
						break;
				}
				hisidx = newlen - 1;
			}
			free(history);
			history = temp;
			hislen = newlen;
		}
	}
	hiscnt = hislen;			/* set hiscnt to impossible history value */

#ifdef DIGRAPHS
	dodigraph(-1);				/* init digraph typahead */
#endif

	/* collect the command string, handling '\b', @ and much more */
	for (;;)
	{
		cursorcmd();	/* set the cursor on the right spot */
		if (nextc)		/* character remaining from CTRL-V */
		{
			c = nextc;
			nextc = 0;
		}
		else
		{
			c = vgetc();
			if (c == Ctrl('C') && got_int)
				got_int = FALSE;
		}

		if (lookfor && c != K_SDARROW && c != K_SUARROW)
		{
			free(lookfor);
			lookfor = NULL;
		}

		if (cmd_numfiles > 0 && !(c == p_wc && KeyTyped) && c != Ctrl('N') &&
						c != Ctrl('P') && c != Ctrl('A') && c != Ctrl('L'))
			(void)ExpandOne(NULL, FALSE, -2);	/* may free expanded file names */

#ifdef DIGRAPHS
		c = dodigraph(c);
#endif

		if (c == '\n' || c == '\r' || (c == ESC && !KeyTyped))
		{
			if (ccheck_abbr(c + 0x100))
				continue;
			outchar('\r');
			flushbuf();
			break;
		}

			/* hitting <ESC> twice means: abandon command line */
			/* wildcard expansion is only done when the key is really typed, not
			   when it comes from a macro */
		if (c == p_wc && !gotesc && KeyTyped)
		{
			if (cmd_numfiles > 0)	/* typed p_wc twice */
				nextwild(buff, 3);
			else					/* typed p_wc first time */
				nextwild(buff, 0);
			if (c == ESC)
				gotesc = TRUE;
			continue;
		}
		gotesc = FALSE;

		if (c == K_ZERO)		/* NUL is stored as NL */
			c = '\n';

		switch (c)
		{
		case BS:
		case DEL:
		case Ctrl('W'):
				/*
				 * delete current character is the same as backspace on next
				 * character, except at end of line
				 */
				if (c == DEL && cmdpos != cmdlen)
					++cmdpos;
				if (cmdpos > 0)
				{
					j = cmdpos;
					if (c == Ctrl('W'))
					{
						while (cmdpos && isspace(buff[cmdpos - 1]))
							--cmdpos;
						i = isidchar(buff[cmdpos - 1]);
						while (cmdpos && !isspace(buff[cmdpos - 1]) && isidchar(buff[cmdpos - 1]) == i)
							--cmdpos;
					}
					else
						--cmdpos;
					cmdlen -= j - cmdpos;
					i = cmdpos;
					while (i < cmdlen)
						buff[i++] = buff[j++];
					redrawcmd();
				}
				else if (cmdlen == 0 && c != Ctrl('W'))
				{
					retval = FALSE;
					msg("");
					goto returncmd; 	/* back to cmd mode */
				}
				continue;

/*		case '@':	only in very old vi */
		case Ctrl('U'):
clearline:
				cmdpos = 0;
				cmdlen = 0;
				cmdslen = 1;
				cmdspos = 1;
				redrawcmd();
				continue;

		case ESC:			/* get here if p_wc != ESC or when ESC typed twice */
		case Ctrl('C'):
				retval = FALSE;
				msg("");
				goto returncmd; 	/* back to cmd mode */

		case Ctrl('D'):
			{
				for (i = cmdpos; i > 0 && buff[i - 1] != ' '; --i)
						;
				showmatches((char *)&buff[i], cmdpos - i);
				for (i = Rows_max - Rows; i; --i)
					outchar('\n');

				redrawcmd();
				continue;
			}

		case K_RARROW:
		case K_SRARROW:
				do
				{
						if (cmdpos >= cmdlen)
								break;
						cmdspos += charsize(buff[cmdpos]);
						++cmdpos;
				}
				while (c == K_SRARROW && buff[cmdpos] != ' ');
				continue;

		case K_LARROW:
		case K_SLARROW:
				do
				{
						if (cmdpos <= 0)
								break;
						--cmdpos;
						cmdspos -= charsize(buff[cmdpos]);
				}
				while (c == K_SLARROW && buff[cmdpos - 1] != ' ');
				continue;

		case Ctrl('B'):		/* begin of command line */
				cmdpos = 0;
				cmdspos = 1;
				continue;

		case Ctrl('E'):		/* end of command line */
				cmdpos = cmdlen;
				buff[cmdlen] = NUL;
				cmdspos = strsize((char *)buff) + 1;
				continue;

		case Ctrl('A'):		/* all matches */
				nextwild(buff, 4);
				continue;

		case Ctrl('L'):		/* longest common part */
				nextwild(buff, 5);
				continue;

		case Ctrl('N'):		/* next match */
		case Ctrl('P'):		/* previous match */
				if (cmd_numfiles > 0)
				{
					nextwild(buff, (c == Ctrl('P')) ? 2 : 1);
					continue;
				}

		case K_UARROW:
		case K_DARROW:
		case K_SUARROW:
		case K_SDARROW:
				if (hislen == 0)		/* no history */
					continue;

				i = hiscnt;
			
					/* save current command string */
				if (c == K_SUARROW || c == K_SDARROW)
				{
					buff[cmdpos] = NUL;
					if (lookfor == NULL && (lookfor = strsave((char *)buff)) == NULL)
						continue;

					j = strlen(lookfor);
				}
				for (;;)
				{
						/* one step backwards */
					if (c == K_UARROW || c == K_SUARROW || c == Ctrl('P'))
					{
						if (hiscnt == hislen)	/* first time */
							hiscnt = hisidx;
						else if (hiscnt == 0 && hisidx != hislen - 1)
							hiscnt = hislen - 1;
						else if (hiscnt != hisidx + 1)
							--hiscnt;
						else					/* at top of list */
							break;
					}
					else	/* one step forwards */
					{
						if (hiscnt == hisidx)	/* on last entry, clear the line */
						{
							hiscnt = hislen;
							goto clearline;
						}
						if (hiscnt == hislen)	/* not on a history line, nothing to do */
							break;
						if (hiscnt == hislen - 1)	/* wrap around */
							hiscnt = 0;
						else
							++hiscnt;
					}
					if (hiscnt < 0 || history[hiscnt] == NULL)
					{
						hiscnt = i;
						break;
					}
					if ((c != K_SUARROW && c != K_SDARROW) || hiscnt == i ||
							strncmp(history[hiscnt], lookfor, (size_t)j) == 0)
						break;
				}

				if (hiscnt != i)		/* jumped to other entry */
				{
					strcpy((char *)buff, history[hiscnt]);
					cmdpos = cmdlen = strlen((char *)buff);
					redrawcmd();
				}
				continue;

		case Ctrl('V'):
				putcmdline('^', buff);
				c = get_literal(&nextc);	/* get next (two) character(s) */
				break;

#ifdef DIGRAPHS
		case Ctrl('K'):
				putcmdline('?', buff);
			  	c = vgetc();
				putcmdline(c, buff);
				c = getdigraph(c, vgetc());
				break;
#endif /* DIGRAPHS */
		}

		/* we come here if we have a normal character */

		if (!isidchar(c) && ccheck_abbr(c))
			continue;

		if (cmdlen < CMDBUFFSIZE - 2)
		{
				for (i = cmdlen++; i > cmdpos; --i)
						buff[i] = buff[i - 1];
				buff[cmdpos] = c;
				outtrans((char *)(buff + cmdpos), cmdlen - cmdpos);
				++cmdpos;
				i = charsize(c);
				cmdslen += i;
				cmdspos += i;
		}
		cmdchecklen();
	}
	retval = TRUE;				/* when we get here we have a valid command line */

returncmd:
	buff[cmdlen] = NUL;
	if (hislen != 0 && cmdlen != 0)		/* put line in history buffer */
	{
		if (++hisidx == hislen)
			hisidx = 0;
		free(history[hisidx]);
		history[hisidx] = strsave((char *)buff);
	}

	/*
	 * If the screen was shifted up, redraw the whole screen (later).
	 * If the line is too long, clear it, so ruler and shown command do
	 * not get printed in the middle of it.
	 */
	if (cmdoffset)
		must_redraw = CLEAR;
	else if (cmdslen >= sc_col)
		gotocmdline(TRUE, NUL);
	State = NORMAL;
	script_winsize_pp();
	return retval;
}

/*
 * put a character on the command line.
 * Used for CTRL-V and CTRL-K
 */
	static void
putcmdline(c, buff)
	int		c;
	u_char	*buff;
{
	int		len;
	char	buf[2];

	buf[0] = c;
	buf[1] = 0;
	len = outtrans(buf, 1);
	outtrans((char *)(buff + cmdpos), cmdlen - cmdpos);
	cmdslen += len;
	cmdchecklen();
	cmdslen -= len;
	cursorcmd();
}

/*
 * Check if the command line spans more than one screen line.
 * The maximum number of lines is remembered.
 */
	static void
cmdchecklen()
{
	if (cmdslen / (int)Columns > cmdoffset)
		cmdoffset = cmdslen / (int)Columns;
}

/*
 * this fuction is called when the screen size changes
 */
	void
redrawcmdline()
{
		cmdoffset = 0;
		redrawcmd();
		cursorcmd();
}

/*
 * Redraw what is currently on the command line.
 */
	void
redrawcmd()
{
	register int i;

	windgoto((int)Rows - 1 - cmdoffset, 0);
	outchar(cmdfirstc);
	cmdslen = 1;
	cmdspos = 1;
	outtrans((char *)cmdbuff, cmdlen);
	for (i = 0; i < cmdlen; )
	{
		cmdslen += charsize(cmdbuff[i]);
		if (++i == cmdpos)
				cmdspos = cmdslen;
	}
	for (i = (cmdoffset + 1) * (int)Columns - cmdslen; --i > 0; )
		outchar(' ');
	cmdchecklen();
}

	static void
cursorcmd()
{
	windgoto((int)Rows - 1 - cmdoffset + (cmdspos / (int)Columns), cmdspos % (int)Columns);
}

/*
 * Check the word in front of the cursor for an abbreviation.
 * Called when the non-id character "c" has been entered.
 * When an abbreviation is recognized it is removed from the text with
 * backspaces and the replacement string is inserted, followed by "c".
 */
	static int
ccheck_abbr(c)
	int c;
{
	if (p_paste || no_abbr)			/* no abbreviations or in paste mode */
		return FALSE;
	
	return check_abbr(c, (char *)cmdbuff, cmdpos, 0);
}

/*
 * docmdline(): execute an Ex command line
 *
 * 1. If no line given, get one.
 * 2. Split up in parts separated with '|'.
 *
 * This function may be called recursively!
 */
	void
docmdline(cmdline)
	u_char		*cmdline;
{
	u_char		buff[CMDBUFFSIZE];		/* command line */
	u_char		*nextcomm;

/*
 * 1. If no line given: get one.
 */
	if (cmdline == NULL)
	{
		if (!getcmdline(':', buff))
			return;
	}
	else
	{
		if (strlen((char *)cmdline) > (size_t)(CMDBUFFSIZE - 2))
		{
			emsg(e_toolong);
			return;
		}
		/* Make a copy of the command so we can mess with it. */
		strcpy((char *)buff, (char *)cmdline);
	}

/*
 * 2. Loop for each '|' separated command.
 *    DoOneCmd will set nextcommand to NULL if there is no trailing '|'.
 */
	for (;;)
	{
		nextcomm = DoOneCmd(buff);
		if (nextcomm == NULL)
			break;
		strcpy((char *)buff, (char *)nextcomm);
	}
}

/*
 * Execute one Ex command.
 *
 * 2. skip comment lines and leading space
 * 3. parse range
 * 4. parse command
 * 5. parse arguments
 * 6. switch on command name
 *
 * This function may be called recursively!
 */
	static u_char *
DoOneCmd(buff)
	u_char *buff;
{
	u_char				cmdbuf[CMDBUFFSIZE];	/* for '%' and '#' expansion */
	u_char				c;
	register u_char		*p;
	char				*q;
	u_char				*cmd, *arg;
	int 				i;
	int					cmdidx;
	int					argt;
	register linenr_t	lnum;
	long				n;
	int					addr_count;	/* number of address specifications */
	FPOS				pos;
	int					append = FALSE;			/* write with append */
	int					usefilter = FALSE;		/* filter instead of file name */
	u_char				*nextcomm;

	if (quitmore)
		--quitmore;		/* when not editing the last file :q has to be typed twice */
/*
 * 2. skip comment lines and leading space, colons or bars
 */
	for (cmd = buff; *cmd && strchr(" \t:|", *cmd) != NULL; cmd++)
		;

	nextcomm = NULL;		/* default: no next command */
	if (strchr("#\"", *cmd) != NULL)	/* ignore comment and empty lines */
		goto doend;

/*
 * 3. parse a range specifier of the form: addr [,addr] [;addr] ..
 *
 * where 'addr' is:
 *
 * %		  (entire file)
 * $  [+-NUM]
 * 'x [+-NUM] (where x denotes a currently defined mark)
 * .  [+-NUM]
 * [+-NUM]..
 * NUM
 *
 * The cmd pointer is updated to point to the first character following the
 * range spec. If an initial address is found, but no second, the upper bound
 * is equal to the lower.
 */

	addr_count = 0;
	--cmd;
	do
	{
		++cmd;							/* skip ',' or ';' */
		line1 = line2;
		line2 = Curpos.lnum;			/* default is current line number */
		skipspace((char **)&cmd);
		lnum = get_address(&cmd);
		if (lnum == INVLNUM)
		{
			if (*cmd == '%')            /* '%' - all lines */
			{
				++cmd;
				line1 = 1;
				line2 = line_count;
				++addr_count;
			}
		}
		else
			line2 = lnum;
		addr_count++;

		if (*cmd == ';')
		{
			if (line2 == 0)
				Curpos.lnum = 1;
			else
				Curpos.lnum = line2;
		}
	} while (*cmd == ',' || *cmd == ';');

	/* One address given: set start and end lines */
	if (addr_count == 1)
	{
		line1 = line2;
			/* ... but only implicit: really no address given */
		if (lnum == INVLNUM)
			addr_count = 0;
	}

	if (line1 > line2 || line2 > line_count)
	{
		emsg(e_invrange);
		goto doend;
	}

/*
 * 4. parse command
 */

	skipspace((char **)&cmd);

	/*
	 * If we got a line, but no command, then go to the line.
	 */
	if (*cmd == NUL || *cmd == '"' || (*cmd == '|' && (nextcomm = cmd) != NULL))
	{
		if (addr_count != 0)
		{
			if (line2 == 0)
				Curpos.lnum = 1;
			else
				Curpos.lnum = line2;
			Curpos.col = 0;
			cursupdate();
		}
		goto doend;
	}

	/*
	 * isolate the command and search for it in the command table
	 */
	p = cmd;
	if (*cmd != 'k')
		while (isalpha(*p))
			++p;
	if (p == cmd && strchr("@!=><&k", *p) != NULL)	/* non-alpha or 'k' command */
		++p;
	i = (int)(p - cmd);

	for (cmdidx = 0; cmdidx < CMD_SIZE; ++cmdidx)
		if (strncmp(cmdnames[cmdidx].cmd_name, (char *)cmd, (size_t)i) == 0)
			break;

	if (i == 0 || cmdidx == CMD_SIZE)
	{
		emsg(e_invcmd);
		goto doend;
	}

	if (*p == '!')					/* forced commands */
	{
		++p;
		forceit = TRUE;
	}
	else
		forceit = FALSE;

/*
 * 5. parse arguments
 */
	argt = cmdnames[cmdidx].cmd_argt;

	if (!(argt & RANGE) && addr_count)
	{
		emsg(e_norange);
		goto doend;
	}

	if (!(argt & ZEROR))			/* zero in range not allowed */
	{
		if (line1 == 0)
			line1 = 1;
		if (line2 == 0)
			line2 = 1;
	}

	/*
	 * for the :make command we insert the 'makeprg' option here,
	 * so things like % get expanded
	 */
	if (cmdidx == CMD_make)
	{
		if (strlen(p_mp) + strlen((char *)p) + 2 >= (unsigned)CMDBUFFSIZE)
		{
			emsg(e_toolong);
			goto doend;
		}
		strcpy((char *)cmdbuf, p_mp);
		strcat((char *)cmdbuf, " ");
		strcat((char *)cmdbuf, (char *)p);
		strcpy((char *)buff, (char *)cmdbuf);
		p = buff;
	}

	arg = p;						/* remember start of argument */
	skipspace((char **)&arg);

	if ((argt & NEEDARG) && *arg == NUL)
	{
		emsg(e_argreq);
		goto doend;
	}

	/*
	 * check for '|' to separate commands and '"' to start comments
	 */
	if (argt & TRLBAR)
	{
		while (*p)
		{
			if (*p == Ctrl('V'))
			{
				if (argt & USECTRLV)	/* skip the CTRL-V and next char */
					++p;
				else					/* remove CTRL-V and skip next char */
					strcpy((char *)p, (char *)p + 1);
			}
			else if ((*p == '"' && !(argt & NOTRLCOM)) || *p == '|')
			{
				if (*(p - 1) == '\\')	/* remove the backslash */
				{
					strcpy((char *)p - 1, (char *)p);
					--p;
				}
				else
				{
					if (*p == '|')
						nextcomm = p + 1;
					*p = NUL;
					break;
				}
			}
			++p;
		}
		if (!(argt & NOTRLCOM))			/* remove trailing spaces */
		{
			q = (char *)arg + strlen((char *)arg);
			while (--q > (char *)arg && isspace(q[0]) && q[-1] != '\\' && q[-1] != Ctrl('V'))
				*q = NUL;
		}
	}

	if ((argt & DFLALL) && addr_count == 0)
	{
		line1 = 1;
		line2 = line_count;
	}

	regname = 0;
		/* accept numbered register only when no count allowed (:put) */
	if ((argt & REGSTR) && (isalpha(*arg) || *arg == '.' || *arg == '"' || (!(argt & COUNT) && isdigit(*arg))))
	{
		regname = *arg;
		++arg;
		skipspace((char **)&arg);
	}

	if ((argt & COUNT) && isdigit(*arg))
	{
		n = getdigits((char **)&arg);
		skipspace((char **)&arg);
		if (n <= 0)
		{
			emsg(e_zerocount);
			goto doend;
		}
		line1 = line2;
		line2 += n - 1;
	}

	if (!(argt & EXTRA) && strchr("|\"#", *arg) == NULL)	/* no arguments allowed */
	{
		emsg(e_trailing);
		goto doend;
	}

	if (cmdidx == CMD_write)
	{
		if (*arg == '>')						/* append */
		{
			if (*++arg != '>')				/* typed wrong */
			{
				emsg("Use w or w>>");
				goto doend;
			}
			++arg;
			skipspace((char **)&arg);
			append = TRUE;
		}
		else if (*arg == '!')					/* :w !filter */
		{
			++arg;
			usefilter = TRUE;
		}
	}

	if (cmdidx == CMD_read)
	{
		usefilter = forceit;					/* :r! filter if forceit */
		if (*arg == '!')						/* :r !filter */
		{
			++arg;
			usefilter = TRUE;
		}
	}

	/*
	 * change '%' to Filename, '#' to altfile
	 */
	if (argt & XFILE)
	{
		for (p = arg; *p; ++p)
		{
			c = *p;
			if (c != '%' && c != '#')	/* nothing to expand */
				continue;
			if (*(p - 1) == '\\')		/* remove escaped char */
			{
				strcpy((char *)p - 1, (char *)p);
				--p;
				continue;
			}

			n = 1;				/* length of what we expand */
			if (c == '#' && *(p + 1) == '<')
			{					/* "#<": current file name without extension */
				n = 2;
				c = '<';
			}
			if (c == '%' || c == '<')
			{
				if (check_fname())
					goto doend;
				q = xFilename;
			}
			else
			{
				q = (char *)p + 1;
				i = (int)getdigits(&q);
				n = q - (char *)p;

				if (i >= NUMALTFILES || altfiles[i] == NULL)
				{
						emsg(e_noalt);
						goto doend;
				}
				doecmdlnum = altlnum[i];
				if (did_cd)
					q = altfiles[i];
				else
					q = saltfiles[i];
			}
			i = strlen((char *)arg) + strlen(q) + 3;
			if (nextcomm)
				i += strlen((char *)nextcomm);
			if (i > CMDBUFFSIZE)
			{
				emsg(e_toolong);
				goto doend;
			}
			/*
			 * we built the new argument in cmdbuf[], then copy it back to buff[]
			 */
			*p = NUL;							/* truncate at the '#' or '%' */
			strcpy((char *)cmdbuf, (char *)arg);/* copy up to there */
			i = p - arg;						/* remember the lenght */
			strcat((char *)cmdbuf, q);			/* append the file name */
			if (c == '<' && (arg = (u_char *)strrchr(q, '.')) != NULL &&
							arg >= (u_char *)gettail(q))	/* remove extension */
				*((char *)cmdbuf + ((char *)arg - q) + i) = NUL;
			i = strlen((char *)cmdbuf);			/* remember the end of the filename */
			strcat((char *)cmdbuf, (char *)p+n);/* append what is after '#' or '%' */
			p = buff + i - 1;					/* remember where to continue */
			if (nextcomm)						/* append next command */
			{
				i = strlen((char *)cmdbuf) + 1;
				strcpy((char *)cmdbuf + i, (char *)nextcomm);
				nextcomm = buff + i;
			}
			strcpy((char *)buff, (char *)cmdbuf);/* copy back to buff[] */
			arg = buff;
		}

		/*
		 * One file argument: expand wildcards.
		 * Don't do this with ":r !command" or ":w !command".
		 */
		if (argt & NOSPC)
		{
			if (has_wildcard((char *)arg) && !usefilter)
			{
				if ((p = (u_char *)ExpandOne(arg, TRUE, -1)) == NULL)
					goto doend;
				if (strlen((char *)p) + arg - buff < CMDBUFFSIZE - 2)
					strcpy((char *)arg, (char *)p);
				else
					emsg(e_toolong);
				free(p);
			}
		}
	}

/*
 * 6. switch on command name
 */
	switch (cmdidx)
	{
		case CMD_quit:
				if (!check_more(FALSE))		/* if more files we won't exit */
					exiting = TRUE;
				if (check_changed(FALSE) || check_more(TRUE))
				{
					exiting = FALSE;
					settmode(1);
					break;
				}
				getout(0);

		case CMD_stop:
		case CMD_suspend:
				if (!forceit && Changed)
					autowrite();
				gotocmdend();
				flushbuf();
				stoptermcap();
				mch_suspend();		/* call machine specific function */
				starttermcap();
				must_redraw = CLEAR;
				break;

		case CMD_xit:
		case CMD_wq:
				if (!check_more(FALSE))		/* if more files we won't exit */
					exiting = TRUE;
				if (((cmdidx == CMD_wq || Changed) &&
				     (check_readonly() || !dowrite(arg, FALSE))) ||
					check_more(TRUE))
				{
					exiting = FALSE;
					settmode(1);
					break;
				}
				getout(0);

		case CMD_args:
				if (numfiles == 0)			/* no file name list */
				{
					if (!check_fname())		/* check for no file name at all */
						smsg("[%s]", Filename);
					break;
				}
				gotocmdline(TRUE, NUL);
				for (i = 0; i < numfiles; ++i)
				{
					if (i == curfile)
						outchar('[');
					outstrn(files[i]);
					if (i == curfile)
						outchar(']');
					outchar(' ');
				}
				outchar('\n');
				wait_return(TRUE);
				break;

		case CMD_wnext:
				n = line2;
				line1 = 1;
				line2 = line_count;
				dowrite(arg, FALSE);
				line2 = n;
				arg = (u_char *)"";		/* no file list */
				/*FALLTHROUGH*/

		case CMD_next:
				if (check_changed(TRUE))
					break;
				if (*arg != NUL)		/* redefine file list */
				{
					if (doarglist((char *)arg))
						break;
					i = 0;
				}
				else
				{
					if (addr_count == 0)
						i = curfile + 1;
					else
						i = curfile + (int)line2;
				}

donextfile:		if (i < 0 || i >= numfiles)
				{
					emsg(e_nomore);
					break;
				}
				if (check_changed(TRUE))
					break;
				curfile = i;
				doecmd(files[curfile], NULL);
				break;

		case CMD_previous:
		case CMD_Next:
				if (addr_count == 0)
					i = curfile - 1;
				else
					i = curfile - (int)line2;
				goto donextfile;

		case CMD_rewind:
				i = 0;
				goto donextfile;

		case CMD_write:
				if (usefilter)		/* input lines to shell command */
					dofilter(arg, TRUE, FALSE);
				else
					dowrite(arg, append);
				break;

		case CMD_edit:
		case CMD_ex:
		case CMD_visual:
				doecmd((char *)arg, NULL);
				break;

		case CMD_file:
				if (*arg != NUL)
				{
					setfname((char *)arg, NULL);
					NotEdited = TRUE;
					maketitle();
				}
				fileinfo(did_cd);		/* print full filename if :cd used */
				break;

		case CMD_files:
#ifdef AMIGA
				settmode(0);			/* set cooked mode, so output can be halted */
#endif
				for (i = 0; i < NUMALTFILES; ++i)
				{
					if (altfiles[i])
					{
						sprintf(IObuff, "%2d \"%s\" line %ld\n", i, altfiles[i], (long)altlnum[i]);
						outstrn(IObuff);
					}
					flushbuf();
				}
#ifdef AMIGA
				settmode(1);
#endif
				wait_return(TRUE);
				break;

		case CMD_read:
				if (usefilter)
				{
					dofilter(arg, FALSE, TRUE);			/* :r!cmd */
					break;
				}
				if (!u_save(line2, (linenr_t)(line2 + 1)))
					break;
				if (readfile((char *)arg, NULL, line2, FALSE))
				{
					emsg(e_notopen);
					break;
				}
				updateScreen(NOT_VALID);
				break;

		case CMD_cd:
		case CMD_chdir:
#ifdef UNIX
				/*
				 * for UNIX ":cd" means: go to home directory
				 */
				if (*arg == NUL)	 /* use IObuff for home directory name */
				{
					expand_env("$HOME", IObuff, IOSIZE);
					arg = (u_char *)IObuff;
				}
#endif
				if (*arg != NUL)
				{
					if (!did_cd)
					{
						scriptfullpath();
						xFilename = Filename;
					}
					did_cd = TRUE;
					if (chdir((char *)arg))
						emsg(e_failed);
					break;
				}
				/*FALLTHROUGH*/

		case CMD_pwd:
				if (dirname(IObuff, IOSIZE))
					msg(IObuff);
				else
					emsg(e_unknown);
				break;

		case CMD_equal:
				smsg("line %ld", (long)line2);
				break;

		case CMD_list:
				i = p_list;
				p_list = 1;
		case CMD_number:
		case CMD_print:
#ifdef AMIGA
				settmode(0);			/* set cooked mode, so output can be halted */
#endif
				gotocmdline(TRUE, NUL);	/* clear command line */
				n = 0;
				for (;;)
				{
					if (p_nu || cmdidx == CMD_number)
					{
						sprintf(IObuff, "%7ld ", (long)line1);
						outstrn(IObuff);
						n += 8;
					}
					n += prt_line(nr2ptr(line1));
					if (++line1 > line2)
						break;
					outchar('\n');
					flushbuf();
					n = Columns;		/* call wait_return later */
				}
#ifdef AMIGA
				settmode(1);
#endif

				if (cmdidx == CMD_list)
					p_list = i;

					/*
					 * if we have one line that runs into the shown command,
					 * or more than one line, call wait_return()
					 */
				if (n >= sc_col || global_busy)
				{
					outchar('\n');
					wait_return(TRUE);
				}
				break;

		case CMD_shell:
				doshell(NULL);
				break;

		case CMD_tag:
				dotag((char *)arg, 0, addr_count ? (int)line2 : 1);
				break;

		case CMD_pop:
				dotag("", 1, addr_count ? (int)line2 : 1);
				break;

		case CMD_tags:
				dotags();
				break;

		case CMD_marks:
				domarks();
				break;

		case CMD_jumps:
				dojumps();
				break;

		case CMD_digraphs:
#ifdef DIGRAPHS
				if (*arg)
					putdigraph((char *)arg);
				else
					listdigraphs();
#else
				emsg("No digraphs in this version");
#endif /* DIGRAPHS */
				break;

		case CMD_set:
				doset((char *)arg);
				break;

		case CMD_abbreviate:
		case CMD_cabbrev:
		case CMD_iabbrev:
		case CMD_cnoreabbrev:
		case CMD_inoreabbrev:
		case CMD_noreabbrev:
		case CMD_unabbreviate:
		case CMD_cunabbrev:
		case CMD_iunabbrev:
				i = ABBREV;
				goto doabbr;		/* almost the same as mapping */

		case CMD_cmap:
		case CMD_imap:
		case CMD_map:
		case CMD_cnoremap:
		case CMD_inoremap:
		case CMD_noremap:
				/*
				 * If we are sourcing .exrc or .vimrc in current directory we
				 * print the mappings for security reasons.
				 */
				if (secure)
				{
					secure = 2;
					outtrans((char *)cmd, -1);
					outchar('\n');
				}
		case CMD_cunmap:
		case CMD_iunmap:
		case CMD_unmap:
				i = 0;
doabbr:
				if (*cmd == 'c')		/* cmap, cunmap, cnoremap, etc. */
				{
					i += CMDLINE;
					++cmd;
				}
				else if (*cmd == 'i')	/* imap, iunmap, inoremap, etc. */
				{
					i += INSERT;
					++cmd;
				}
				else if (forceit || i)	/* map!, unmap!, noremap!, abbrev */
					i += INSERT + CMDLINE;
				else
					i += NORMAL;			/* map, unmap, noremap */
				switch (domap((*cmd == 'n') ? 2 : (*cmd == 'u'), (char *)arg, i))
				{
					case 1: emsg(e_invarg);
							break;
					case 2: emsg(e_nomap);
							break;
					case 3: emsg(e_ambmap);
							break;
				}
				break;

		case CMD_display:
				outchar('\n');
				dodis();		/* display buffer contents */
				break;

		case CMD_help:
				help();
				break;

		case CMD_version:
				msg(longVersion);
				break;

		case CMD_winsize:
				line1 = getdigits((char **)&arg);
				skipspace((char **)&arg);
				line2 = getdigits((char **)&arg);
				set_winsize((int)line1, (int)line2, TRUE);
				break;

		case CMD_delete:
		case CMD_yank:
		case CMD_rshift:
		case CMD_lshift:
				yankbuffer = regname;
				startop.lnum = line1;
				endop.lnum = line2;
				nlines = line2 - line1 + 1;
				mtype = MLINE;
				Curpos.lnum = line1;
				switch (cmdidx)
				{
				case CMD_delete:
					dodelete();
					break;
				case CMD_yank:
					doyank(FALSE);
					break;
				case CMD_rshift:
					doshift(RSHIFT);
					break;
				case CMD_lshift:
					doshift(LSHIFT);
					break;
				}
				break;

		case CMD_put:
				yankbuffer = regname;
				Curpos.lnum = line2;
				doput(forceit ? BACKWARD : FORWARD, -1L);
				break;

		case CMD_t:
		case CMD_copy:
		case CMD_move:
				n = get_address(&arg);
				if (n == INVLNUM)
				{
					emsg(e_invaddr);
					break;
				}

				if (cmdidx == CMD_move)
				{
					if (n >= line1 && n < line2 && line2 > line1)
					{
						emsg("Move lines into themselves");
						break;
					}
					if (n >= line1)
					{
						--n;
						Curpos.lnum = n - (line2 - line1) + 1;
					}
					else
						Curpos.lnum = n + 1;
					while (line1 <= line2)
					{
							/* this undo is not efficient, but it works */
						u_save(line1 - 1, line1 + 1);
						q = delsline(line1, FALSE);
						u_save(n, n + 1);
						appendline(n, q);
						if (n < line1)
						{
							++n;
							++line1;
						}
						else
							--line2;
					}
				}
				else
				{
					/*
					 * there are three situations:
					 * 1. destination is above line1
					 * 2. destination is between line1 and line2
					 * 3. destination is below line2
					 *
					 * n = destination (when starting)
					 * Curpos.lnum = destination (while copying)
					 * line1 = start of source (while copying)
					 * line2 = end of source (while copying)
					 */
					u_save(n, n + 1);
					Curpos.lnum = n;
					lnum = line2 - line1 + 1;
					while (line1 <= line2)
					{
						appendline(Curpos.lnum, save_line(nr2ptr(line1)));
								/* situation 2: skip already copied lines */
						if (line1 == n)
							line1 = Curpos.lnum;
						++line1;
						if (Curpos.lnum < line1)
							++line1;
						if (Curpos.lnum < line2)
							++line2;
						++Curpos.lnum;
					}
					msgmore((long)lnum);
				}
				u_clearline();
				Curpos.col = 0;
				updateScreen(NOT_VALID);
				break;

		case CMD_and:
		case CMD_substitute:
				dosub(line1, line2, (char *)arg, &nextcomm);
				break;

		case CMD_join:
				Curpos.lnum = line1;
				if (line1 == line2)
				{
					if (line2 == line_count)
					{
						beep();
						break;
					}
					++line2;
				}
				dodojoin(line2 - line1 + 1, !forceit, TRUE);
				break;

		case CMD_global:
				if (forceit)
					*cmd = 'v';
		case CMD_vglobal:
				doglob(*cmd, line1, line2, (char *)arg);
				break;

		case CMD_at:				/* :[addr]@r */
				Curpos.lnum = line2;
				if (!doexecbuf(*arg))		/* put the register in mapbuf */
					beep();
				else
					docmdline(NULL);		/* execute from the mapbuf */
				break;

		case CMD_bang:
				dobang(addr_count, arg);
				break;

		case CMD_undo:
				u_undo(1);
				break;

		case CMD_redo:
				u_redo(1);
				break;

		case CMD_source:
				if (forceit)	/* :so! read vi commands */
					openscript((char *)arg);
				else if (dosource((char *)arg))		/* :so read ex commands */
					emsg(e_notopen);
				break;

		case CMD_mkvimrc:
				if (*arg == NUL)
					arg = (u_char *)VIMRC_FILE;
				/*FALLTHROUGH*/

		case CMD_mkexrc:
				{
					FILE	*fd;

					if (*arg == NUL)
						arg = (u_char *)EXRC_FILE;
#ifdef UNIX
						/* with Unix it is possible to open a directory */
					if (isdir((char *)arg) > 0)
					{
						emsg2("\"%s\" is a directory", (char *)arg);
						break;
					}
#endif
					if (!forceit && (fd = fopen((char *)arg, "r")) != NULL)
					{
						fclose(fd);
						emsg2("\"%s\" exists (use ! to override)", (char *)arg);
						break;
					}

					if ((fd = fopen((char *)arg, "w")) == NULL)
					{
						emsg2("Cannot open \"%s\" for writing", (char *)arg);
						break;
					}
					if (makemap(fd) || makeset(fd) || fclose(fd))
						emsg(e_write);
					break;
				}

		case CMD_cc:
					qf_jump(atoi((char *)arg));
					break;

		case CMD_cf:
					if (*arg != NUL)
					{
						/*
						 * Great trick: Insert 'ef=' before arg.
						 * Always ok, because "cf " must be there.
						 */
						arg -= 3;
						arg[0] = 'e';
						arg[1] = 'f';
						arg[2] = '=';
						doset((char *)arg);
					}
					qf_init();
					break;

		case CMD_cl:
					qf_list();
					break;

		case CMD_cn:
					qf_jump(-1);
					break;

		case CMD_cp:
					qf_jump(-2);
					break;

		case CMD_cq:
					getout(1);		/* this does not always work. why? */

		case CMD_mark:
		case CMD_k:
					pos = Curpos;			/* save Curpos */
					Curpos.lnum = line2;
					Curpos.col = 0;
					setmark(*arg);			/* set mark */
					Curpos = pos;			/* restore Curpos */
					break;

#ifdef SETKEYMAP
		case CMD_setkeymap:
					set_keymap(arg);
					break;
#endif

		case CMD_center:
		case CMD_right:
		case CMD_left:
					do_align(line1, line2, atoi((char *)arg),
							cmdidx == CMD_center ? 0 : cmdidx == CMD_right ? 1 : -1);
					break;

		case CMD_make:
					domake((char *)arg);
					break;

		default:
					emsg(e_invcmd);
	}


doend:
	forceit = FALSE;		/* reset now so it can be used in getfile() */
	return nextcomm;
}

/*
 * handle the :! command.
 * We replace the extra bangs by the previously entered command and remember
 * the command.
 */
	static void
dobang(addr_count, arg)
	int		addr_count;
	u_char	*arg;
{
	static	char	*prevcmd = NULL;		/* the previous command */
	char			*t;
	char			*trailarg;
	int 			len;

	/*
	 * Disallow shell commands from .exrc and .vimrc in current directory for
	 * security reasons.
	 */
	if (secure)
	{
		secure = 2;
		emsg(e_curdir);
		return;
	}
	len = strlen((char *)arg) + 1;

	if (Changed)
		autowrite();
	/*
	 * try to find an embedded bang, like in :!<cmd> ! [args]
	 * (:!! is indicated by the 'forceit' variable)
	 */
	trailarg = (char *)arg;
	skiptospace(&trailarg);
	skipspace(&trailarg);
	if (*trailarg == '!')
		*trailarg++ = NUL;
	else
		trailarg = NULL;

	if (forceit || trailarg != NULL)			/* use the previous command */
	{
		if (prevcmd == NULL)
		{
			emsg(e_noprev);
			return;
		}
		len += strlen(prevcmd) * (trailarg != NULL && forceit ? 2 : 1);
	}

	if (len > CMDBUFFSIZE)
	{
		emsg(e_toolong);
		return;
	}
	if ((t = alloc(len)) == NULL)
		return;
	*t = NUL;
	if (forceit)
		strcpy(t, prevcmd);
	strcat(t, (char *)arg);
	if (trailarg != NULL)
	{
		strcat(t, prevcmd);
		strcat(t, trailarg);
	}
	free(prevcmd);
	prevcmd = t;

	if (bangredo)			/* put cmd in redo buffer for ! command */
	{
		AppendToRedobuff(prevcmd);
		AppendToRedobuff("\n");
		bangredo = FALSE;
	}
		/* echo the command */
	gotocmdline(TRUE, ':');
	if (addr_count)						/* :range! */
	{
		outnum((long)line1);
		outchar(',');
		outnum((long)line2);
	}
	outchar('!');
	outtrans(prevcmd, -1);

	if (addr_count == 0)				/* :! */
		doshell(prevcmd); 
	else								/* :range! */
		dofilter((u_char *)prevcmd, TRUE, TRUE);
}

	static int
autowrite()
{
	if (!p_aw || check_readonly() || check_fname())
		return FALSE;
	return (writeit(Filename, sFilename, (linenr_t)1, line_count, 0, 0, TRUE));
}

	static int
dowrite(arg, append)
	u_char	*arg;
	int		append;
{
	FILE	*f;
	int		other;

	/*
	 * if we have a new file name put it in the list of alternate file names
	 */
	other = otherfile((char *)arg);
	if (*arg != NUL && other)
		setaltfname(strsave((char *)arg), strsave((char *)arg), (linenr_t)1, TRUE);

	/*
	 * writing to the current file is not allowed in readonly mode
	 */
	if ((*arg == NUL || !other) && check_readonly())
		return FALSE;

	/*
	 * write to current file
	 */
	if (*arg == NUL || !other)
	{
		if (check_fname())
			return FALSE;
		return (writeit(Filename, sFilename, line1, line2, append, forceit, TRUE));
	}

	/*
	 * write to other file; overwriting only allowed with '!'
	 */
	if (!forceit && !append && !p_wa && (f = fopen((char *)arg, "r")) != NULL)
	{								/* don't overwrite existing file */
			fclose(f);
#ifdef UNIX
				/* with UNIX it is possible to open a directory */
			if (isdir((char *)arg) > 0)
				emsg2("\"%s\" is a directory", (char *)arg);
			else
#endif
				emsg(e_exists);
			return 0;
	}
	return (writeit((char *)arg, NULL, line1, line2, append, forceit, TRUE));
}

	static int
doecmd(arg, sarg)
	char		*arg;
	char		*sarg;
{
	int			setalt;
	char		*command = NULL;
	int			redraw_save;
	linenr_t	newlnum;

	newlnum = doecmdlnum;
	doecmdlnum = 0;						/* reset it for next time */

	if (*arg == '+')		/* :e +[command] file */
	{
		++arg;
		if (isspace(*arg))
			command = "$";
		else
		{
			command = arg;
			while (*arg && !isspace(*arg))
				++arg;
		}
		if (*arg)
			*arg++ = NUL;
		
		skipspace(&arg);
	}

	if (sarg == NULL)
		sarg = arg;

#ifdef AMIGA
	fname_case(arg);				/* set correct case for filename */
#endif

	setalt = (*arg != NUL && otherfile(arg));
	if (check_changed(FALSE))
	{
		if (setalt)
			setaltfname(strsave(arg), strsave(sarg), (linenr_t)1, TRUE);
		return FALSE;
	}
	if (setalt)
	{
		setaltfname(Filename, sFilename, Curpos.lnum, FALSE);
		Filename = NULL;
		sFilename = NULL;
		setfname(arg, sarg);
	}
	else if (newlnum == 0)
		newlnum = Curpos.lnum;
	maketitle();
	if (check_fname())
		return FALSE;

	/* clear mem and read file */
	freeall();
	filealloc();
	startop.lnum = 0;	/* clear '[ and '] marks */
	endop.lnum = 0;

	redraw_save = RedrawingDisabled;
	RedrawingDisabled = TRUE;		/* don't redraw until the cursor is in
									 * the right line */
	startscript();					/* re-start auto script file */
	readfile(Filename, sFilename, (linenr_t)0, TRUE);
	if (newlnum && command == NULL)
	{
		if (newlnum != INVLNUM)
			Curpos.lnum = newlnum;
		else
			Curpos.lnum = line_count;
		Curpos.col = 0;
	}
	UNCHANGED;
	if (command)
		docmdline((u_char *)command);
	RedrawingDisabled = redraw_save;	/* cursupdate() will redraw the screen later */
	if (p_im)
		stuffReadbuff("i");			/* start editing in insert mode */
	return TRUE;
}

	static void
doshell(cmd)
	char	*cmd;
{
	/*
	 * Disallow shell commands from .exrc and .vimrc in current directory for
	 * security reasons.
	 */
	if (secure)
	{
		secure = 2;
		emsg(e_curdir);
		return;
	}
	stoptermcap();
	outchar('\n');					/* shift screen one line up */

		/* warning message before calling the shell */
	if (p_warn && Changed)
	{
		gotocmdline(TRUE, NUL);
		outstr("[No write since last change]\n");
	}
	call_shell(cmd, 0, TRUE);

#ifdef AMIGA
	wait_return(!term_console);		/* see below */
#else
	wait_return(TRUE);				/* includes starttermcap() */
#endif

	/*
	 * In an Amiga window redrawing is caused by asking the window size.
	 * If we got an interrupt this will not work. The chance that the window
	 * size is wrong is very small, but we need to redraw the screen.
	 */
#ifdef AMIGA
	if (term_console)
	{
		outstr("\033[0 q"); 	/* get window size */
		if (got_int)
			must_redraw = CLEAR;	/* if got_int is TRUE we have to redraw */
		else
			must_redraw = FALSE;	/* no extra redraw needed */
	}
#endif /* AMIGA */
}

/*
 * dofilter: filter lines through a command given by the user
 *
 * We use temp files and the call_shell() routine here. This would normally
 * be done using pipes on a UNIX machine, but this is more portable to
 * the machines we usually run on. The call_shell() routine needs to be able
 * to deal with redirection somehow, and should handle things like looking
 * at the PATH env. variable, and adding reasonable extensions to the
 * command name given by the user. All reasonable versions of call_shell()
 * do this.
 * We use input redirection if do_in is TRUE.
 * We use output redirection if do_out is TRUE.
 */
	static void
dofilter(buff, do_in, do_out)
	u_char		*buff;
	int			do_in, do_out;
{
#ifdef LATTICE
	char		itmp[L_tmpnam];		/* use tmpnam() */
	char		otmp[L_tmpnam];
#else
	char		itmp[TMPNAMELEN];
	char		otmp[TMPNAMELEN];
#endif
	linenr_t 	linecount;

	/*
	 * Disallow shell commands from .exrc and .vimrc in current directory for
	 * security reasons.
	 */
	if (secure)
	{
		secure = 2;
		emsg(e_curdir);
		return;
	}
	if (*buff == NUL)		/* no filter command */
		return;
	linecount = line2 - line1 + 1;
	Curpos.lnum = line1;
	Curpos.col = 0;
	/* cursupdate(); */

	/*
	 * 1. Form temp file names
	 * 2. Write the lines to a temp file
	 * 3. Run the filter command on the temp file
	 * 4. Read the output of the command into the buffer
	 * 5. Delete the original lines to be filtered
	 * 6. Remove the temp files
	 */

#ifndef LATTICE
	/* for lattice we use tmpnam(), which will make its own name */
	strcpy(itmp, TMPNAME1);
	strcpy(otmp, TMPNAME2);
#endif

	if ((do_in && *mktemp(itmp) == NUL) || (do_out && *mktemp(otmp) == NUL))
	{
		emsg(e_notmp);
		return;
	}

/*
 * ! command will be overwritten by next mesages
 * This is a trade off between showing the command and not scrolling the
 * text one line up (problem on slow terminals).
 */
	must_redraw = CLEAR;		/* screen has been shifted up one line */
	if (do_in && !writeit(itmp, NULL, line1, line2, FALSE, 0, FALSE))
	{
		outchar('\n');			/* keep message from writeit() */
		emsg(e_notcreate);
		return;
	}
	if (!do_out)
		outchar('\n');

#ifdef UNIX
/*
 * put braces around the command (for concatenated commands)
 */
 	sprintf(IObuff, "(%s)", (char *)buff);
	if (do_in)
	{
		strcat(IObuff, " < ");
		strcat(IObuff, itmp);
	}
	if (do_out)
	{
		strcat(IObuff, " > ");
		strcat(IObuff, otmp);
	}
#else
/*
 * for shells that don't understand braces around commands, at least allow
 * the use of commands in a pipe.
 */
	strcpy(IObuff, (char *)buff);
	if (do_in)
	{
		char		*p;
	/*
	 * If there is a pipe, we have to put the '<' in front of it
	 */
		p = strchr(IObuff, '|');
		if (p)
			*p = NUL;
		strcat(IObuff, " < ");
		strcat(IObuff, itmp);
		p = strchr((char *)buff, '|');
		if (p)
			strcat(IObuff, p);
	}
	if (do_out)
	{
		strcat(IObuff, " > ");
		strcat(IObuff, otmp);
	}
#endif

	call_shell(IObuff, 1, FALSE);	/* errors are ignored, so you can see the error
								   messages from the command; use 'u' to fix the
								   text */

	if (do_out)
	{
		if (!u_save((linenr_t)(line2), (linenr_t)(line2 + 1)))
		{
			linecount = 0;
			goto error;
		}
		if (readfile(otmp, NULL, line2, FALSE))
		{
			outchar ('\n');
			emsg(e_notread);
			linecount = 0;
			goto error;
		}

		if (do_in)
		{
			Curpos.lnum = line1;
			dellines(linecount, TRUE, TRUE);
		}
	}
	else
	{
error:
		wait_return(FALSE);
	}
	updateScreen(CLEAR);		/* do this before messages below */

	if (linecount > p_report)
	{
		if (!do_in && do_out)
			msgmore(linecount);
		else
			smsg("%ld lines filtered", (long)linecount);
	}
	remove(itmp);
	remove(otmp);
	return;
}

	static void
domake(arg)
	char *arg;
{
	if (*p_ef == NUL)
	{
		emsg("errorfile option not set");
		return;
	}
	if (Changed)
		autowrite();
	remove(p_ef);
	outchar(':');
	outstr(arg);		/* show what we are doing */
#ifdef UNIX
	sprintf(IObuff, "%s |& tee %s", arg, p_ef);
#else
	sprintf(IObuff, "%s > %s", arg, p_ef);
#endif
	doshell(IObuff);
#ifdef AMIGA
	flushbuf();
	vpeekc();		/* read window status report and redraw before message */
#endif
	qf_init();
	remove(p_ef);
}

/* 
 * Redefine the argument list to 'str'.
 * Return TRUE for failure.
 */
	static int
doarglist(str)
	char *str;
{
	int		new_numfiles = 0;
	char	**new_files = NULL;
	int		exp_numfiles;
	char	**exp_files;
	char	**t;
	char	*p;
	int		inquote;
	int		i;

	while (*str)
	{
		/*
		 * create a new entry in new_files[]
		 */
		t = (char **)lalloc((u_long)(sizeof(char *) * (new_numfiles + 1)), TRUE);
		if (t != NULL)
			for (i = new_numfiles; --i >= 0; )
				t[i] = new_files[i];
		free(new_files);
		if (t == NULL)
			return TRUE;
		new_files = t;
		new_files[new_numfiles++] = str;

		/*
		 * isolate one argument, taking quotes
		 */
		inquote = FALSE;
		for (p = str; *str; ++str)
		{
			if (*str == '\\' && *(str + 1) != NUL)
				*p++ = *++str;
			else
			{
				if (!inquote && isspace(*str))
					break;
				if (*str == '"')
					inquote ^= TRUE;
				else
					*p++ = *str;
			}
		}
		skipspace(&str);
		*p = NUL;
	}
	
	if (ExpandWildCards(new_numfiles, new_files, &exp_numfiles, &exp_files, FALSE, TRUE) != 0)
		return TRUE;
	else if (exp_numfiles == 0)
	{
		emsg(e_nomatch);
		return TRUE;
	}
	if (files_exp)			/* files[] has been allocated, free it */
		FreeWild(numfiles, files);
	else
		files_exp = TRUE;
	files = exp_files;
	numfiles = exp_numfiles;

	return FALSE;
}

	void
gotocmdline(clr, firstc)
	int				clr;
	int				firstc;
{
	int		i;

	if (clr)			/* clear the bottom line(s) */
	{
		for (i = 0; i <= cmdoffset; ++i)
		{
			windgoto((int)Rows - i - 1, 0);
			clear_line();
		}
		redraw_msg = TRUE;
	}
	windgoto((int)Rows - cmdoffset - 1, 0);
	if (firstc)
		outchar(firstc);
}

	void
gotocmdend()
{
	windgoto((int)Rows - 1, 0);
	outchar('\n');
}

	static int
check_readonly()
{
	if (!forceit && p_ro)
	{
		emsg(e_readonly);
		return TRUE;
	}
	return FALSE;
}

	static int
check_changed(checkaw)
	int		checkaw;
{
	if (!forceit && Changed && (!checkaw || !autowrite()))
	{
		emsg(e_nowrtmsg);
		return TRUE;
	}
	return FALSE;
}

	int
check_fname()
{
	if (Filename == NULL)
	{
		emsg(e_noname);
		return TRUE;
	}
	return FALSE;
}

	static int
check_more(message)
	int message;			/* when FALSE check only, no messages */
{
	if (!forceit && curfile + 1 < numfiles && quitmore == 0)
	{
		if (message)
		{
			emsg(e_more);
			quitmore = 2;			/* next try to quit is allowed */
		}
		return TRUE;
	}
	return FALSE;
}

/*
 * try to abandon current file and edit "fname"
 * return 1 for "normal" error, 2 for "not written" error, 0 for success
 * -1 for succesfully opening another file
 */
	int
getfile(fname, sfname, setpm)
	char	*fname;
	char	*sfname;
	int		setpm;
{
	int other;

	other = otherfile(fname);
	if (other && !forceit && Changed && !autowrite())
	{
		emsg(e_nowrtmsg);
		return 2;		/* file has been changed */
	}
	if (setpm)
		setpcmark();
	if (!other)
		return 0;		/* it's in the same file */
	if (doecmd(fname, sfname))
		return -1;		/* opened another file */
	return 1;			/* error encountered */
}

/*
 * return TRUE if alternate file n is the same as the current file
 */
	int
samealtfile(n)
	int			n;
{
	if (n < NUMALTFILES && altfiles[n] != NULL && Filename != NULL &&
					fnamecmp(altfiles[n], Filename) == 0)
		return TRUE;
	return FALSE;
}

/*
 * get alternate file n
 * set linenr to lnum or altlnum if lnum == 0
 * if (setpm) setpcmark
 * return 1 for failure, 0 for success
 */
	int
getaltfile(n, lnum, setpm)
	int			n;
	linenr_t	lnum;
	int			setpm;
{
	if (n < 0 || n >= NUMALTFILES || altfiles[n] == NULL)
	{
		emsg(e_noalt);
		return 1;
	}
	if (lnum == 0)
		lnum = altlnum[n];		/* altlnum may be changed by getfile() */
	RedrawingDisabled = TRUE;
	if (getfile(altfiles[n], saltfiles[n], setpm) <= 0)
	{
		RedrawingDisabled = FALSE;
		if (lnum == 0 || lnum > line_count)		/* check for valid lnum */
			Curpos.lnum = 1;
		else
			Curpos.lnum = lnum;

		Curpos.col = 0;
		return 0;
	}
	RedrawingDisabled = FALSE;
	return 1;
}

/*
 * get name of "n"th alternate file
 */
 	char *
getaltfname(n)
	int n;
{
	if (n >= NUMALTFILES)
		return NULL;
	return altfiles[n];
}

/*
 * put name "arg" in the list of alternate files.
 * "arg" must have been allocated
 * "lnum" is the default line number when jumping to the file
 * "newfile" must be TRUE when "arg" != current file
 */
	static void
setaltfname(arg, sarg, lnum, newfile)
	char		*arg;
	char		*sarg;
	linenr_t	lnum;
	int			newfile;
{
	int i;

	free(altfiles[NUMALTFILES - 1]);
	free(saltfiles[NUMALTFILES - 1]);
	for (i = NUMALTFILES - 1; i > 0; --i)
	{
		altfiles[i] = altfiles[i - 1];
		saltfiles[i] = saltfiles[i - 1];
		altlnum[i] = altlnum[i - 1];
	}
	incrmarks();		/* increment file number for all jumpmarks */
	incrtags();			/* increment file number for all tags */
	if (newfile)
	{
		decrmarks();		/* decrement file number for jumpmarks in current file */
		decrtags();			/* decrement file number for tags in current file */
	}

	altfiles[0] = arg;
	saltfiles[0] = sarg;
	altlnum[0] = lnum;
}

	static void
nextwild(buff, type)
	u_char *buff;
	int		type;
{
	int		i;
	char	*p1, *p2;
	int		oldlen;
	int		difflen;

	outstr("...");		/* show that we are busy */
	flushbuf();
	i = cmdslen;
	cmdslen = cmdpos + 4;
	cmdchecklen();		/* check if we caused a scrollup */
	cmdslen = i;

	for (i = cmdpos; i > 0 && buff[i - 1] != ' '; --i)
		;
	oldlen = cmdpos - i;

		/* add a "*" to the file name and expand it */
	if ((p1 = addstar((char *)&buff[i], oldlen)) != NULL)
	{
		if ((p2 = ExpandOne((u_char *)p1, FALSE, type)) != NULL)
		{
			if (cmdlen + (difflen = strlen(p2) - oldlen) > CMDBUFFSIZE - 4)
				emsg(e_toolong);
			else
			{
				strncpy((char *)&buff[cmdpos + difflen], (char *)&buff[cmdpos], (size_t)(cmdlen - cmdpos));
				strncpy((char *)&buff[i], p2, strlen(p2));
				cmdlen += difflen;
				cmdpos += difflen;
			}
			free(p2);
		}
		free(p1);
	}
	redrawcmd();
}

/*
 * Do wildcard expansion on the string 'str'.
 * Return a pointer to alloced memory containing the new string.
 * Return NULL for failure.
 *
 * mode = -2: only release file names
 * mode = -1: normal expansion, do not keep file names
 * mode =  0: normal expansion, keep file names
 * mode =  1: use next match in multiple match
 * mode =  2: use previous match in multiple match
 * mode =  3: use next match in multiple match and wrap to first
 * mode =  4: return all matches concatenated
 * mode =  5: return longest matched part
 */
	char *
ExpandOne(str, list_notfound, mode)
	u_char	*str;
	int		list_notfound;
	int		mode;
{
	char		*ss = NULL;
	static char **cmd_files = NULL;	  /* list of input files */
	static int	findex;
	int			i, found = 0;
	int			multmatch = FALSE;
	u_long		len;
	char		*filesuf, *setsuf, *nextsetsuf;
	int			filesuflen, setsuflen;

/*
 * first handle the case of using an old match
 */
	if (mode >= 1 && mode < 4)
	{
		if (cmd_numfiles > 0)
		{
			if (mode == 2)
				--findex;
			else	/* mode == 1 || mode == 3 */
				++findex;
			if (findex < 0)
				findex = 0;
			if (findex > cmd_numfiles - 1)
			{
				if (mode == 3)
					findex = 0;
				else
					findex = cmd_numfiles - 1;
			}
			return strsave(cmd_files[findex]);
		}
		else
			return NULL;
	}

/* free old names */
	if (cmd_numfiles != -1 && mode < 4)
	{
		FreeWild(cmd_numfiles, cmd_files);
		cmd_numfiles = -1;
	}
	findex = 0;

	if (mode == -2)		/* only release file name */
		return NULL;

	if (cmd_numfiles == -1)
	{
		if (ExpandWildCards(1, (char **)&str, &cmd_numfiles, &cmd_files, FALSE, list_notfound) != 0)
			/* error: do nothing */;
		else if (cmd_numfiles == 0)
			emsg(e_nomatch);
		else if (mode < 4)
		{
			if (cmd_numfiles > 1)		/* more than one match; check suffixes */
			{
				found = -2;
				for (i = 0; i < cmd_numfiles; ++i)
				{
					if ((filesuf = strrchr(cmd_files[i], '.')) != NULL)
					{
						filesuflen = strlen(filesuf);
						for (setsuf = p_su; *setsuf; setsuf = nextsetsuf)
						{
							if ((nextsetsuf = strchr(setsuf + 1, '.')) == NULL)
								nextsetsuf = setsuf + strlen(setsuf);
							setsuflen = (int)(nextsetsuf - setsuf);
							if (filesuflen == setsuflen &&
										strncmp(setsuf, filesuf, (size_t)setsuflen) == 0)
								break;
						}
						if (*setsuf)				/* suffix matched: ignore file */
							continue;
					}
					if (found >= 0)
					{
						multmatch = TRUE;
						break;
					}
					found = i;
				}
			}
			if (multmatch || found < 0)
			{
				emsg(e_toomany);
				found = 0;				/* return first one */
				multmatch = TRUE;		/* for found < 0 */
			}
			if (found >= 0 && !(multmatch && mode == -1))
				ss = strsave(cmd_files[found]);
		}
	}

	if (mode == 5 && cmd_numfiles > 0)		/* find longest common part */
	{
		for (len = 0; cmd_files[0][len]; ++len)
		{
			for (i = 0; i < cmd_numfiles; ++i)
			{
#ifdef AMIGA
				if (toupper(cmd_files[i][len]) != toupper(cmd_files[0][len]))
#else
				if (cmd_files[i][len] != cmd_files[0][len])
#endif
					break;
			}
			if (i < cmd_numfiles)
				break;
		}
		ss = alloc((unsigned)len + 1);
		if (ss)
		{
			strncpy(ss, cmd_files[0], (size_t)len);
			ss[len] = NUL;
		}
		multmatch = TRUE;					/* don't free the names */
		findex = -1;						/* next p_wc gets first one */
	}

	if (mode == 4 && cmd_numfiles > 0)		/* concatenate all file names */
	{
		len = 0;
		for (i = 0; i < cmd_numfiles; ++i)
			len += strlen(cmd_files[i]) + 1;
		ss = lalloc(len, TRUE);
		if (ss)
		{
			*ss = NUL;
			for (i = 0; i < cmd_numfiles; ++i)
			{
				strcat(ss, cmd_files[i]);
				if (i != cmd_numfiles - 1)
					strcat(ss, " ");
			}
		}
	}

	if (!multmatch || mode == -1 || mode == 4)
	{
		FreeWild(cmd_numfiles, cmd_files);
		cmd_numfiles = -1;
	}
	return ss;
}

/*
 * show all filenames that match the string "file" with length "len"
 */
	static void
showmatches(file, len)
	char *file;
	int	len;
{
	char *file_str;
	int num_files;
	char **files_found;
	int i, j, k;
	int maxlen;
	int lines;
	int columns;

	file_str = addstar(file, len);		/* add star to file name */
	if (file_str != NULL)
	{
		outchar('\n');
		flushbuf();

		/* find all files that match the description */
		ExpandWildCards(1, &file_str, &num_files, &files_found, FALSE, FALSE);

		/* find the maximum length of the file names */
		maxlen = 0;
		for (i = 0; i < num_files; ++i)
		{
			j = strlen(files_found[i]);
			if (j > maxlen)
				maxlen = j;
		}

		/* compute the number of columns and lines for the listing */
		maxlen += 2;	/* two spaces between file names */
		columns = ((int)Columns + 2) / maxlen;
		if (columns < 1)
			columns = 1;
		lines = (num_files + columns - 1) / columns;

		/* list the files line by line */
#ifdef AMIGA
		settmode(0);		/* allow output to be halted */
#endif
		for (i = 0; i < lines; ++i)
		{
			for (k = i; k < num_files; k += lines)
			{
				if (k > i)
					for (j = maxlen - strlen(files_found[k - lines]); --j >= 0; )
						outchar(' ');
				j = isdir(files_found[k]);	/* highlight directories */
				if (j > 0)
				{
#ifdef AMIGA
					if (term_console)
						outstr("\033[33m");		/* use highlight color */
					else
#endif /* AMIGA */
						outstr(T_TI);
				}
				outstrn(files_found[k]);
				if (j > 0)
				{
#ifdef AMIGA
					if (term_console)
						outstr("\033[0m");		/* use normal color */
					else
#endif /* AMIGA */
						outstr(T_TP);
				}
			}
			outchar('\n');
			flushbuf();
		}
		free(file_str);
		FreeWild(num_files, files_found);
#ifdef AMIGA
		settmode(1);
#endif

		for (i = cmdoffset; --i >= 0; )	/* make room for the command */
			outchar('\n');
		must_redraw = CLEAR;			/* must redraw later */
	}
}

/*
 * copy the file name into allocated memory and add a '*' at the end
 */
	static char *
addstar(fname, len)
	char	*fname;
	int		len;
{
	char	*retval;
#ifdef MSDOS
	int		i;
#endif

	retval = alloc(len + 4);
	if (retval != NULL)
	{
		strncpy(retval, fname, (size_t)len);
#ifdef MSDOS
	/*
	 * if there is no dot in the file name, add "*.*" instead of "*".
	 */
		for (i = len - 1; i >= 0; --i)
			if (strchr(".\\/:", retval[i]))
				break;
		if (i < 0 || retval[i] != '.')
		{
			retval[len++] = '*';
			retval[len++] = '.';
		}
#endif
		retval[len] = '*';
		retval[len + 1] = 0;
	}
	return retval;
}

/*
 * dosource: read the file "fname" and execute its lines as EX commands
 *
 * This function may be called recursively!
 */
	int
dosource(fname)
	register char *fname;
{
	register FILE	*fp;
	register int	len;
#ifdef MSDOS
	int				error = FALSE;
#endif

	expand_env(fname, IObuff, IOSIZE);		/* use IObuff for expanded name */
	if ((fp = fopen(IObuff, READBIN)) == NULL)
		return 1;

	len = 0;
	while (fgets(IObuff + len, IOSIZE - len, fp) != NULL && !got_int)
	{
		len = strlen(IObuff) - 1;
		if (len >= 0 && IObuff[len] == '\n')	/* remove trailing newline */
		{
				/* escaped newline, read more */
			if (len > 0 && len < IOSIZE && IObuff[len - 1] == Ctrl('V'))
			{
				IObuff[len - 1] = '\n';		/* remove CTRL-V */
				continue;
			}
#ifdef MSDOS
			if (len > 0 && IObuff[len - 1] == '\r') /* trailing CR-LF */
				--len;
			else
			{
				if (!error)
					emsg("Warning: Wrong line separator, ^M may be missing");
				error = TRUE;		/* lines like ":map xx yy^M" will fail */
			}
#endif
			IObuff[len] = NUL;
		}
		breakcheck();		/* check for ^C here, so recursive :so will be broken */
		docmdline((u_char *)IObuff);
		len = 0;
	}
	fclose(fp);
	if (got_int)
		emsg(e_interr);
	return 0;
}

/*
 * get single EX address
 */
	static linenr_t
get_address(ptr)
	u_char		**ptr;
{
	linenr_t	curpos_lnum = Curpos.lnum;
	int			c;
	int			i;
	long		n;
	u_char  	*cmd;
	FPOS		pos;
	FPOS		*fp;
	linenr_t	lnum;

	cmd = *ptr;
	skipspace((char **)&cmd);
	lnum = INVLNUM;
	do
	{
		switch (*cmd)
		{
			case '.': 						/* '.' - Cursor position */
						++cmd;
						lnum = curpos_lnum;
						break;

			case '$': 						/* '$' - last line */
						++cmd;
						lnum = line_count;
						break;

			case '\'': 						/* ''' - mark */
						if (*++cmd == NUL || (fp = getmark(*cmd++, FALSE)) == NULL)
						{
							emsg(e_umark);
							goto error;
						}
						lnum = fp->lnum;
						break;

			case '/':
			case '?':						/* '/' or '?' - search */
						c = *cmd++;
						pos = Curpos;		/* save Curpos */
						Curpos.col = -1;	/* searchit() will increment the col */
						if (c == '/')
						{
						 	if (Curpos.lnum == line_count)	/* :/pat on last line */
								Curpos.lnum = 1;
							else
								++Curpos.lnum;
						}
						searchcmdlen = 0;
						if (dosearch(c, (char *)cmd, FALSE, (long)1, FALSE))
							lnum = Curpos.lnum;
						Curpos = pos;
				
						cmd += searchcmdlen;	/* adjust command string pointer */
						break;

			default:
						if (isdigit(*cmd))				/* absolute line number */
							lnum = getdigits((char **)&cmd);
		}
		
		while (*cmd == '-' || *cmd == '+')
		{
			if (lnum == INVLNUM)
				lnum = curpos_lnum;
			i = *cmd++;
			if (!isdigit(*cmd))	/* '+' is '+1', but '+0' is not '+1' */
				n = 1;
			else 
				n = getdigits((char **)&cmd);
			if (i == '-')
				lnum -= n;
			else
				lnum += n;
		}

		curpos_lnum = lnum;
	} while (*cmd == '/' || *cmd == '?');

error:
	*ptr = cmd;
	return lnum;
}

/*
 * align text:
 * type = -1  left aligned
 * type = 0   centered
 * type = 1   right aligned
 */
	static void
do_align(start, end, width, type)
	linenr_t	start;
	linenr_t	end;
	int			width;
	int			type;
{
	FPOS	pos;
	int		len;
	int		indent = 0;

	pos = Curpos;
	if (type == -1)		/* left align: width is used for new indent */
	{
		if (width >= 0)
			indent = width;
	}
	else
	{
		if (width <= 0)
			width = p_tw;
		if (width == 0)
			width = 80;
	}

	if (!u_save((linenr_t)(line1 - 1), (linenr_t)(line2 + 1)))
		return;
	for (Curpos.lnum = start; Curpos.lnum <= end; ++Curpos.lnum)
	{
		set_indent(indent, TRUE);				/* remove existing indent */
		if (type == -1)							/* left align */
			continue;
		len = strsize(nr2ptr(Curpos.lnum));		/* get line lenght */
		if (len < width)
			switch (type)
			{
			case 0:		set_indent((width - len) / 2, FALSE);	/* center */
						break;
			case 1:		set_indent(width - len, FALSE);			/* right */
						break;
			}
	}
	Curpos = pos;
	beginline(TRUE);
	updateScreen(NOT_VALID);
}
