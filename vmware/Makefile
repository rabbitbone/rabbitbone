ROOT := $(abspath $(CURDIR)/..)

.PHONY: all clean test image live-iso legacy-image bootcheck ueficheck layoutcheck kernellayoutcheck rustsymbolscheck rustpaniccheck usercheck splitintegritycheck
all clean test image live-iso legacy-image bootcheck ueficheck layoutcheck kernellayoutcheck rustsymbolscheck rustpaniccheck usercheck splitintegritycheck:
	$(MAKE) -C $(ROOT) $@
