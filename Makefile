PLATFORM ?= ps4

.PHONY: all clean deps

ifeq ($(PLATFORM),ps4)
include Makefile.ps4
else
$(error Unsupported PLATFORM '$(PLATFORM)' for binldr; expected ps4)
endif
