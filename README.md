#MV UPDATER

**MV Updater (MVU)** es un *[Background Worker](https://www.postgresql.org/docs/9.5/static/bgworker.html)* de **PostgreSQL**, que maneja la actualización asincrónica de *[Materialized Views](https://www.postgresql.org/docs/9.5/static/rules-materializedviews.html)* a pedido del usuario.

##MOTIVACIÓN
Cuando se maneja un gran volúmen de datos en conjunto con una gran cantidad de cálculos, la performance de ciertas consultas puede verse muy disminuida. En este punto, se empiezan a probar distintas alternativas para mejorar los cálculos, el manejo de los datos, etc. Existen distintas aproximaciones a distintos problemas/escenarios.

En particular, cuando lo que se desea es obtener un conjunto reducido de información, a partir de un gran volúmen de datos (que tranquilamente podrían incluir cálculos), una buena solución podría ser el uso de *Vistas Materializadas*. Sin embargo, el sólo hecho de crear estas vistas no soluciona totalmente el problema. Si alguno de los *datos base* cambia, debemos actualizar la vista materializada correspondiente. **PostgreSQL** permite esto a través del comando *REFRESH MATERIALIZED VIEW*. El problema aquí, es que nuestra consulta (INSERT, UPDATE, etc) deberá esperar a que termine la ejecución del comando REFRESH para poder continuar... y si la construcción de la vista demora 1, 2, 5, 10 minutos, el usuario deberá esperar (seguramente impaciente) para poder seguir con su trabajo. Esto claramente, puede causar problemas (además del enojo del usuario) en *timeouts* de *HTTP*, *deadblocks*, etc...

**MVU** evita esto mediante la utilización de notificaciones asincrónicas ([Asynchronous Notification](https://www.postgresql.org/docs/9.5/static/libpq-notify.html)), logrando abstraer a la operación de cálculos que se hagan posteriormente con los datos involucrados. De esta forma, se puede lograr un modelo donde se pueda mantener una vista (pesada) de datos calculados sincronizada, sin perjudicar operaciones elementales en el sistema.

###Un ejemplo para aclarar

Supongamos que tenemos una Vista Materializada *calculado* que, entre otras cosas, utiliza información de la tabla *origen*. Si algún dato de *origen* cambia, debemos notificar al **MVU** para que actualice, asincrónicamente, la vista materializada *calculado*. Esto lo podríamos hacer de muchas formas, pero como ejemplo, hagámoslo con *TRIGGERS*:

	CREATE OR REPLACE FUNCTION notify_mvu(mv text)
	  RETURNS TRIGGER
	  AS $$
	    BEGIN
	      EXECUTE 'NOTIFY mv_update, ''REFRESH ' || matview || '''';
	      IF TG_OP = 'DELETE' THEN
	        RETURN OLD;
	      ELSE
	        RETURN NEW;
	      END IF;
	    END
	  $$
	  LANGUAGE 'plpgsql';

	CREATE TRIGGER refresh_calculado
	  AFTER INSERT OR UPDATE OR DELETE
	  ON origen
	  FOR EACH STATEMENT
	  EXECUTE PROCEDURE notify_mvu('calculado');



##MODO DE USO

	 NOTIFY mv_update, '<COMMAND>';

Donde *COMMAND* puede ser:

- *REFRESH* mv:               Actualizar la vista materializada mv.
- *{START|STOP} IGNOREME*:  Se ignoran todos los comandos del *PID* que lo solicita, a excepción del comando '*STOP IGNOREME*'. Esto es útil para realizar operaciones que ejecutarían muchas solicitudes al MVU.
- *PING*: Reporte del estado del *bg worker*.

## REQUERIMIENTOS

  * PosgreSQL 9.3 o superior.
  * Paquete postgresql-server-dev-*POSTGRESQL_VERSION* (para poder ejecutar el '*make install*').

##INSTALACIÓN

### 1) Instalar *mv_updater*

 Ejecutando '*make install*' en el directorio raíz se instalarán los archivos necesarios para el servicio.

### 2) Configurar *shared_preload_libraries*

 En el archivo '*/etc/postgresql/POSTGRESQL_VERSION/CLUSTER_NAME/postgresql.conf*' agregar la cadena '*mv_updater*'. De esta forma se indica a **PostgreSQL** que inicie el BG Worker.

##PARÁMETROS DE CONFIGURACIÓN

Éstos se agregan en el archivo «postgresql.conf», en la sección *CUSTOMIZED OPTIONS*.

*mv_updater.conninfo* (cadena de conexión a la base de datos): Se utiliza para indicar al servicio cómo conectarse a la base de datos. Por ejemplo: '*dbname=pindonga user=pirulo*'. Ver (1). Default: *NULL*

*mv_updater.schema* (esquema donde se encuentran las vistas materializadas). Default: *public*

*mv_updater.channel* (canal de notificaciones): Default: *mv_update*

*mv_updater.laptime* (tiempo [en segundos] entre cada ciclo). Default: *10*

*mv_updater.logdir* (directo para archivos de Log). Default: */var/log/mv_updater*

##REFERENCIAS

http://www.postgresql.org/docs/current/static/bgworker.html
http://www.postgresql.org/docs/current/static/libpq.html
https://github.com/ibarwick/config_log/blob/master/config_log.c
https://wiki.postgresql.org/wiki/What%27s_new_in_PostgreSQL_9.3#Custom_Background_Workers
http://michael.otacoo.com/postgresql-2/postgres-9-3-feature-highlight-hello-world-with-custom-bgworkers
https://github.com/michaelpq/pg_plugins/blob/master/receiver_raw/receiver_raw.c
(1) http://www.postgresql.org/docs/9.3/static/libpq-connect.html#LIBPQ-CONNSTRING

##TODO

- Actualizar a la nueva sintaxis: https://www.postgresql.org/docs/9.5/static/sql-refreshmaterializedview.html.

##COLABORADORES

- Abel M. Osorio [abel.m.osorio at gmail dot com](mailto:abel.m.osorio@gmail.com).