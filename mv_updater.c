/* $Id: mv_updater.c,v 1.20 2015/06/30 15:39:43 aosorio Exp $
 *
 * @file
 * Background worker para actualización asincrónica de vistas materializadas.
 * Diseñado para PostgreSQL 9.4.
 *
 * Modo de uso (en PostgreSQL):
 * --------------------------
 *
 * NOTIFY mv_update, <COMMAND>;
 *
 * COMMAND:
 *
 *   REFRESH <mv>		Actualizar la vista materializada <mv>.
 *   {START|STOP} IGNOREME	Se ignoran todos los comandos del PID
 *				que lo solicita, a excepción del comando
 *				'STOP IGNOREME'. Esto es útil para realizar
 *				operaciones que ejecutarían muchas solicitudes
 *				al MV Updater.
 *   PING			Reporte del estado del bg worker.
 */

#include <mv_updater.h>

/* Necesario para librerías cargadas a través de shared_preload_libraries */
PG_MODULE_MAGIC;

static void
myLog(char *logText, ...)
//{{{
{
  va_list list;
  FILE *logFile;
  char timeChar[70];
  time_t currentTime;
  char fileName[500] = "";
  char *p, *r;
  int e;

  strcat(fileName, mv_updater_logdir);
  strcat(fileName, "/mv_updater.log");

  if ((logFile = fopen(fileName, "a+")))
  {
    currentTime = time(NULL);
    strftime(timeChar, sizeof(timeChar), "%F %T %Z", localtime(&currentTime));

    fprintf(logFile, "%s;", timeChar);

    va_start(list, logText);
    for (p = logText; *p; ++p)
    {
      if (*p != '%')
      {
        fputc(*p, logFile);
      }
      else
      {
        switch (*++p)
        {
          /* string */
          case 's':
          {
            r = va_arg(list, char *);

            fprintf(logFile, "%s", r);
            continue;
          }

          /* integer */
          case 'd':
          {
            e = va_arg(list, int);

            fprintf(logFile, "%d", e);
            continue;
          }

          default:
            fputc(*p, logFile);
        }
      }
    }
    va_end(list);
    fprintf(logFile, "\n");
    fclose(logFile);
  }
  else
  {
    elog(WARNING,
      "El archivo de log «%s» no existe o no se puede escribir en el directorio.",
      fileName);
    elog(LOG, "%s", logText);
  }
}
//}}}

static void
exit_nicely(PGconn *conn)
//{{{
{
  SPI_finish();
  PQfinish(conn);
  proc_exit(0);
}
//}}}

static bool
match(const char *string, char *pattern)
//{{{
{
  int status;
  regex_t re;

  if (regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB) != 0) return false;

  status = regexec(&re, string, (size_t) 0, NULL, 0);
  regfree(&re);

  if (status != 0) return false;

  return true;
}
//}}}

static bool
is_pid_ignored(IgnoredPID *conductor, int pid)
//{{{
{
  if (conductor == NULL) return false;
  if (conductor->pid == pid) return true;

  return is_pid_ignored(conductor->next, pid);
}
//}}}

static bool
its_an_old_ignored_pid(IgnoredPID *ignored_pid)
//{{{
{
  double diff;
  time_t current_time;

  time(&current_time);
  diff = (double) current_time - (double) ignored_pid->start_time;

  return (diff > MAX_IGNORED_TIME);
}
//}}}

static IgnoredPID *
delete_ignored_pid_node(IgnoredPID *node)
//{{{
{
  IgnoredPID *auxiliar;

  auxiliar = node->next;

  free(node);
  return auxiliar;
}
//}}}

static IgnoredPID *
purge_ignored_pids(IgnoredPID *conductor)
//{{{
{
  if (conductor == NULL) return NULL;

  if (its_an_old_ignored_pid(conductor))
  {
    myLog("Removing ignored PID «%d», since it's oldest than «%d» seconds.",
      conductor->pid, MAX_IGNORED_TIME);

    return delete_ignored_pid_node(conductor);
  }

  conductor->next = purge_ignored_pids(conductor->next);

  return conductor;
}
//}}}

static IgnoredPID *
create_ignored_pid_node(int pid)
//{{{
{
  IgnoredPID *node;

  node = malloc(sizeof(IgnoredPID));

  if (node == NULL)
  {
    myLog("Can't create new node. Out of memory!");
    return NULL;
  }

  node->pid = pid;
  node->start_time = time(NULL);
  node->next = NULL;

  return node;
}
//}}}

