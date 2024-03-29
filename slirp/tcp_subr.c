/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp_subr.c	8.1 (Berkeley) 6/10/93
 * tcp_subr.c,v 1.5 1994/10/08 22:39:58 phk Exp
 */

/*
 * Changes and additions relating to SLiRP
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#define WANT_SYS_IOCTL_H
#include <slirp.h>
#define SLIRP_COMPILATION
#include "sockets.h"
#include "proxy_common.h"

/* patchable/settable parameters for tcp */
int 	tcp_mssdflt = TCP_MSS;
int 	tcp_rttdflt = TCPTV_SRTTDFLT / PR_SLOWHZ;
int	tcp_do_rfc1323 = 0;	/* Don't do rfc1323 performance enhancements */
int	tcp_rcvspace;	/* You may want to change this */
int	tcp_sndspace;	/* Keep small if you have an error prone link */

/*
 * Tcp initialization
 */
void
tcp_init()
{
	tcp_iss = 1;		/* wrong */
	tcb.so_next = tcb.so_prev = &tcb;

	/* tcp_rcvspace = our Window we advertise to the remote */
	tcp_rcvspace = TCP_RCVSPACE;
	tcp_sndspace = TCP_SNDSPACE;

	/* Make sure tcp_sndspace is at least 2*MSS */
	if (tcp_sndspace < 2*(min(if_mtu, if_mru) - sizeof(struct tcpiphdr)))
		tcp_sndspace = 2*(min(if_mtu, if_mru) - sizeof(struct tcpiphdr));
}

/*
 * Create template to be used to send tcp packets on a connection.
 * Call after host entry created, fills
 * in a skeletal tcp/ip header, minimizing the amount of work
 * necessary when the connection is used.
 */
/* struct tcpiphdr * */
void
tcp_template(tp)
	struct tcpcb *tp;
{
	struct socket *so = tp->t_socket;
	register struct tcpiphdr *n = &tp->t_template;

	n->ti_next = n->ti_prev = 0;
	n->ti_x1 = 0;
	n->ti_pr = IPPROTO_TCP;
	n->ti_len = htons(sizeof (struct tcpiphdr) - sizeof (struct ip));
	n->ti_src = so->so_faddr;
	n->ti_dst = so->so_laddr;
	n->ti_sport = so->so_fport;
	n->ti_dport = so->so_lport;

	n->ti_seq = 0;
	n->ti_ack = 0;
	n->ti_x2 = 0;
	n->ti_off = 5;
	n->ti_flags = 0;
	n->ti_win = 0;
	n->ti_sum = 0;
	n->ti_urp = 0;
}

/*
 * Send a single message to the TCP at address specified by
 * the given TCP/IP header.  If m == 0, then we make a copy
 * of the tcpiphdr at ti and send directly to the addressed host.
 * This is used to force keep alive messages out using the TCP
 * template for a connection tp->t_template.  If flags are given
 * then we send a message back to the TCP which originated the
 * segment ti, and discard the mbuf containing it and any other
 * attached mbufs.
 *
 * In any case the ack and sequence number of the transmitted
 * segment are as specified by the parameters.
 */
