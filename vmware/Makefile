ROOT := $(abspath $(CURDIR)/..)

.PHONY: all clean test image bootcheck layoutcheck kernellayoutcheck rustsymbolscheck rustpaniccheck usercheck splitintegritycheck
all clean test image bootcheck layoutcheck kernellayoutcheck rustsymbolscheck rustpaniccheck usercheck splitintegritycheck:
	$(MAKE) -C $(ROOT) $@