static IgnoredPID *
add_pid_ignored(IgnoredPID *conductor, int pid)
//{{{
{
  if (conductor == NULL)
  {
    conductor = malloc(sizeof(IgnoredPID));

    if (conductor == NULL)
    {
      myLog("Can't create new node. Out of memory!");
      return NULL;
    }

    conductor->pid = pid;
    conductor->start_time = time(NULL);
    conductor->next = NULL;

    return conductor;
  }
  else
  {
    conductor->next = add_pid_ignored(conductor->next, pid);
    return conductor;
  }
}
//}}}

static IgnoredPID *
delete_pid_ignored(IgnoredPID *conductor, int pid)
//{{{
{
  if (conductor == NULL) return NULL;

  if (conductor->pid == pid)
  {
    myLog("PID «%d» will no longer be ignored.", conductor->pid);
    return delete_ignored_pid_node(conductor);
  }

  conductor->next = delete_pid_ignored(conductor->next, pid);
  return conductor;
}
//}}}

static void
start_ignore(PGnotify *notify)
//{{{
{
  IgnoredPID *result;

  // Se purga la lista de PIDs ignorados.
  ignored_pids = purge_ignored_pids(ignored_pids);

  if (is_pid_ignored(ignored_pids, notify->be_pid))
  {
    myLog("PID «%d» previously ignored.", notify->be_pid);
    return;
  }

  if (ignored_pids == NULL)
  {
    ignored_pids = create_ignored_pid_node(notify->be_pid);
    result = ignored_pids;
  }
  else
  {
    result = add_pid_ignored(ignored_pids, notify->be_pid);
  }

  if (result == NULL)
  {
    myLog("Can't ignore PID «%d».", notify->be_pid);
  }
  else
  {
    myLog("RefreshRequests from PID «%d» will be ignored.", notify->be_pid);
  }
}
//}}}

static void
stop_ignore(PGnotify *notify)
//{{{
{
  ignored_pids = delete_pid_ignored(ignored_pids, notify->be_pid);
}
//}}}

static void
ping()
//{{{
{
  int counter = 0;
  IgnoredPID *conductor;

  myLog("MVU status:");
  myLog("-----------");
  myLog("State: Listening channel «%s»", mv_updater_channel);
  myLog("Ignored PIDs:");

  conductor = ignored_pids;

  while (conductor != NULL)
  {
    if ( conductor->pid > 0)
    {
      myLog("---» PID: «%d»", conductor->pid);
      counter++;
    }
    conductor = conductor->next;
  }

  myLog("---» TOTAL: «%d»", counter);
  myLog("Hola, ping, ping... sí, soy yo. Root.");
}
//}}}

static RefreshRequest *
create_refresh_request_node(char *mv_name)
//{{{
{
  RefreshRequest *node;

  node = malloc(sizeof(RefreshRequest));

  if (node == NULL)
  {
    myLog("Can't create new node. Out of memory!");
    return NULL;
  }

  node->mv_name = malloc(strlen(mv_name) + 1);
  memcpy(node->mv_name, mv_name, strlen(mv_name) + 1);
  node->next = NULL;

  return node;
}
//}}}

static void
add_refresh_request(RefreshRequest *conductor, char *mv_name)
//{{{
{
  if (strcmp(conductor->mv_name, mv_name) == 0)
  {
    myLog("Request «%s» ignored since was received before", mv_name);
    return;
  }

  if (conductor->next == NULL)
  {
    conductor->next = create_refresh_request_node(mv_name);
  }
  else
  {
    add_refresh_request(conductor->next, mv_name);
  }
}
//}}}

static void
save_refresh_request(PGnotify *notify)
//{{{
{
  char *request = notify->extra;
  char *mv_name;
  regex_t regexp;
  regmatch_t match[2];
  int start, finish;

  if (request == NULL)
  {
    myLog("Notify ignored since it's empty");
    return;
  }

  if (is_pid_ignored(ignored_pids, notify->be_pid))
  {
    myLog("All requests from PID «%d» are ignored");
    return;
  }

  regcomp(&regexp,
    "^[[:space:]]*REFRESH[[:space:]]+([^[:space:]]+)[[:space:]]*$",
    REG_EXTENDED);

  if (regexec(&regexp, request, (size_t) 2, match, 0) != 0)
  {
    myLog("Invalid request");
    return;
  }

  start = match[1].rm_so;
  finish = match[1].rm_eo;

  mv_name = (char *) malloc(finish - start);
  // note: Todavía no entiendo porqué hay que sumar 1...
  strncpy(mv_name, request + start, finish - start + 1);

  if (refresh_requests == NULL)
  {
    refresh_requests = create_refresh_request_node(mv_name);
  }
  else
  {
    add_refresh_request(refresh_requests, mv_name);
  }

  free(mv_name);

  return;
}
//}}}