void
tcp_respond(tp, ti, m, ack, seq, flags)
	struct tcpcb *tp;
	register struct tcpiphdr *ti;
	register MBuf m;
	tcp_seq ack, seq;
	int flags;
{
	register int tlen;
	int win = 0;

	DEBUG_CALL("tcp_respond");
	DEBUG_ARG("tp = %lx", (long)tp);
	DEBUG_ARG("ti = %lx", (long)ti);
	DEBUG_ARG("m = %lx", (long)m);
	DEBUG_ARG("ack = %u", ack);
	DEBUG_ARG("seq = %u", seq);
	DEBUG_ARG("flags = %x", flags);

	if (tp)
		win = sbuf_space(&tp->t_socket->so_rcv);
	if (m == 0) {
		if ((m = mbuf_alloc()) == NULL)
			return;
#ifdef TCP_COMPAT_42
		tlen = 1;
#else
		tlen = 0;
#endif
		m->m_data += if_maxlinkhdr;
		*MBUF_TO(m, struct tcpiphdr *) = *ti;
		ti = MBUF_TO(m, struct tcpiphdr *);
		flags = TH_ACK;
	} else {
		/*
		 * ti points into m so the next line is just making
		 * the mbuf point to ti
		 */
		m->m_data = (caddr_t)ti;

		m->m_len = sizeof (struct tcpiphdr);
		tlen = 0;
#define xchg(a,b,type) { type t; t=a; a=b; b=t; }
		xchg(ti->ti_dst.s_addr, ti->ti_src.s_addr, u_int32_t);
		xchg(ti->ti_dport, ti->ti_sport, u_int16_t);
#undef xchg
	}
	ti->ti_len = htons((u_short)(sizeof (struct tcphdr) + tlen));
	tlen += sizeof (struct tcpiphdr);
	m->m_len = tlen;

	ti->ti_next = ti->ti_prev = 0;
	ti->ti_x1 = 0;
	ti->ti_seq = htonl(seq);
	ti->ti_ack = htonl(ack);
	ti->ti_x2 = 0;
	ti->ti_off = sizeof (struct tcphdr) >> 2;
	ti->ti_flags = flags;
	if (tp)
		ti->ti_win = htons((u_int16_t) (win >> tp->rcv_scale));
	else
		ti->ti_win = htons((u_int16_t)win);
	ti->ti_urp = 0;
	ti->ti_sum = 0;
	ti->ti_sum = cksum(m, tlen);
	((struct ip *)ti)->ip_len = tlen;

	if(flags & TH_RST)
	  ((struct ip *)ti)->ip_ttl = MAXTTL;
	else
	  ((struct ip *)ti)->ip_ttl = ip_defttl;

	(void) ip_output((struct socket *)0, m);
}

/*
 * Create a new TCP control block, making an
 * empty reassembly queue and hooking it to the argument
 * protocol control block.
 */
struct tcpcb *
tcp_newtcpcb(so)
	struct socket *so;
{
	register struct tcpcb *tp;

	tp = (struct tcpcb *)malloc(sizeof(*tp));
	if (tp == NULL)
		return ((struct tcpcb *)0);

	memset((char *) tp, 0, sizeof(struct tcpcb));
	tp->seg_next = tp->seg_prev = (tcpiphdrp_32)tp;
	tp->t_maxseg = tcp_mssdflt;

	tp->t_flags = tcp_do_rfc1323 ? (TF_REQ_SCALE|TF_REQ_TSTMP) : 0;
	tp->t_socket = so;

	/*
	 * Init srtt to TCPTV_SRTTBASE (0), so we can tell that we have no
	 * rtt estimate.  Set rttvar so that srtt + 2 * rttvar gives
	 * reasonable initial retransmit time.
	 */
	tp->t_srtt = TCPTV_SRTTBASE;
	tp->t_rttvar = tcp_rttdflt * PR_SLOWHZ << 2;
	tp->t_rttmin = TCPTV_MIN;

	TCPT_RANGESET(tp->t_rxtcur,
	    ((TCPTV_SRTTBASE >> 2) + (TCPTV_SRTTDFLT << 2)) >> 1,
	    TCPTV_MIN, TCPTV_REXMTMAX);

	tp->snd_cwnd = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->snd_ssthresh = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->t_state = TCPS_CLOSED;

	so->so_tcpcb = tp;

	return (tp);
}

/*
 * Drop a TCP connection, reporting
 * the specified error.  If connection is synchronized,
 * then send a RST to peer.
 */
struct tcpcb *tcp_drop(struct tcpcb *tp, int err)
{
/* tcp_drop(tp, errno)
	register struct tcpcb *tp;
	int errno;
{
*/

	DEBUG_CALL("tcp_drop");
	DEBUG_ARG("tp = %lx", (long)tp);
	DEBUG_ARG("errno = %d", errno);

	if (TCPS_HAVERCVDSYN(tp->t_state)) {
		tp->t_state = TCPS_CLOSED;
		(void) tcp_output(tp);
		tcpstat.tcps_drops++;
	} else
		tcpstat.tcps_conndrops++;
/*	if (errno == ETIMEDOUT && tp->t_softerror)
 *		errno = tp->t_softerror;
 */
/*	so->so_error = errno; */
	return (tcp_close(tp));
}

