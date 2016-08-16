MV UPDATER
----------

MV Updater (MVU) es un «background worker» de PostgreSQL, que maneja la
actualización asincrónica de vistas materializadas a pedido del usuario.

MODO DE USO
-----------

PARÁMETROS DE CONFIGURACIÓN
---------------------------

- *mv_updater.conninfo* (cadena de conexión a la base de datos)

  Se utiliza para indicar al servicio cómo conectarse a la base de datos.
  Por ejemplo: "dbname=pindonga user=pirulo". Ver (1).

    Default: NULL

- *mv_updater.schema* (esquema donde se encuentran las vistas materializadas)

    Default: public

- *mv_updater.channel* (canal de notificaciones)

    Default: mv_update

- *mv_updater.laptime* (tiempo (en segundos) entre cada ciclo)

    Default: 10

- *mv_updater.logdir* (directo para archivos de Log)

    Default: /var/log/mv_updater

REFERENCIAS
-----------

- http://www.postgresql.org/docs/current/static/bgworker.html
- http://www.postgresql.org/docs/current/static/libpq.html
- https://github.com/ibarwick/config_log/blob/master/config_log.c
- https://wiki.postgresql.org/wiki/What%27s_new_in_PostgreSQL_9.3#Custom_Background_Workers
- http://michael.otacoo.com/postgresql-2/postgres-9-3-feature-highlight-hello-world-with-custom-bgworkers
- https://github.com/michaelpq/pg_plugins/blob/master/receiver_raw/receiver_raw.c
- (1) http://www.postgresql.org/docs/9.3/static/libpq-connect.html#LIBPQ-CONNSTRING

COLABORADORES
-------------

- Abel M. Osorio < abel.m.osorio at gmail dot com >.
