
# Install the English man file and include it when `make dist` is used
dist_man_MANS = modemu2k.1

LINGUAS = pt_BR it_IT

# the other man pages need to be added. This doesn't work in for this case yet
#
# EXTRA_DIST = $(LINGUAS)

install-data-local:
		for lang in $(LINGUAS); do \
			file=$(srcdir)/$(PACKAGE_NAME).$$lang.1; \
			dest=$(DESTDIR)$(mandir)/$$lang/man1; \
			dest=`echo $$dest | sed '$(program_transform_name)'`; \
			$(mkinstalldirs) $$dest; \
			$(INSTALL_DATA) $$file $$dest/$(PACKAGE_NAME).1; \
		done;

uninstall-local:
	for lang in $(LINGUAS); do \
		dest=$(DESTDIR)$(mandir)/$$lang/man1/$(PACKAGE).1; \
		dest=`echo $$dest | sed '$(program_transform_name)'`; \
		if test -f $$dest; then \
			rm -f $$dest; \
		fi \
	done;