/*
 * Close a TCP control block:
 *	discard all space held by the tcp
 *	discard internet protocol block
 *	wake up any sleepers
 */
struct tcpcb *
tcp_close(tp)
	register struct tcpcb *tp;
{
	register struct tcpiphdr *t;
	struct socket *so = tp->t_socket;
	register MBuf m;

	DEBUG_CALL("tcp_close");
	DEBUG_ARG("tp = %lx", (long )tp);

	/* free the reassembly queue, if any */
	t = (struct tcpiphdr *) tp->seg_next;
	while (t != (struct tcpiphdr *)tp) {
		t = (struct tcpiphdr *)t->ti_next;
		m = (MBuf ) REASS_MBUF((struct tcpiphdr *)t->ti_prev);
		remque_32((struct tcpiphdr *) t->ti_prev);
		mbuf_free(m);
	}
	/* It's static */
/*	if (tp->t_template)
 *		(void) mbuf_free(MBUF_FROM(tp->t_template));
 */
/*	free(tp, M_PCB);  */
	free(tp);
	so->so_tcpcb = 0;
	soisfdisconnected(so);
	/* clobber input socket cache if we're closing the cached connection */
	if (so == tcp_last_so)
		tcp_last_so = &tcb;
	closesocket(so->s);
	sbuf_free(&so->so_rcv);
	sbuf_free(&so->so_snd);
	sofree(so);
	tcpstat.tcps_closed++;
	return ((struct tcpcb *)0);
}

void
tcp_drain()
{
	/* XXX */
}

/*
 * When a source quench is received, close congestion window
 * to one segment.  We will gradually open it again as we proceed.
 */

#ifdef notdef

void
tcp_quench(i, errno)

	int errno;
{
	struct tcpcb *tp = intotcpcb(inp);

	if (tp)
		tp->snd_cwnd = tp->t_maxseg;
}

#endif /* notdef */

/*
 * TCP protocol interface to socket abstraction.
 */

/*
 * User issued close, and wish to trail through shutdown states:
 * if never received SYN, just forget it.  If got a SYN from peer,
 * but haven't sent FIN, then go to FIN_WAIT_1 state to send peer a FIN.
 * If already got a FIN from peer, then almost done; go to LAST_ACK
 * state.  In all other cases, have already sent FIN to peer (e.g.
 * after PRU_SHUTDOWN), and just have to play tedious game waiting
 * for peer to send FIN or not respond to keep-alives, etc.
 * We can let the user exit from the close as soon as the FIN is acked.
 */
void
tcp_sockclosed(tp)
	struct tcpcb *tp;
{

	DEBUG_CALL("tcp_sockclosed");
	DEBUG_ARG("tp = %lx", (long)tp);

	switch (tp->t_state) {

	case TCPS_CLOSED:
	case TCPS_LISTEN:
	case TCPS_SYN_SENT:
		tp->t_state = TCPS_CLOSED;
		tp = tcp_close(tp);
		break;

	case TCPS_SYN_RECEIVED:
	case TCPS_ESTABLISHED:
		tp->t_state = TCPS_FIN_WAIT_1;
		break;

	case TCPS_CLOSE_WAIT:
		tp->t_state = TCPS_LAST_ACK;
		break;
	}
/*	soisfdisconnecting(tp->t_socket); */
	if (tp && tp->t_state >= TCPS_FIN_WAIT_2)
		soisfdisconnected(tp->t_socket);
	if (tp)
		tcp_output(tp);
}

static void
tcp_proxy_event( struct socket*  so,
                 ProxyEvent      event )
{
    so->so_state &= ~SS_PROXIFIED;

    if (event == PROXY_EVENT_CONNECTED) {
        so->so_state &= ~(SS_ISFCONNECTING);
    }
    else {
        so->so_state = SS_NOFDREF;
    }

    /* continue the connect */
    tcp_input(NULL, sizeof(struct ip), so);
}

/*
 * Connect to a host on the Internet
 * Called by tcp_input
 * Only do a connect, the tcp fields will be set in tcp_input
 * return 0 if there's a result of the connect,
 * else return -1 means we're still connecting
 * The return value is almost always -1 since the socket is
 * nonblocking.  Connect returns after the SYN is sent, and does
 * not wait for ACK+SYN.
 */
