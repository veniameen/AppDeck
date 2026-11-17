// SPDX-License-Identifier: GPL-3.0-or-later
//go:build darwin

package main

/*
#include <libproc.h>
#include <sys/proc_info.h>
#include <stdint.h>
static uint64_t appdeck_birth(int pid) {
 struct proc_bsdinfo info = {0};
 if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) return 0;
 return (uint64_t)info.pbi_start_tvsec * 1000000ULL + (uint64_t)info.pbi_start_tvusec;
}
*/
import "C"

func processBirth(pid int) uint64 { return uint64(C.appdeck_birth(C.int(pid))) }
