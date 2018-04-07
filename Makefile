# append_only_filter Makefile

MODULE_big = append_only_filter
OBJS = append_only_filter.o $(WIN32RES)
PGFILEDESC = "filter statements meeting plan criteria - currently by the plan's target relation for UPDATE / DELETE
DOCS         = $(wildcard doc/*.md)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