int tcp_fconnect(so)
     struct socket *so;
{
  int ret=0;
  int try_proxy = 0;

  DEBUG_CALL("tcp_fconnect");
  DEBUG_ARG("so = %lx", (long )so);

  if( (ret=so->s=socket(AF_INET,SOCK_STREAM,0)) >= 0) {
    int s=so->s;
    struct sockaddr_in addr;

    socket_set_nonblock(s);
    socket_set_xreuseaddr(s);
    socket_set_oobinline(s);

    addr.sin_family = AF_INET;
    if ((so->so_faddr.s_addr & htonl(0xffffff00)) == special_addr.s_addr) {
      /* It's an alias */
      int  last_byte = ntohl(so->so_faddr.s_addr) & 0xff;

      if (CTL_IS_DNS(last_byte))
        addr.sin_addr = dns_addr[last_byte - CTL_DNS];
      else
        addr.sin_addr = loopback_addr;
    } else {
      addr.sin_addr = so->so_faddr;
      try_proxy = 1;
    }
    addr.sin_port = so->so_fport;

    DEBUG_MISC((dfd, " connect()ing, addr.sin_port=%d, "
                "addr.sin_addr.s_addr=%.16s\n",
                ntohs(addr.sin_port), inet_ntoa(addr.sin_addr)));

    if (try_proxy) {
        if (!proxy_manager_add(s, &addr, so, (ProxyEventFunc) tcp_proxy_event)) {
            soisfconnecting(so);
            so->so_state |= SS_PROXIFIED;
            return 0;
        }
    }

    /* We don't care what port we get */
    ret = connect(s,(struct sockaddr *)&addr,sizeof (addr));

    /*
     * If it's not in progress, it failed, so we just return 0,
     * without clearing SS_NOFDREF
     */
    soisfconnecting(so);
  }

  return(ret);
}

/*
 * Accept the socket and connect to the local-host
 *
 * We have a problem. The correct thing to do would be
 * to first connect to the local-host, and only if the
 * connection is accepted, then do an accept() here.
 * But, a) we need to know who's trying to connect
 * to the socket to be able to SYN the local-host, and
 * b) we are already connected to the foreign host by
 * the time it gets to accept(), so... We simply accept
 * here and SYN the local-host.
 */
void
tcp_connect(inso)
	struct socket *inso;
{
	struct socket *so;
	struct sockaddr_in addr;
	int addrlen = sizeof(struct sockaddr_in);
	struct tcpcb *tp;
	int s;

	DEBUG_CALL("tcp_connect");
	DEBUG_ARG("inso = %lx", (long)inso);

	/*
	 * If it's an SS_ACCEPTONCE socket, no need to socreate()
	 * another socket, just use the accept() socket.
	 */
	if (inso->so_state & SS_FACCEPTONCE) {
		/* FACCEPTONCE already have a tcpcb */
		so = inso;
	} else {
		if ((so = socreate()) == NULL) {
			/* If it failed, get rid of the pending connection */
			closesocket(accept(inso->s,(struct sockaddr *)&addr,&addrlen));
			return;
		}
		if (tcp_attach(so) < 0) {
			free(so); /* NOT sofree */
			return;
		}
		so->so_laddr = inso->so_laddr;
		so->so_lport = inso->so_lport;
	}

	(void) tcp_mss(sototcpcb(so), 0);

	if ((s = accept(inso->s,(struct sockaddr *)&addr,&addrlen)) < 0) {
		tcp_close(sototcpcb(so)); /* This will sofree() as well */
		return;
	}
	socket_set_nonblock(s);
	socket_set_xreuseaddr(s);
	socket_set_oobinline(s);
	socket_set_lowlatency(s);

	so->so_fport = addr.sin_port;
	so->so_faddr = addr.sin_addr;
	/* Translate connections from localhost to the real hostname */
	if (so->so_faddr.s_addr == 0 || so->so_faddr.s_addr == loopback_addr.s_addr)
	   so->so_faddr = alias_addr;

	/* Close the accept() socket, set right state */
	if (inso->so_state & SS_FACCEPTONCE) {
		socket_close(so->s); /* If we only accept once, close the accept() socket */
		so->so_state = SS_NOFDREF; /* Don't select it yet, even though we have an FD */
					   /* if it's not FACCEPTONCE, it's already NOFDREF */
	}
	so->s = s;

