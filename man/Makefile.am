
# Install the English man file and include it when `make dist` is used
dist_man_MANS = modemu2k.1

# used by the install target
LINGUAS = pt_BR it_IT

# This will include the man files when `make dist` is run.
EXTRA_DIST = \
	modemu2k.pt_BR.1 \
	modemu2k.it_IT.1

# install translated man pages when `make install` is run.
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
