/* $Id$ */

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#ifdef __linux__
# include <getopt.h>
# include "linux.h"
#endif

/* Not finished: SunOS code not yet distributed, and messy!. */
#ifdef __sun__
# include <unistd.h>
# include "sunos.h"
#endif

/* Not even started! */
#ifdef __osf__
# define USING_BPF
# include "osf.h"
#endif

#include "sniff.h"
#include "lists.h"

/*
 * Local variables.
 */
static int cache_increment = CACHE_INC;
static int cache_max = 0;
static int cache_size = 0;
static int colorfrom = FROM_COLOR;
static int colorize = 0;
static int colorto = TO_COLOR;
static int curr_conn = 0;
static int hiport = 0;
static int maxdata = IS_MAXDATA;
static int nolocal = OS_NOLOCAL;
static int squash_output = 0;
static int timeout = IS_TIMEOUT;
static int verbose = 0;
static PList *cache;
static Ports *ports;
static int stats[] = { 0, 0, 0, 0 };

enum { s_tot, s_finrst, s_maxdata, s_timeout };
  
/*
 * Local function prototypes.
 */
static void dump_conns (int);
static void dump_node (const PList *, const char *);
static void show_conns (int);
static void show_state (int);
static PList *find_node (PORT_T, ADDR_T, PORT_T, ADDR_T);

/*
 * Signal handler.
 */
static void
dump_conns (int sig)
{
  int i;
  PList *node;

  if (sig == SIGHUP) {
    signal (SIGHUP, dump_conns);
  }
  for (i = 0; i <= hiport; i++) {
    if ((node = ports[i].next)) {
      while (node) {
	dump_node (node, "SIGNAL");
	node = node->next;
      }
    }
  }
  if (sig == SIGHUP) {
    return;
  }
  if_close (sig);
}

/*
 * Signal handler.
 */
static void
show_conns (int sig)
{
  char *timep;
  int i;
  PList *node;

  signal (sig, show_conns);
  fputs ("\n** Active connections:\n", stderr);

  for (i = 0; i <= hiport; i++) {
    if ((node = ports[i].next)) {
      while (node) {
	timep = ctime (&node->stime);
	timep[strlen (timep) - 1] = 0; /* Zap newline */
	MENTION(node->dport, node->daddr, node->sport, node->saddr, timep);
	node = node->next;
      }
    }
  }
  fputc ('\n', stderr);
}

/*
 * Signal handler.
 */
static void
show_state (int sig)
{
  int i;

  signal (sig, show_state);
  fprintf (stderr, "\n\
** Current state:\n\
*  Version: %s\n\
*  Interface: %s\n\
*  Active connections: %d\n\
*  Current cache entries: %d\n\
*  Max cache entries: %d\n\
*  Cache increment: %d\n\
*  Max data size (bytes): %d\n\
*  Idle timeout (seconds): %d\n\
*  Ignoring local connections/packets: %s\n\
*  Squashed output: %s\n\
*  Verbose mode: %s\n\
*  Ted Turner mode (colorization): %s\n\
*  Stats:\n\
*    Total connections:  %d\n\
*    FIN/RST terminated: %d\n\
*    Exceeded data size: %d\n\
*    Exceeded timeout:   %d\n\
*  Monitoring ports:",
	   IS_VERSION,
	   if_getname (),
	   curr_conn,
	   cache_size,
	   cache_max,
	   cache_increment,
	   maxdata,
	   timeout,
	   YN (nolocal),
	   YN (squash_output),
	   YN (verbose),
	   YN (colorize),
	   stats[s_tot],
	   stats[s_finrst],
	   stats[s_maxdata],
	   stats[s_timeout]);

  for (i = 0; i <= hiport; i++) {
    if (ports[i].port) {
      if (ports[i].twoway) {
	fprintf (stderr, " +%d", i);
      } else {
	fprintf (stderr, " %d", i);
      }
    }
  }
  fputs ("\n\n", stderr);
}

/*
 * Does double duty as a node-finder and as a timeout routine.
 */
