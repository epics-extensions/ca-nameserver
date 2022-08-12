# Channel Access Name Server Release Notes

## 2022/08/12 - 2.1.0

* Converted to build as a stand-alone EPICS module.
* Switching back to 3-component version numbers for this release (not semver).
* Fixed use-after-free bug.
* Removed obsolete `JS_FILEWAIT` and `PIOC` code.
* Added support for Perl Compatible Regular Expressions, using the
implementation from the ca-gateway module. Improved the configuration of
which regex engine to compile with, now in `configure/CONFIG_SITE`.

## Mon May 11 15:34:18 CST 2014 - 2.0.0.14

* Changed asHost library name to dbCore for R3.15 base.
* Changes for WIN32 build. Changed setpgrp to setpgid.
* Added change from Dr.Jeong Han Lee - changed function `basename()` to `basename_st()` to avoid conflict with `basename()` from string.h.

## Thu Apr  7 14:27:28 CDT 2011 - 2.0.0.13

* Changed `-d <n>` command line option to control log messaging level.
* The higher the more messages.  Removed `-v` verbose output mode.
* Changed `ca_pend_io()` delay value to .001.

## Tue Oct 27 16:07:18 CDT 2009 - 2.0.0.12

* Changed `void* hash_table;` to `gphPvt* hash_table;` in tsHash.h for build with R3.14.11.

## Wed Sep 20 09:06:34 CDT 2006 - 2.0.0.11

* Fixed `ifdef filewait` in directoryServer.c.
* Changed default to NO filewait by commenting out option in Makefile.

## Wed Feb  1 11:46:31 CST 2006 - 2.0.0.10

* Code modified to read all pvlist files before connecting heartbeat pvs.
* Functions to try to reserve a low number file descriptor for `fopen()`s sometimes lost the low (<256 on solaris) fd to CA/CAS socket connections.

## Mon Sep 19 13:27:29 CDT 2005

* Added delay before reading pvlist file after an ioc heartbeat reconnect.

## Thu Jun 30 13:49:36 CDT 2005

* Add non zero test for print of `home_dir`, `log_file`, `fileName`, `pvlist_file`.

## Mon Jun 27 11:18:39 CDT 2005 - 2.0.0.9

* Eliminate solaris warnings by putting `extern "C"` on sigusr1.
* Add delete of fileList in directoryServer destructor.
* main.cc: Removed unused definitons. Fixed clear channel test.
* Print pvnames in report only if verbose is true.
* Rename `get()` to `get_pvE()` for pIoc.
* Remove some const qualifiers.
* Allow PVname move from DOWN ioc to an UP ioc.

## Thu Jun 23 10:33:24 CDT 2005 - 2.0.0.6

* Use functions to reserve file descriptor for open/close of log file.
* Moved `processPendingList` and `processReconnectingIocs` code out of main().
* Use functions to reserve file descriptor for open/close of never.log.

## Fri Jun  3 12:03:47 CDT 2005 - 2.0.0.5

* Added logging timestamp to many messages. Added build time and version.

## Thu Jun  2 16:10:47 CDT 2005

* Added `-x` to usage line.  Let ca call `fdmgr_init()`.
* Allow pvname to move to a new ioc if old ioc is DOWN.

## Tue May 24 20:54:34 CDT 2005

* Set a ca connection handler on ALL learned pvs (may be gateway pvs).

## Thu May 19 10:04:16 CDT 2005

* Implemented `setDebugLevel()` and `generateBeaconAnomaly()`.
* Call `generateBeaconAnomaly()` after `add_all_pvs()` on ioc reboot.
* Added if verbose to fprintf stmnt which preceed setup_logging.

## Wed May 18 16:03:11 CDT 2005

* Try reading pvlist file 3 times before giving up to avoid getting "Stale NFS file handle" messages.

## Tue May 17 15:34:25 CDT 2005 - 1.3.1-asd4

* Added nameserver pvs - heartbeat, pvExistTest statistics (hit,broadcast, pending, broadcast_denied. ioc_down, ioc_error)

## Tue May 10 10:33:33 CDT 2005

* Print ca and cas environment variables to log file at startup.
* Add `static` to appropriate routines.
* Added pvPrefix to command line args (not used yet)

## Tue Oct  5 15:14:09 CDT 2004 - 1.3.1-asd3

* Remove all pvs and remove PH after learned host disconnects.
* Add monitored pv back into pending list.
* Added functions to reserve a file descriptor to use for fopen so maximum number of file descriptors will not be exceeded.
* Commented out printfs. Added basename,dirname,iocname for WIN32.
* Removed `getopt()` code - not portable to WIN32. Other WIN32 code changes.

## Fri Aug 20 16:31:23 CDT 2004

* Make nameserver portable.
* No server mode (watcher process) on WIN32.
* No kill and restart scripts on WIN32.
* Remove usage of `basename()` and `dirname()`.
* Remove usage of `getopt()`.
* Remove usage of `gettimeofday()` (use epicsTime).

## Mon Jul 30 17:00:00 CDT 2004

* Changes for multiple iocs on a single workstation.
* Added new class `pvEHost` for host specific linked list of `pvEs`. This linked list is used to delete all pvs for a host when it becomes disconnected and reconnected.
* Removed code for copy and read of private pvlist files.
* Modified `installHostName()` to return `pHost *`.
* Modified `pvE` class to contain `pHost *` instead of `pHostName` char string.
* Modified `filewait` class to contain `pHost *` instead of `pHostName` char string.
* Changed code to set `struct sockaddr_in ipa` values after channel connects not when `pHost` is created.

The `pHost` class now is an ioc class and hostname means iocname (not channel ca server name). Multiple ioc's can exist on a single ip host. The iocnames now may not have anything to do with the ip host name. The heartbeat pv has the name `<iocname>:heartbeat`. The iocname may be the ip host name in some cases.

If the search for an unknown pv connects and the ip host is not in the host hash table, use the "host:port" string to create a `pHost` class and add it to the host hash table.

## Fri Jul 23 09:30:31 CDT 2004 - 1.3.1-asd2

* Changes for R3.14 build
* Changes for `tsSLList` (`init` and `destroyAllEntries()`)
* Changes for cas server (create `pvExistTest(3parms)`)
* Changes for `tsSLIter`
* Changes for `osiTime`, `osiTimer` (changed to `epicsTime`, `epicsTimer`)
* Changes for ca (`ca_task_initialize()` to `ca_context_create()`)

## Thu Jul  3 12:46:48 CDT 2003 - 1.2

* Changes from previous version:
* Remove some logging prints
* Fix bug for no heartbeat in signal.list found by Janet Anderson
* Compile options for other labs

## Fri Nov 22 09:20:21 CST 2002 - caDirServ.20021001

* Now test hostname for both "." and ':'.
* Moved `ca_flush_io()`.
* Made some portablility changes.
* Added `-n` option: Input Signal list filename is iocname (else iocname is `basename(dirname(name))`)

