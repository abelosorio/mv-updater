MODULE_big = mv_updater
OBJS = mv_updater.o
PG_CPPFLAGS += -I/usr/include/postgresql
SHLIB_LINK = /usr/local/lib/libpq.so
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
