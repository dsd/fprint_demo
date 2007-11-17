AUTOMAKE_OPTIONS = dist-bzip2
SUBDIRS = src
DISTCLEANFILES = ChangeLog

.PHONY: ChangeLog dist-up
ChangeLog:
	git --git-dir $(top_srcdir)/.git log > ChangeLog || touch ChangeLog

dist-hook: ChangeLog

dist-up: dist
	ncftpput upload.sourceforge.net incoming $(distdir).tar.bz2

