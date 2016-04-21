EXTRA_DIST += \
	$(systemdsystemunit_DATA:=.in) \
	$(null)

CLEANFILES += \
	$(systemdsystemunit_DATA) \
	$(null)

$(systemdsystemunit_DATA): %: %.in $(CONFIG_HEADER) %D%/units.mk
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && \
		$(SED) \
		-e 's:[@]bindir[@]:$(bindir):g' \
		< $< > $@ || rm $@

