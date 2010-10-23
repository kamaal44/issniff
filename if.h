/* $Id$ */

extern int linkhdr_len;

extern char *if_detect (int);

#define SUCCESSFUL 0
#define UNSUCCESSFUL -1
#define USE_TEMP_SOCK -1

#ifdef __linux__
# include <net/if.h>
# include <linux/if_ether.h>
#endif /* __linux__ */

#ifdef __osf__
# include <sys/mbuf.h>
# include <sys/socket.h>
# include <net/route.h>
# include <net/if.h>
# include <net/if_arp.h>
# include <netinet/in.h>
# include <netinet/if_ether.h>

extern int ioctl (int, int, ...);
#endif /* __osf__ */

extern struct ifreq ifr;
