EXTRA_DIST += \
	$(systemduserunit_DATA:=.in) \
	$(null)

CLEANFILES += \
	$(systemduserunit_DATA) \
	$(null)

$(systemduserunit_DATA): %: %.in $(CONFIG_HEADER) %D%/units.mk
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && \
		$(SED) \
		-e 's:[@]bindir[@]:$(bindir):g' \
		< $< > $@ || rm $@