static void
process_notification(PGnotify *notify)
//{{{
{
  char *notification = notify->extra;

  if (match(notification,
        "^[[:space:]]*REFRESH[[:space:]]+[^[:space:]]+[[:space:]]*$"))
  {
    save_refresh_request(notify);
    return;
  }

  if (match(notification,
        "^[[:space:]]*START[[:space:]]+IGNOREME[[:space:]]*$"))
  {
    start_ignore(notify);
    return;
  }

  if (match(notification, "^[[:space:]]*STOP[[:space:]]+IGNOREME[[:space:]]*$"))
  {
    stop_ignore(notify);
    return;
  }

  if (match(notification, "^[[:space:]]*PING[[:space:]]*$"))
  {
    ping();
    return;
  }

  myLog("Invalid command «%s». Ignored.", notification);
}
//}}}

static void
refresh_matview(char *mv_name)
//{{{
{
  PGresult *res;
  char command[500] = "SELECT ";

  myLog("Refreshing materialized view «%s» by notification", mv_name);

  /* Comando: SELECT <schema>.refresh_matview('<schema>.<mv_name>'); */
  strcat(command, mv_updater_schema);
  strcat(command, ".refresh_matview('");
  strcat(command, mv_updater_schema);
  strcat(command, ".");
  strcat(command, mv_name);
  strcat(command, "');");

  res = PQexec(conn, command);

  if (PQresultStatus(res) != PGRES_TUPLES_OK)
  {
    myLog("REFRESH failed: «%s»", PQerrorMessage(conn));
  }
  else
  {
    myLog("REFRESH OK");
  }

  PQclear(res);
}
//}}}

static void
process_refresh_requests(RefreshRequest *conductor)
//{{{
{
  if (conductor != NULL)
  {
    refresh_matview(conductor->mv_name);
    if (conductor->next != NULL)
    {
      process_refresh_requests(conductor->next);
    }
  }
}
//}}}

static void
clean_refresh_requests(RefreshRequest *conductor)
//{{{
{
  if (conductor != NULL)
  {
    if (conductor->next != NULL)
    {
      clean_refresh_requests(conductor->next);
      conductor->next = NULL;
    }
    free(conductor);
  }
}
//}}}

static void
mv_updater_main(Datum arg)
//{{{
{
  PGresult *res;
  PGnotify *notify;
  char command[100] = "LISTEN ";
  int sock;
  fd_set input_mask;

  /* Conexión a la base de datos */
  if (mv_updater_conninfo == NULL)
  {
    elog(WARNING, "Connecting to database with default values");
  }

  conn = PQconnectdb(mv_updater_conninfo);

  if (PQstatus(conn) != CONNECTION_OK)
  {
    elog(ERROR, "Connection to database failed: «%s»", PQerrorMessage(conn));
    exit_nicely(conn);
  }
  else
  {
    elog(LOG, "Successful connection to database with string connection «%s».",
      mv_updater_conninfo);
  }

  /* Inicio de recepción de señales por el worker */
  BackgroundWorkerUnblockSignals();

  /* Inicio de recepción de notificaciones */
  strcat(command, mv_updater_channel);
  res = PQexec(conn, command);

  if (PQresultStatus(res) != PGRES_COMMAND_OK)
  {
    elog(ERROR, "LISTEN command failed: «%s»", PQerrorMessage(conn));
    PQclear(res);
    exit_nicely(conn);
  }
  else
  {
    elog(LOG, "Start receiving notifications from channel «%s»",
      mv_updater_channel);
  }

  while (true)
  {
    /* Cota de tiempo */
    WaitLatch(&MyProc->procLatch,
      WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
      (long int) mv_updater_laptime * 1000L);
    ResetLatch(&MyProc->procLatch);

    /* Se espera a que algo suceda en la conexión utilizando 'select' */
    sock = PQsocket(conn);

    /* Esto sólo podría suceder cuando se está reiniciando Postgresql */
    if (sock < 0)
    {
      elog(WARNING, "sock < 0");
      continue;
    }

    FD_ZERO(&input_mask);
    FD_SET(sock, &input_mask);

    if (select(sock + 1, &input_mask, NULL, NULL, NULL) < 0)
    {
      elog(ERROR, "select() failed: «%s»", strerror(errno));
      exit_nicely(conn);
    }

    /* Ahora se analiza la entrada y se atienden todas las notificaciones en
     * cola.
     */
    PQconsumeInput(conn);
    while ((notify = PQnotifies(conn)) != NULL)
    {
      myLog("Received notification '%s' from PID %d with payload: %s",
        notify->relname, notify->be_pid, notify->extra);

      process_notification(notify);
      PQfreemem(notify);
    }

    process_refresh_requests(refresh_requests);
    clean_refresh_requests(refresh_requests);
    refresh_requests = NULL;
  }

  exit_nicely(conn);
  return;
}
//}}}