	so->so_iptos = tcp_tos(so);
	tp = sototcpcb(so);

	tcp_template(tp);

	/* Compute window scaling to request.  */
/*	while (tp->request_r_scale < TCP_MAX_WINSHIFT &&
 *		(TCP_MAXWIN << tp->request_r_scale) < so->so_rcv.sb_hiwat)
 *		tp->request_r_scale++;
 */

/*	soisconnecting(so); */ /* NOFDREF used instead */
	tcpstat.tcps_connattempt++;

	tp->t_state = TCPS_SYN_SENT;
	tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
	tp->iss = tcp_iss;
	tcp_iss += TCP_ISSINCR/2;
	tcp_sendseqinit(tp);
	tcp_output(tp);
}

/*
 * Attach a TCPCB to a socket.
 */
int
tcp_attach(so)
	struct socket *so;
{
	if ((so->so_tcpcb = tcp_newtcpcb(so)) == NULL)
	   return -1;

	insque(so, &tcb);

	return 0;
}

/*
 * Set the socket's type of service field
 */
struct tos_t tcptos[] = {
	  {0, 20, IPTOS_THROUGHPUT, 0},	/* ftp data */
	  {21, 21, IPTOS_LOWDELAY,  EMU_FTP},	/* ftp control */
	  {0, 23, IPTOS_LOWDELAY, 0},	/* telnet */
	  {0, 80, IPTOS_THROUGHPUT, 0},	/* WWW */
	  {0, 513, IPTOS_LOWDELAY, EMU_RLOGIN|EMU_NOCONNECT},	/* rlogin */
	  {0, 514, IPTOS_LOWDELAY, EMU_RSH|EMU_NOCONNECT},	/* shell */
	  {0, 544, IPTOS_LOWDELAY, EMU_KSH},		/* kshell */
	  {0, 543, IPTOS_LOWDELAY, 0},	/* klogin */
	  {0, 6667, IPTOS_THROUGHPUT, EMU_IRC},	/* IRC */
	  {0, 6668, IPTOS_THROUGHPUT, EMU_IRC},	/* IRC undernet */
	  {0, 7070, IPTOS_LOWDELAY, EMU_REALAUDIO }, /* RealAudio control */
	  {0, 113, IPTOS_LOWDELAY, EMU_IDENT }, /* identd protocol */
	  {0, 0, 0, 0}
};


/*
 * Return TOS according to the above table
 */
u_int8_t
tcp_tos(so)
	struct socket *so;
{
	int i = 0;
	struct emu_t *emup;

	while(tcptos[i].tos) {
		if ((tcptos[i].fport && (ntohs(so->so_fport) == tcptos[i].fport)) ||
		    (tcptos[i].lport && (ntohs(so->so_lport) == tcptos[i].lport))) {
			so->so_emu = tcptos[i].emu;
			return tcptos[i].tos;
		}
		i++;
	}

	return 0;
}

int do_echo = -1;

/*
 * Emulate programs that try and connect to us
 * This includes ftp (the data connection is
 * initiated by the server) and IRC (DCC CHAT and
 * DCC SEND) for now
 *
 * NOTE: It's possible to crash SLiRP by sending it
 * unstandard strings to emulate... if this is a problem,
 * more checks are needed here
 *
 * XXX Assumes the whole command came in one packet
 *
 * XXX Some ftp clients will have their TOS set to
 * LOWDELAY and so Nagel will kick in.  Because of this,
 * we'll get the first letter, followed by the rest, so
 * we simply scan for ORT instead of PORT...
 * DCC doesn't have this problem because there's other stuff
 * in the packet before the DCC command.
 *
 * Return 1 if the mbuf m is still valid and should be
 * sbuf_append()ed
 *
 * NOTE: if you return 0 you MUST mbuf_free() the mbuf!
 */
