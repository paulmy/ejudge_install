/* -*- c -*- */
/* $Id$ */
/* Copyright (C) 2004 Alexander Chernov */

/* This file is derived from `linux/sockios.h' of the Linux Kernel.
   The original copyright follows. */

/*
 * INET         An implementation of the TCP/IP protocol suite for the LINUX
 *              operating system.  INET is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              Definitions of the socket-level I/O control calls.
 *
 * Version:     @(#)sockios.h   1.0.2   03/09/93
 *
 * Authors:     Ross Biro, <bir7@leland.Stanford.Edu>
 *              Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 */

#ifndef __RCC_LINUX_SOCKIOS_H__
#define __RCC_LINUX_SOCKIOS_H__

#include <sys/ioctl.h>

int enum
{
  FIOSETOWN = 0x8901,
#define FIOSETOWN FIOSETOWN
  SIOCSPGRP = 0x8902,
#define SIOCSPGRP SIOCSPGRP
  FIOGETOWN = 0x8903,
#define FIOGETOWN FIOGETOWN
  SIOCGPGRP = 0x8904,
#define SIOCGPGRP SIOCGPGRP
  SIOCATMARK = 0x8905,
#define SIOCATMARK SIOCATMARK
  SIOCGSTAMP = 0x8906,
#define SIOCGSTAMP SIOCGSTAMP
  SIOCINQ = FIONREAD,
#define SIOCINQ SIOCINQ
  SIOCOUTQ = TIOCOUTQ,
#define SIOCOUTQ SIOCOUTQ

#ifndef RCC_SIO_CONSTANTS
#define RCC_SIO_CONSTANTS  
  SIOCADDRT = 0x890B,
#define SIOCADDRT SIOCADDRT
  SIOCDELRT = 0x890C,
#define SIOCDELRT SIOCDELRT
  SIOCRTMSG = 0x890D,
#define SIOCRTMSG SIOCRTMSG
  SIOCGIFNAME = 0x8910,
#define SIOCGIFNAME SIOCGIFNAME
  SIOCSIFLINK = 0x8911,
#define SIOCSIFLINK SIOCSIFLINK
  SIOCGIFCONF = 0x8912,
#define SIOCGIFCONF SIOCGIFCONF
  SIOCGIFFLAGS = 0x8913,
#define SIOCGIFFLAGS SIOCGIFFLAGS
  SIOCSIFFLAGS = 0x8914,
#define SIOCSIFFLAGS SIOCSIFFLAGS
  SIOCGIFADDR = 0x8915,
#define SIOCGIFADDR SIOCGIFADDR
  SIOCSIFADDR = 0x8916,
#define SIOCSIFADDR SIOCSIFADDR
  SIOCGIFDSTADDR = 0x8917,
#define SIOCGIFDSTADDR SIOCGIFDSTADDR
  SIOCSIFDSTADDR = 0x8918,
#define SIOCSIFDSTADDR SIOCSIFDSTADDR
  SIOCGIFBRDADDR = 0x8919,
#define SIOCGIFBRDADDR SIOCGIFBRDADDR
  SIOCSIFBRDADDR = 0x891a,
#define SIOCSIFBRDADDR SIOCSIFBRDADDR
  SIOCGIFNETMASK = 0x891b,
#define SIOCGIFNETMASK SIOCGIFNETMASK
  SIOCSIFNETMASK = 0x891c,
#define SIOCSIFNETMASK SIOCSIFNETMASK
  SIOCGIFMETRIC = 0x891d,
#define SIOCGIFMETRIC SIOCGIFMETRIC
  SIOCSIFMETRIC = 0x891e,
#define SIOCSIFMETRIC SIOCSIFMETRIC
  SIOCGIFMEM = 0x891f,
#define SIOCGIFMEM SIOCGIFMEM
  SIOCSIFMEM = 0x8920,
#define SIOCSIFMEM SIOCSIFMEM
  SIOCGIFMTU = 0x8921,
#define SIOCGIFMTU SIOCGIFMTU
  SIOCSIFMTU = 0x8922,
#define SIOCSIFMTU SIOCSIFMTU
  SIOCSIFNAME = 0x8923,
#define SIOCSIFNAME SIOCSIFNAME
  SIOCSIFHWADDR = 0x8924,
#define SIOCSIFHWADDR SIOCSIFHWADDR
  SIOCGIFENCAP = 0x8925,
#define SIOCGIFENCAP SIOCGIFENCAP
  SIOCSIFENCAP = 0x8926,
#define SIOCSIFENCAP SIOCSIFENCAP
  SIOCGIFHWADDR = 0x8927,
#define SIOCGIFHWADDR SIOCGIFHWADDR
  SIOCGIFSLAVE = 0x8929,
#define SIOCGIFSLAVE SIOCGIFSLAVE
  SIOCSIFSLAVE = 0x8930,
#define SIOCSIFSLAVE SIOCSIFSLAVE
  SIOCADDMULTI = 0x8931,
#define SIOCADDMULTI SIOCADDMULTI
  SIOCDELMULTI = 0x8932,
#define SIOCDELMULTI SIOCDELMULTI
  SIOCGIFINDEX = 0x8933,
#define SIOCGIFINDEX SIOCGIFINDEX
  SIOGIFINDEX = SIOCGIFINDEX,
#define SIOGIFINDEX SIOGIFINDEX
  SIOCSIFPFLAGS = 0x8934,
#define SIOCSIFPFLAGS SIOCSIFPFLAGS
  SIOCGIFPFLAGS = 0x8935,
#define SIOCGIFPFLAGS SIOCGIFPFLAGS
  SIOCDIFADDR = 0x8936,
#define SIOCDIFADDR SIOCDIFADDR
  SIOCSIFHWBROADCAST = 0x8937,
#define SIOCSIFHWBROADCAST SIOCSIFHWBROADCAST
  SIOCGIFCOUNT = 0x8938,
#define SIOCGIFCOUNT SIOCGIFCOUNT
  SIOCGIFBR = 0x8940,
#define SIOCGIFBR SIOCGIFBR
  SIOCSIFBR = 0x8941,
#define SIOCSIFBR SIOCSIFBR
  SIOCGIFTXQLEN = 0x8942,
#define SIOCGIFTXQLEN SIOCGIFTXQLEN
  SIOCSIFTXQLEN = 0x8943,
#endif /* RCC_SIO_CONSTANTS is defined */

#define SIOCSIFTXQLEN SIOCSIFTXQLEN
  SIOCGIFDIVERT = 0x8944,
#define SIOCGIFDIVERT SIOCGIFDIVERT
  SIOCSIFDIVERT = 0x8945,
#define SIOCSIFDIVERT SIOCSIFDIVERT
  SIOCETHTOOL = 0x8946,
#define SIOCETHTOOL SIOCETHTOOL
  SIOCGMIIPHY = 0x8947,
#define SIOCGMIIPHY SIOCGMIIPHY
  SIOCGMIIREG = 0x8948,
#define SIOCGMIIREG SIOCGMIIREG
  SIOCSMIIREG = 0x8949,
#define SIOCSMIIREG SIOCSMIIREG

#ifndef RCC_SIO_CONSTANTS_1
#define RCC_SIO_CONSTANTS_1
  SIOCDARP = 0x8953,
#define SIOCDARP SIOCDARP
  SIOCGARP = 0x8954,
#define SIOCGARP SIOCGARP
  SIOCSARP = 0x8955,
#define SIOCSARP SIOCSARP
  SIOCDRARP = 0x8960,
#define SIOCDRARP SIOCDRARP
  SIOCGRARP = 0x8961,
#define SIOCGRARP SIOCGRARP
  SIOCSRARP = 0x8962,
#define SIOCSRARP SIOCSRARP
  SIOCGIFMAP = 0x8970,
#define SIOCGIFMAP SIOCGIFMAP
  SIOCSIFMAP = 0x8971,
#define SIOCSIFMAP SIOCSIFMAP
  SIOCADDDLCI = 0x8980,
#define SIOCADDDLCI SIOCADDDLCI
  SIOCDELDLCI = 0x8981,
#define SIOCDELDLCI SIOCDELDLCI
#endif /* RCC_SIO_CONSTANTS_1 is defined */

  SIOCGIFVLAN = 0x8982,
#define SIOCGIFVLAN SIOCGIFVLAN
  SIOCSIFVLAN = 0x8983,
#define SIOCSIFVLAN SIOCSIFVLAN
  SIOCBONDENSLAVE = 0x8990,
#define SIOCBONDENSLAVE SIOCBONDENSLAVE
  SIOCBONDRELEASE = 0x8991,
#define SIOCBONDRELEASE SIOCBONDRELEASE
  SIOCBONDSETHWADDR = 0x8992,
#define SIOCBONDSETHWADDR SIOCBONDSETHWADDR
  SIOCBONDSLAVEINFOQUERY = 0x8993,
#define SIOCBONDSLAVEINFOQUERY SIOCBONDSLAVEINFOQUERY
  SIOCBONDINFOQUERY = 0x8994,
#define SIOCBONDINFOQUERY SIOCBONDINFOQUERY
  SIOCBONDCHANGEACTIVE = 0x8995,
#define SIOCBONDCHANGEACTIVE SIOCBONDCHANGEACTIVE
  
  
#ifndef RCC_SIO_CONSTANTS_2
#define RCC_SIO_CONSTANTS_2
  SIOCDEVPRIVATE = 0x89F0,
#define SIOCDEVPRIVATE SIOCDEVPRIVATE
  SIOCPROTOPRIVATE = 0x89E0,
#define SIOCPROTOPRIVATE SIOCPROTOPRIVATE
#endif /* RCC_SIO_CONSTANTS_2 is defined */
};

#endif  /* __RCC_LINUX_SOCKIOS_H__ */
