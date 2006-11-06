/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2005-2006 David Gibson & Adam Litke, IBM Corporation.
 * Copyright (C) 2006 Hugh Dickins <hugh@veritas.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>

#include <hugetlbfs.h>

#include "hugetests.h"

/*
 * Test rationale:
 *
 * At one stage, a misconversion of hugetlb_vmtruncate_list to a
 * prio_tree meant that on 32-bit machines, truncates at or above 4GB
 * could truncate lower pages, resulting in BUG_ON()s.
 */
#define FOURGIG ((off64_t)0x100000000ULL)

static void sigbus_handler_fail(int signum, siginfo_t *si, void *uc)
{
	FAIL("Unexpected SIGBUS");
}

static void sigbus_handler_pass(int signum, siginfo_t *si, void *uc)
{
	PASS();
}

int main(int argc, char *argv[])
{
	int page_size;
	int hpage_size;
	long long buggy_offset;
	int fd;
	void *p, *q;
	volatile unsigned int *pi, *qi;
	int err;
	struct sigaction sa_fail = {
		.sa_sigaction = sigbus_handler_fail,
		.sa_flags = SA_SIGINFO,
	};
	struct sigaction sa_pass = {
		.sa_sigaction = sigbus_handler_pass,
		.sa_flags = SA_SIGINFO,
	};

	test_init(argc, argv);

	page_size = getpagesize();
	hpage_size = gethugepagesize();
	if (hpage_size < 0)
		CONFIG("No hugepage kernel support");

	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		FAIL("hugetlbfs_unlinked_fd()");

	/* First get arena of three hpages size, at file offset 4GB */
	q = mmap64(NULL, 3*hpage_size, PROT_READ|PROT_WRITE,
		 MAP_PRIVATE, fd, FOURGIG);
	if (q == MAP_FAILED)
		FAIL("mmap() offset 4GB");
	qi = q;
	/* Touch the high page */
	*qi = 0;

	/* This part of the test makes the problem more obvious, but
	 * is not essential.  It can't be done on powerpc, where
	 * segment restrictions prohibit us from performing such a
	 * mapping, so skip it there */
#if !defined(__powerpc__) && !defined(__powerpc64__)
	/* Replace middle hpage by tinypage mapping to trigger
	 * nr_ptes BUG */
	p = mmap64(q + hpage_size, hpage_size, PROT_READ|PROT_WRITE,
		   MAP_FIXED|MAP_PRIVATE|MAP_ANON, -1, 0);
	if (p != q + hpage_size)
		FAIL("mmap() before low hpage");
	pi = p;
	/* Touch one page to allocate its page table */
	*pi = 0;
#endif

	/* Replace top hpage by hpage mapping at confusing file offset */
	buggy_offset = FOURGIG / (hpage_size / page_size);
	p = mmap64(q + 2*hpage_size, hpage_size, PROT_READ|PROT_WRITE,
		 MAP_FIXED|MAP_PRIVATE, fd, buggy_offset);
	if (p != q + 2*hpage_size)
		FAIL("mmap() buggy offset 0x%llx", buggy_offset);
	pi = p;
	/* Touch the low page with something non-zero */
	*pi = 1;

	err = ftruncate64(fd, FOURGIG);
	if (err)
		FAIL("ftruncate(): %s", strerror(errno));

	err = sigaction(SIGBUS, &sa_fail, NULL);
	if (err)
		FAIL("sigaction() fail");

	if (*pi != 1)
		FAIL("Data 1 has changed to %u", *pi);

	err = sigaction(SIGBUS, &sa_pass, NULL);
	if (err)
		FAIL("sigaction() pass");

	*qi;

	/* Should have SIGBUSed above */
	FAIL("Didn't SIGBUS on truncated page.");
}