int
tcp_emu(so, m)
	struct socket *so;
	MBuf m;
{
	u_int n1, n2, n3, n4, n5, n6;
	char buff[256];
	u_int32_t laddr;
	u_int lport;
	char *bptr;

	DEBUG_CALL("tcp_emu");
	DEBUG_ARG("so = %lx", (long)so);
	DEBUG_ARG("m = %lx", (long)m);

	switch(so->so_emu) {
		int x, i;

	 case EMU_IDENT:
		/*
		 * Identification protocol as per rfc-1413
		 */

		{
			struct socket *tmpso;
			struct sockaddr_in addr;
			int addrlen = sizeof(struct sockaddr_in);
			SBuf  so_rcv = &so->so_rcv;

			memcpy(so_rcv->sb_wptr, m->m_data, m->m_len);
			so_rcv->sb_wptr += m->m_len;
			so_rcv->sb_rptr += m->m_len;
			m->m_data[m->m_len] = 0; /* NULL terminate */
			if (strchr(m->m_data, '\r') || strchr(m->m_data, '\n')) {
				if (sscanf(so_rcv->sb_data, "%d%*[ ,]%d", &n1, &n2) == 2) {
					HTONS(n1);
					HTONS(n2);
					/* n2 is the one on our host */
					for (tmpso = tcb.so_next; tmpso != &tcb; tmpso = tmpso->so_next) {
						if (tmpso->so_laddr.s_addr == so->so_laddr.s_addr &&
						    tmpso->so_lport == n2 &&
						    tmpso->so_faddr.s_addr == so->so_faddr.s_addr &&
						    tmpso->so_fport == n1) {
							if (getsockname(tmpso->s,
								(struct sockaddr *)&addr, &addrlen) == 0)
							   n2 = ntohs(addr.sin_port);
							break;
						}
					}
				}
				so_rcv->sb_cc = sprintf(so_rcv->sb_data, "%d,%d\r\n", n1, n2);
				so_rcv->sb_rptr = so_rcv->sb_data;
				so_rcv->sb_wptr = so_rcv->sb_data + so_rcv->sb_cc;
			}
			mbuf_free(m);
			return 0;
		}

        case EMU_FTP: /* ftp */
		*(m->m_data+m->m_len) = 0; /* NULL terminate for strstr */
		if ((bptr = (char *)strstr(m->m_data, "ORT")) != NULL) {
			/*
			 * Need to emulate the PORT command
			 */
			x = sscanf(bptr, "ORT %d,%d,%d,%d,%d,%d\r\n%256[^\177]",
				   &n1, &n2, &n3, &n4, &n5, &n6, buff);
			if (x < 6)
			   return 1;

			laddr = htonl((n1 << 24) | (n2 << 16) | (n3 << 8) | (n4));
			lport = htons((n5 << 8) | (n6));

			if ((so = solisten(0, laddr, lport, SS_FACCEPTONCE)) == NULL)
			   return 1;

			n6 = ntohs(so->so_fport);

			n5 = (n6 >> 8) & 0xff;
			n6 &= 0xff;

			laddr = ntohl(so->so_faddr.s_addr);

			n1 = ((laddr >> 24) & 0xff);
			n2 = ((laddr >> 16) & 0xff);
			n3 = ((laddr >> 8)  & 0xff);
			n4 =  (laddr & 0xff);

			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr,"ORT %d,%d,%d,%d,%d,%d\r\n%s",
					    n1, n2, n3, n4, n5, n6, x==7?buff:"");
			return 1;
		} else if ((bptr = (char *)strstr(m->m_data, "27 Entering")) != NULL) {
			/*
			 * Need to emulate the PASV response
			 */
			x = sscanf(bptr, "27 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n%256[^\177]",
				   &n1, &n2, &n3, &n4, &n5, &n6, buff);
			if (x < 6)
			   return 1;

			laddr = htonl((n1 << 24) | (n2 << 16) | (n3 << 8) | (n4));
			lport = htons((n5 << 8) | (n6));

			if ((so = solisten(0, laddr, lport, SS_FACCEPTONCE)) == NULL)
			   return 1;

			n6 = ntohs(so->so_fport);

			n5 = (n6 >> 8) & 0xff;
			n6 &= 0xff;

			laddr = ntohl(so->so_faddr.s_addr);

			n1 = ((laddr >> 24) & 0xff);
			n2 = ((laddr >> 16) & 0xff);
			n3 = ((laddr >> 8)  & 0xff);
			n4 =  (laddr & 0xff);

			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr,"27 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n%s",
					    n1, n2, n3, n4, n5, n6, x==7?buff:"");

			return 1;
		}

		return 1;

	 case EMU_KSH:
		/*
		 * The kshell (Kerberos rsh) and shell services both pass
		 * a local port port number to carry signals to the server
		 * and stderr to the client.  It is passed at the beginning
		 * of the connection as a NUL-terminated decimal ASCII string.
		 */
		so->so_emu = 0;
		for (lport = 0, i = 0; i < m->m_len-1; ++i) {
			if (m->m_data[i] < '0' || m->m_data[i] > '9')
				return 1;       /* invalid number */
			lport *= 10;
			lport += m->m_data[i] - '0';
		}
		if (m->m_data[m->m_len-1] == '\0' && lport != 0 &&
		    (so = solisten(0, so->so_laddr.s_addr, htons(lport), SS_FACCEPTONCE)) != NULL)
			m->m_len = sprintf(m->m_data, "%d", ntohs(so->so_fport))+1;
		return 1;

	 case EMU_IRC:
		/*
		 * Need to emulate DCC CHAT, DCC SEND and DCC MOVE
		 */
		*(m->m_data+m->m_len) = 0; /* NULL terminate the string for strstr */
		if ((bptr = (char *)strstr(m->m_data, "DCC")) == NULL)
			 return 1;

		/* The %256s is for the broken mIRC */
		if (sscanf(bptr, "DCC CHAT %256s %u %u", buff, &laddr, &lport) == 3) {
			if ((so = solisten(0, htonl(laddr), htons(lport), SS_FACCEPTONCE)) == NULL)
				return 1;

			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr, "DCC CHAT chat %lu %u%c\n",
			     (unsigned long)ntohl(so->so_faddr.s_addr),
			     ntohs(so->so_fport), 1);
		} else if (sscanf(bptr, "DCC SEND %256s %u %u %u", buff, &laddr, &lport, &n1) == 4) {
			if ((so = solisten(0, htonl(laddr), htons(lport), SS_FACCEPTONCE)) == NULL)
				return 1;

			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr, "DCC SEND %s %lu %u %u%c\n",
			      buff, (unsigned long)ntohl(so->so_faddr.s_addr),
			      ntohs(so->so_fport), n1, 1);
		} else if (sscanf(bptr, "DCC MOVE %256s %u %u %u", buff, &laddr, &lport, &n1) == 4) {
			if ((so = solisten(0, htonl(laddr), htons(lport), SS_FACCEPTONCE)) == NULL)
				return 1;

			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr, "DCC MOVE %s %lu %u %u%c\n",
			      buff, (unsigned long)ntohl(so->so_faddr.s_addr),
			      ntohs(so->so_fport), n1, 1);
		}
		return 1;

	 case EMU_REALAUDIO:
                /*
		 * RealAudio emulation - JP. We must try to parse the incoming
		 * data and try to find the two characters that contain the
		 * port number. Then we redirect an udp port and replace the
		 * number with the real port we got.
		 *
		 * The 1.0 beta versions of the player are not supported
		 * any more.
		 *
		 * A typical packet for player version 1.0 (release version):
		 *
		 * 0000:50 4E 41 00 05
		 * 0000:00 01 00 02 1B D7 00 00 67 E6 6C DC 63 00 12 50 .....�..g�l�c..P
		 * 0010:4E 43 4C 49 45 4E 54 20 31 30 31 20 41 4C 50 48 NCLIENT 101 ALPH
		 * 0020:41 6C 00 00 52 00 17 72 61 66 69 6C 65 73 2F 76 Al..R..rafiles/v
		 * 0030:6F 61 2F 65 6E 67 6C 69 73 68 5F 2E 72 61 79 42 oa/english_.rayB
		 *
		 * Now the port number 0x1BD7 is found at offset 0x04 of the
		 * Now the port number 0x1BD7 is found at offset 0x04 of the
		 * second packet. This time we received five bytes first and
		 * then the rest. You never know how many bytes you get.
		 *
		 * A typical packet for player version 2.0 (beta):
		 *
		 * 0000:50 4E 41 00 06 00 02 00 00 00 01 00 02 1B C1 00 PNA...........�.
		 * 0010:00 67 75 78 F5 63 00 0A 57 69 6E 32 2E 30 2E 30 .gux�c..Win2.0.0
		 * 0020:2E 35 6C 00 00 52 00 1C 72 61 66 69 6C 65 73 2F .5l..R..rafiles/
		 * 0030:77 65 62 73 69 74 65 2F 32 30 72 65 6C 65 61 73 website/20releas
		 * 0040:65 2E 72 61 79 53 00 00 06 36 42                e.rayS...6B
		 *
		 * Port number 0x1BC1 is found at offset 0x0d.
		 *
		 * This is just a horrible switch statement. Variable ra tells
		 * us where we're going.
		 */

		bptr = m->m_data;
		while (bptr < m->m_data + m->m_len) {
			u_short p;
			static int ra = 0;
			char ra_tbl[4];

			ra_tbl[0] = 0x50;
			ra_tbl[1] = 0x4e;
			ra_tbl[2] = 0x41;
			ra_tbl[3] = 0;

			switch (ra) {
			 case 0:
			 case 2:
			 case 3:
				if (*bptr++ != ra_tbl[ra]) {
					ra = 0;
					continue;
				}
				break;

			 case 1:
				/*
				 * We may get 0x50 several times, ignore them
				 */
				if (*bptr == 0x50) {
					ra = 1;
					bptr++;
					continue;
				} else if (*bptr++ != ra_tbl[ra]) {
					ra = 0;
					continue;
				}
				break;

			 case 4:
				/*
				 * skip version number
				 */
				bptr++;
				break;

			 case 5:
				/*
				 * The difference between versions 1.0 and
				 * 2.0 is here. For future versions of
				 * the player this may need to be modified.
				 */
				if (*(bptr + 1) == 0x02)
				   bptr += 8;
				else
				   bptr += 4;
				break;

			 case 6:
				/* This is the field containing the port
				 * number that RA-player is listening to.
				 */
				lport = (((u_char*)bptr)[0] << 8)
				+ ((u_char *)bptr)[1];
				if (lport < 6970)
				   lport += 256;   /* don't know why */
				if (lport < 6970 || lport > 7170)
				   return 1;       /* failed */

				/* try to get udp port between 6970 - 7170 */
				for (p = 6970; p < 7071; p++) {
					if (udp_listen( htons(p),
						       so->so_laddr.s_addr,
						       htons(lport),
						       SS_FACCEPTONCE)) {
						break;
					}
				}
				if (p == 7071)
				   p = 0;
				*(u_char *)bptr++ = (p >> 8) & 0xff;
				*(u_char *)bptr++ = p & 0xff;
				ra = 0;
				return 1;   /* port redirected, we're done */
				break;

			 default:
				ra = 0;
			}
			ra++;
		}
		return 1;

	 default:
		/* Ooops, not emulated, won't call tcp_emu again */
		so->so_emu = 0;
		return 1;
	}
}