static PList *
find_node (PORT_T dport, ADDR_T daddr, PORT_T sport, ADDR_T saddr)
{
  PList *node = ports[dport].next;
  time_t now = time (NULL);

  while (node) {
    /* What's the optimal order for these, I wonder? */
    if ((node->sport == sport) && (node->saddr == saddr) &&
	(node->daddr == daddr)) {
      return node;
    }
    /* Timeout stanza. */
    if (timeout && (now - node->timeout > timeout)) {
      PList *nnode = node->next;
      ++stats[s_timeout];
      END_NODE (node, dport, "TIMEOUT");
      node = nnode;
    } else {
      node = node->next;
    }
  }
  return node;
}

/*
 * Finds the packets we're interested in and does fun things with them.
 * Made more complicated by adding two-way monitoring capabilities on
 * selectable ports.
 *
 * Need to check FIN/RST packets from remote end (e.g. connection
 * refused), even when not in two-way mode.
 */
void
filter (UCHAR *buf)
{
  enum { data_to = 0, data_from = 8 };
  IPhdr *iph = (IPhdr *)buf;

#ifndef USING_BPF
  if (IPPROT (iph) != TCPPROT) { /* Only looking at TCP/IP right now. */
    return;
  } else {
#endif /* USING_BPF */
    TCPhdr *tcph = (TCPhdr *)(buf + IPHLEN (iph));
    PORT_T dport = ntohs (DPORT (tcph)), sport = ntohs (SPORT (tcph));

    if (dport > hiport || !ports[dport].port) {
      if (sport <= hiport && ports[sport].twoway) {
	ADDR_T daddr = DADDR (iph), saddr = SADDR (iph);
	PList *node = find_node (sport, saddr, dport, daddr); /* Backwards. */

	if (node) {
	  ++node->pkts[pkt_from];
	  ADD_DATA (node, buf + IPHLEN (iph) + DOFF (tcph), iph, tcph,
		    data_from);

	  if (FINRST (tcph)) {
	    ++stats[s_finrst];
	    END_NODE (node, sport, "<-FIN/RST");
	  }
	}
      }
    } else {
      ADDR_T daddr = DADDR (iph), saddr = SADDR (iph);
      PList *node = find_node (dport, daddr, sport, saddr);

      if (!node) {
	if (SYN (tcph)) {
	  ADD_NODE (dport, daddr, sport, saddr);

	  if (verbose) {
	    MENTION (dport, daddr, sport, saddr, "New connection");
	  }
	}
      } else {
	++node->pkts[pkt_to];
	ADD_DATA (node, buf + IPHLEN (iph) + DOFF (tcph), iph, tcph, data_to);

	if (FINRST (tcph)) {
	  ++stats[s_finrst];
	  END_NODE (node, dport, "FIN/RST->");
	}
      }
    }
#ifndef USING_BPF
  }
#endif /* USING_BPF */
}