/* Inicialización del servicio */
void
_PG_init(void)
//{{{
{
  BackgroundWorker worker;

  /* Variables de configuración del worker */
  /* NOTE: No encontré buena documentación de cómo definir estos parámetros.
   *       Esto lo escribí basándome en ejemplos y eleyendo la definición de
   *       cada función y estructura de datos.
   *       - http://doxygen.postgresql.org/guc_8c.html
   */

  // Cadena de conexión a la base de datos
  DefineCustomStringVariable("mv_updater.conninfo",
                             "Cadena de conexión a la base de datos",
                             "Ver http://www.postgresql.org/docs/9.3/static/libpq-connect.html#LIBPQ-CONNSTRING",
                             &mv_updater_conninfo,
                             NULL,		/* bootValue (se notificará cuando sea vacío) */
                             PGC_POSTMASTER,	/* (GucContext) context */
                             0,			/* flags */
                             NULL,		/* check_hook */
                             NULL,		/* assign_hook */
                             NULL		/* shook_hook */
  );

  // Esquema donde se encuentran las vistas materializadas
  DefineCustomStringVariable("mv_updater.schema",
                             "Esquema donde se encuentran las vistas materializadas",
                             NULL,
                             &mv_updater_schema,
                             "public",          /* bootValue */
                             PGC_POSTMASTER,    /* (GucContext) context */
                             0,                 /* flags */
                             NULL,              /* check_hook */
                             NULL,              /* assign_hook */
                             NULL               /* shook_hook */
  );

  // Canal de escucha de notificaciones
  DefineCustomStringVariable("mv_updater.channel",
                             "Canal de notificaciones",
                             NULL,
                             &mv_updater_channel,
                             "mv_update",       /* bootValue */
                             PGC_POSTMASTER,    /* (GucContext) context */
                             0,                 /* flags */
                             NULL,              /* check_hook */
                             NULL,              /* assign_hook */
                             NULL               /* shook_hook */
  );

  // Tiempo (en segundos) entre cada ciclo
  DefineCustomIntVariable("mv_updater.laptime",
                          "Tiempo (en segundos) entre cada ciclo",
                          NULL,
                          &mv_updater_laptime,
                          10,			/* bootValue */
                          1,			/* minValue */
                          3600,			/* maxValue */
                          PGC_POSTMASTER,	/* (GucContext) context */
                          0,			/* flags */
                          NULL,			/* check_hook */
                          NULL,			/* assign_hook */
                          NULL			/* shook_hook */
  );

  // Directorio para archivos de Log
  DefineCustomStringVariable("mv_updater.logdir",
                             "Directo para archivos de Log",
                             NULL,
                             &mv_updater_logdir,
                             "/var/log/mv_updater",       /* bootValue */
                             PGC_POSTMASTER,    /* (GucContext) context */
                             0,                 /* flags */
                             NULL,              /* check_hook */
                             NULL,              /* assign_hook */
                             NULL               /* shook_hook */
  );

  /* Fin de variables de configuración */

  /* Acceso a la memoria compartida de Postgres */
  worker.bgw_flags 	= BGWORKER_SHMEM_ACCESS;
  /* Iniciar cuando Postgres entre en un estado normal de lectura/escritura */
  worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
  /* Puntero a la función inicial */
  worker.bgw_main 	= &mv_updater_main;
  /* Nombre del servicio */
  strncpy(worker.bgw_name, "MV Updater", sizeof(worker.bgw_name));
  /* Reiniciar el servicio en 1 segundo en caso que falle */
  worker.bgw_restart_time = 1;
  /* Argumentos de la función principal */
  worker.bgw_main_arg 	= (Datum) NULL;
  /* Desde PostgreSQL 9.4 es necesario setear este valor en 0 */
  worker.bgw_notify_pid = 0;

  RegisterBackgroundWorker(&worker);
}
//}}}