/*
 * Do misc. config of SLiRP while its running.
 * Return 0 if this connections is to be closed, 1 otherwise,
 * return 2 if this is a command-line connection
 */
int
tcp_ctl(so)
	struct socket *so;
{
	SBuf  sb = &so->so_snd;
	int command;
 	struct ex_list *ex_ptr;
	int do_pty;
        //	struct socket *tmpso;

	DEBUG_CALL("tcp_ctl");
	DEBUG_ARG("so = %lx", (long )so);

#if 0
	/*
	 * Check if they're authorised
	 */
	if (ctl_addr.s_addr && (ctl_addr.s_addr == -1 || (so->so_laddr.s_addr != ctl_addr.s_addr))) {
		sb->sb_cc = sprintf(sb->sb_wptr,"Error: Permission denied.\r\n");
		sb->sb_wptr += sb->sb_cc;
		return 0;
	}
#endif
	command = (ntohl(so->so_faddr.s_addr) & 0xff);

	switch(command) {
	default:
		/*
		 * Nothing bound..
		 */
		/* tcp_fconnect(so); */

	case CTL_ALIAS:
	  sb->sb_cc = sprintf(sb->sb_wptr,
			      "Error: No application configured.\r\n");
	  sb->sb_wptr += sb->sb_cc;
	  return(0);
	}
}