int
main (int argc, char **argv)
{
  if (argc > 1) {
    char opt;
    int i;
    
    while ((opt = getopt (argc, argv, "F:T:c:d:i:t:Cnsv")) != -1) {
      switch (opt) {
      case 'C':
	colorize = 1;
	break;
      case 'F':
	colorfrom = CHKOPT (FROM_COLOR);
	colorize = 1;
	break;
      case 'T':
	colorto = CHKOPT (TO_COLOR);
	colorize = 1;
	break;
      case 'c':
	cache_increment = CHKOPT (CACHE_INC);
	break;
      case 'd':
	maxdata = CHKOPT (IS_MAXDATA);
	break;
      case 'i':
	if (if_setname (optarg) == -1) {
	  fprintf (stderr, "Invalid/unknown interface: %s\n", optarg);
	  return 1;
	}
	break;
      case 'n':
	nolocal = 1;
	break;
      case 's':
	squash_output = 1;
	break;
      case 't':
	timeout = CHKOPT (IS_TIMEOUT);
	break;
      case 'v':
	verbose = 1;
	break;
      default:
	fputs ("Usage: issniff [options] [+]port [[+]port ...]\n", stderr);
	return 1;
      }
    }
    for (i = optind; i < argc; i++) {
      int thisport = argv[i][0] == '+' ? atoi (&argv[i][1]) : atoi (argv[i]);
      hiport = thisport > hiport ? thisport : hiport;
    }
    /* Yes, wasting some memory for the sake of speed. */
    if (!(ports = (Ports *)malloc (sizeof (Ports) * (hiport + 1)))) {
      perror ("malloc");
      exit (errno);
    }
    memset (ports, 0, sizeof (Ports) * (hiport + 1));

    for (i = optind; i < argc; i++) {
      int thisport = argv[i][0] == '+' ? atoi (&argv[i][1]) : atoi (argv[i]);
      ports[thisport].port = 1;
      ports[thisport].twoway = argv[i][0] == '+' ? 1 : 0;
    }
  } else {
    fputs ("Must specify some ports!\n", stderr);
    return 1;
  }
  if_open (nolocal);
  signal (SIGQUIT, if_close);
  signal (SIGTERM, if_close);
  /* Initialize cache. */
  if (!(cache = (PList *)malloc (sizeof (PList)))) {
    perror ("malloc");
    exit (errno);
  }
  cache->next = NULL;
  EXPAND_CACHE;			/* Get ready for first packet. */
  signal (SIGINT, dump_conns);
  signal (SIGHUP, dump_conns);
  signal (SIGUSR1, show_state);
  signal (SIGUSR2, show_conns);
  if_read ();			/* Main loop. */
  if_close (0);			/* Not reached. */
  return 0;
}

/*
 * Will probably be moved to children.
 */
static void
dump_node (const PList *node, const char *reason)
{
  int current_color = NO_COLOR;
  struct in_addr ia;
  UCHAR data, lastc = 0;
  UDATA *datp = node->data;
  UINT dlen = node->dlen;
  char *timep = ctime (&node->stime);
  time_t now = time (NULL);

  puts ("========================================================================");
  timep[strlen (timep) - 1] = 0; /* Zap newline. */
  printf ("Time: %s ", timep);
  printf ("to %s", ctime (&now)); /* Two calls to printf() for a reason! */
  ia.s_addr = node->saddr;
  printf ("Path: %s:%d %s ", inet_ntoa (ia), node->sport, 
	  ports[node->dport].twoway ? "<->" : "->");
  ia.s_addr = node->daddr;
  printf ("%s:%d\n", inet_ntoa (ia), node->dport);
  printf ("Stat: %d/%d packets (to/from), %d bytes [%s]\n", node->pkts[pkt_to],
	  node->pkts[pkt_from], dlen, reason);
  puts ("------------------------------------------------------------------------");

  while (dlen-- > 0) {
    /* Ack, can't tell which way NULL bytes came from this way. */
    if (*datp > 0x00ff) {
      data = *datp++ >> 8;

      if (colorize && current_color != colorfrom) {
	printf ("%c[%dm", 0x1b, current_color = colorfrom);
      }
    } else {
      data = *datp++;

      if (colorize && current_color != colorto) {
	printf ("%c[%dm", 0x1b, current_color = colorto);
      }
    }
    if (data >= 127) {
      printf ("<%d>", data);
    } else if (data >= 32) {
      putchar (data);
    } else {
      switch (data) {
      case '\0':
	if ((lastc == '\r') || (lastc == '\n') || (lastc =='\0')) {
	  break;
	}
      case '\r':
      case '\n':
	/* Can't tell which end a \n came from either. */
	if (!squash_output || !((lastc == '\r') || (lastc == '\n'))) {
	  putchar ('\n');
	}
	break;
      case '\t':
	/* Add an option to print the control code instead of expanding it? */
	putchar ('\t');
	break;
      default:
	printf ("<^%c>", (data + 64));
	break;
      }
    }
    lastc = data;
  }
  if (colorize && current_color != NO_COLOR) {
    printf ("%c[00m", 0x1b);
  }
  puts ("\n========================================================================");
}
