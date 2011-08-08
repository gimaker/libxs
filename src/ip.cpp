/*
    Copyright (c) 2007-2011 iMatix Corporation
    Copyright (c) 2007-2011 Other contributors as noted in the AUTHORS file

    This file is part of 0MQ.

    0MQ is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    0MQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <string>

#include "ip.hpp"
#include "err.hpp"
#include "platform.hpp"
#include "stdint.hpp"

#if !defined ZMQ_HAVE_WINDOWS && !defined ZMQ_HAVE_OPENVMS
#include <sys/un.h>
#endif

#if !defined ZMQ_HAVE_WINDOWS
#include <fcntl.h>
#endif

#if defined ZMQ_HAVE_OPENVMS
#include <ioctl.h>
#endif

#if defined ZMQ_HAVE_SOLARIS

#include <sys/sockio.h>
#include <net/if.h>
#include <unistd.h>

//  On Solaris platform, network interface name can be queried by ioctl.
static int resolve_nic_name (struct sockaddr* addr_, char const *interface_,
    bool ipv4only_)
{
    //  TODO: Unused parameter, IPv6 support not implemented for Solaris.
    (void) ipv4only;

    //  Create a socket.
    int fd = socket (AF_INET, SOCK_DGRAM, 0);
    zmq_assert (fd != -1);

    //  Retrieve number of interfaces.
    lifnum ifn;
    ifn.lifn_family = AF_INET;
    ifn.lifn_flags = 0;
    int rc = ioctl (fd, SIOCGLIFNUM, (char*) &ifn);
    zmq_assert (rc != -1);

    //  Allocate memory to get interface names.
    size_t ifr_size = sizeof (struct lifreq) * ifn.lifn_count;
    char *ifr = (char*) malloc (ifr_size);
    alloc_assert (ifr);
    
    //  Retrieve interface names.
    lifconf ifc;
    ifc.lifc_family = AF_INET;
    ifc.lifc_flags = 0;
    ifc.lifc_len = ifr_size;
    ifc.lifc_buf = ifr;
    rc = ioctl (fd, SIOCGLIFCONF, (char*) &ifc);
    zmq_assert (rc != -1);

    //  Find the interface with the specified name and AF_INET family.
    bool found = false;
    lifreq *ifrp = ifc.lifc_req;
    for (int n = 0; n < (int) (ifc.lifc_len / sizeof (lifreq));
          n ++, ifrp ++) {
        if (!strcmp (interface_, ifrp->lifr_name)) {
            rc = ioctl (fd, SIOCGLIFADDR, (char*) ifrp);
            zmq_assert (rc != -1);
            if (ifrp->lifr_addr.ss_family == AF_INET) {
                *addr_ = ((sockaddr_in*) &ifrp->lifr_addr)->sin_addr;
                found = true;
                break;
            }
        }
    }

    //  Clean-up.
    free (ifr);
    close (fd);

    if (!found) {
        errno = ENODEV;
        return -1;
    }

    return 0;
}

#elif defined ZMQ_HAVE_AIX || ZMQ_HAVE_HPUX

#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>

static int resolve_nic_name (struct sockaddr* addr_, char const *interface_,
    bool ipv4only_)
{
    //  TODO: Unused parameter, IPv6 support not implemented for AIX or HP/UX.
    (void) ipv4only;

    //  Create a socket.
    int sd = socket (AF_INET, SOCK_DGRAM, 0);
    zmq_assert (sd != -1);

    struct ifreq ifr; 

    //  Copy interface name for ioctl get.
    strncpy (ifr.ifr_name, interface_, sizeof (ifr.ifr_name));

    //  Fetch interface address.
    int rc = ioctl (sd, SIOCGIFADDR, (caddr_t) &ifr, sizeof (struct ifreq));

    //  Clean up.
    close (sd);

    if (rc == -1) {
        errno = ENODEV;
        return -1;
    }

    struct sockaddr *sa = (struct sockaddr *) &ifr.ifr_addr;
    *addr_ = ((sockaddr_in*)sa)->sin_addr;
    return 0;    
}

#elif ((defined ZMQ_HAVE_LINUX || defined ZMQ_HAVE_FREEBSD ||\
    defined ZMQ_HAVE_OSX || defined ZMQ_HAVE_OPENBSD ||\
    defined ZMQ_HAVE_QNXNTO || defined ZMQ_HAVE_NETBSD)\
    && defined ZMQ_HAVE_IFADDRS)

#include <ifaddrs.h>

//  On these platforms, network interface name can be queried
//  using getifaddrs function.
static int resolve_nic_name (struct sockaddr* addr_, char const *interface_,
    bool ipv4only_)
{
    //  Get the addresses.
    ifaddrs* ifa = NULL;
    int rc = getifaddrs (&ifa);
    zmq_assert (rc == 0);    
    zmq_assert (ifa != NULL);

    //  Find the corresponding network interface.
    bool found = false;
    for (ifaddrs *ifp = ifa; ifp != NULL ;ifp = ifp->ifa_next)
    {
        if (ifp->ifa_addr == NULL)
            continue;

        int family = ifp->ifa_addr->sa_family;

        if ((family == AF_INET
             || (!ipv4only_ && family == AF_INET6))
            && !strcmp (interface_, ifp->ifa_name))
        {
            memcpy (addr_, ifp->ifa_addr,
                    (family == AF_INET) ? sizeof (struct sockaddr_in)
                                        : sizeof (struct sockaddr_in6));
            found = true;
            break;
        }
    }

    //  Clean-up;
    freeifaddrs (ifa);

    if (!found) {
        errno = ENODEV;
        return -1;
    }

    return 0;
}

#else

//  On other platforms we assume there are no sane interface names.
//  This is true especially of Windows.
static int resolve_nic_name (struct sockaddr* addr_, char const *interface_,
    bool ipv4only_)
{
    //  All unused parameters.
    (void) addr_;
    (void) interface_;
    (void) ipv4only_;

    errno = ENODEV;
    return -1;
}

#endif

int zmq::resolve_ip_interface (sockaddr_storage* addr_, socklen_t *addr_len_,
    char const *interface_, bool ipv4only_)
{
    //  Find the ':' at end that separates NIC name from service.
    const char *delimiter = strrchr (interface_, ':');
    if (!delimiter) {
        errno = EINVAL;
        return -1;
    }

    //  Separate the name/port.
    std::string iface (interface_, delimiter - interface_);
    std::string service (delimiter + 1);

    //  0 is not a valid port.
    uint16_t sin_port = htons ((uint16_t) atoi (service.c_str()));
    if (!sin_port) {
        errno = EINVAL;
        return -1;
    }

    //  Initialize the output parameter.
    memset (addr_, 0, sizeof (*addr_));

    //  Initialize temporary output pointers with storage address.
    sockaddr_storage ss;
    sockaddr *out_addr = (sockaddr *) &ss;
    socklen_t out_addrlen;

    //  Initialise IP-format family/port and populate temporary output pointers
    //  with the address.
    if (ipv4only_) {
        sockaddr_in ip4_addr;
        memset (&ip4_addr, 0, sizeof (ip4_addr));
        ip4_addr.sin_family = AF_INET;
        ip4_addr.sin_port = sin_port;
        ip4_addr.sin_addr.s_addr = htonl (INADDR_ANY);
        out_addrlen = (socklen_t) sizeof (ip4_addr);
        memcpy (out_addr, &ip4_addr, out_addrlen);
    } else {
        sockaddr_in6 ip6_addr;
        memset (&ip6_addr, 0, sizeof (ip6_addr));
        ip6_addr.sin6_family = AF_INET6;
        ip6_addr.sin6_port = sin_port;
        memcpy (&ip6_addr.sin6_addr, &in6addr_any, sizeof (in6addr_any));
        out_addrlen = (socklen_t) sizeof (ip6_addr);
        memcpy (out_addr, &ip6_addr, out_addrlen);
    }

    //  * resolves to INADDR_ANY or in6addr_any.
    if (iface.compare("*") == 0) {
        zmq_assert (out_addrlen <= (socklen_t) sizeof (*addr_));
        memcpy (addr_, out_addr, out_addrlen);
        *addr_len_ = out_addrlen;
        return 0;
    }

    //  Try to resolve the string as a NIC name.
    int rc = resolve_nic_name (out_addr, iface.c_str(), ipv4only_);
    if (rc != 0 && errno != ENODEV)
        return rc;
    if (rc == 0) {
        zmq_assert (out_addrlen <= (socklen_t) sizeof (*addr_));
        memcpy (addr_, out_addr, out_addrlen);
        *addr_len_ = out_addrlen;
        return 0;
    }

    //  There's no such interface name. Assume literal address.
#if defined ZMQ_HAVE_OPENVMS && defined __ia64
    __addrinfo64 *res = NULL;
    __addrinfo64 req;
#else
    addrinfo *res = NULL;
    addrinfo req;
#endif
    memset (&req, 0, sizeof (req));

    //  Choose IPv4 or IPv6 protocol family. Note that IPv6 allows for
    //  IPv4-in-IPv6 addresses.
    req.ai_family = ipv4only_ ? AF_INET : AF_INET6;

    //  Arbitrary, not used in the output, but avoids duplicate results.
    req.ai_socktype = SOCK_STREAM;

    //  Restrict hostname/service to literals to avoid any DNS lookups or
    //  service-name irregularity due to indeterminate socktype.
    req.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;

#ifndef ZMQ_HAVE_WINDOWS
    //  Windows by default maps IPv4 addresses into IPv6. In this API we only
    //  require IPv4-mapped addresses when no native IPv6 interfaces are
    //  available (~AI_ALL).  This saves an additional DNS roundtrip for IPv4
    //  addresses.
    if (req.ai_family == AF_INET6)
        req.ai_flags |= AI_V4MAPPED;
#endif

    //  Resolve the literal address. Some of the error info is lost in case
    //  of error, however, there's no way to report EAI errors via errno.
    rc = getaddrinfo (iface.c_str(), service.c_str(), &req, &res);
    if (rc) {
        errno = ENODEV;
        return -1;
    }

    //  Use the first result.
    zmq_assert ((size_t) (res->ai_addrlen) <= sizeof (*addr_));
    memcpy (addr_, res->ai_addr, res->ai_addrlen);
    *addr_len_ = (socklen_t) res->ai_addrlen;

    //  Cleanup getaddrinfo after copying the possibly referenced result.
    if (res)
        freeaddrinfo (res);

    return 0;
}

int zmq::resolve_ip_hostname (sockaddr_storage *addr_, socklen_t *addr_len_,
    const char *hostname_, bool ipv4only_)
{
    //  Find the ':' that separates hostname name from service.
    const char *delimiter = strrchr (hostname_, ':');
    if (!delimiter) {
        errno = EINVAL;
        return -1;
    }

    //  Separate the hostname and service.
    std::string hostname (hostname_, delimiter - hostname_);
    std::string service (delimiter + 1);

    //  Set up the query.
    addrinfo req;
    memset (&req, 0, sizeof (req));

    //  Choose IPv4 or IPv6 protocol family. Note that IPv6 allows for
    //  IPv4-in-IPv6 addresses.
    req.ai_family = ipv4only_ ? AF_INET : AF_INET6;

    //  Need to choose one to avoid duplicate results from getaddrinfo() - this
    //  doesn't really matter, since it's not included in the addr-output.
    req.ai_socktype = SOCK_STREAM;
    
    //  Avoid named services due to unclear socktype.
    req.ai_flags = AI_NUMERICSERV;

#ifndef ZMQ_HAVE_WINDOWS
    //  Windows by default maps IPv4 addresses into IPv6. In this API we only
    //  require IPv4-mapped addresses when no native IPv6 interfaces are
    //  available.  This saves an additional DNS roundtrip for IPv4 addresses.
    if (req.ai_family == AF_INET6)
        req.ai_flags |= AI_V4MAPPED;
#endif

    //  Resolve host name. Some of the error info is lost in case of error,
    //  however, there's no way to report EAI errors via errno.
    addrinfo *res;
    int rc = getaddrinfo (hostname.c_str (), service.c_str (), &req, &res);
    if (rc) {

        switch (rc) {
        case EAI_MEMORY:
            errno = ENOMEM;
            break;
        default:
        errno = EINVAL;
            break;
        }

        return -1;
    }

    //  Copy first result to output addr with hostname and service.
    zmq_assert ((size_t) (res->ai_addrlen) <= sizeof (*addr_));
    memcpy (addr_, res->ai_addr, res->ai_addrlen);
    *addr_len_ = (socklen_t) res->ai_addrlen;
 
    freeaddrinfo (res);
    
    return 0;
}

int zmq::resolve_local_path (sockaddr_storage *addr_, socklen_t *addr_len_,
    const char *path_)
{
#if defined ZMQ_HAVE_WINDOWS || defined ZMQ_HAVE_OPENVMS
    errno = EPROTONOSUPPORT;
    return -1;
#else
    sockaddr_un *un = (sockaddr_un*) addr_;
    if (strlen (path_) >= sizeof (un->sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy (un->sun_path, path_);
    un->sun_family = AF_UNIX;
    *addr_len_ = sizeof (sockaddr_un);
    return 0;
#endif
}

void zmq::tune_tcp_socket (fd_t s_)
{
    //  Disable Nagle's algorithm. We are doing data batching on 0MQ level,
    //  so using Nagle wouldn't improve throughput in anyway, but it would
    //  hurt latency.
    int nodelay = 1;
    int rc = setsockopt (s_, IPPROTO_TCP, TCP_NODELAY, (char*) &nodelay,
        sizeof (int));
#ifdef ZMQ_HAVE_WINDOWS
    wsa_assert (rc != SOCKET_ERROR);
#else
    errno_assert (rc == 0);
#endif

#ifdef ZMQ_HAVE_OPENVMS
    //  Disable delayed acknowledgements as they hurt latency is serious manner.
    int nodelack = 1;
    rc = setsockopt (s_, IPPROTO_TCP, TCP_NODELACK, (char*) &nodelack,
        sizeof (int));
    errno_assert (rc != SOCKET_ERROR);
#endif
}

void zmq::unblock_socket (fd_t s_)
{
#ifdef ZMQ_HAVE_WINDOWS
    u_long nonblock = 1;
    int rc = ioctlsocket (s_, FIONBIO, &nonblock);
    wsa_assert (rc != SOCKET_ERROR);
#elif ZMQ_HAVE_OPENVMS
	int nonblock = 1;
	int rc = ioctl (s_, FIONBIO, &nonblock);
    errno_assert (rc != -1);
#else
	int flags = fcntl (s_, F_GETFL, 0);
	if (flags == -1)
        flags = 0;
	int rc = fcntl (s_, F_SETFL, flags | O_NONBLOCK);
    errno_assert (rc != -1);
#endif
}


