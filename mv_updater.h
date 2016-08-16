/* $Id: mv_updater.h,v 1.6 2015/05/18 15:46:31 aosorio Exp $
 */

#include "time.h"
#include "libpq-fe.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "errno.h"
#include "stdarg.h"
#include "postgres.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "postmaster/bgworker.h"
#include "executor/spi.h"
#include "utils/guc.h"
#include "regex.h"

/* Variables de configuración
 ******************************************************************************/
char *mv_updater_conninfo;
char *mv_updater_schema;
char *mv_updater_channel;
char *mv_updater_logdir;
int mv_updater_laptime;

#define MAX_IGNORED_TIME 60 // Tiempo máximo que se ignorará un PID, en segundos

/* Estructuras y variables globales
 ******************************************************************************/

/* Estructura para los nodos de la lista vinculada de PIDs ignorados */
typedef struct IgnoredPIDs
{
  int pid;
  time_t start_time;
  struct IgnoredPIDs *next;
} IgnoredPID;

/* Estructura para los nodos de la lista vinculada de solicitudes de
 * actualización */
typedef struct RefreshRequests
{
  char *mv_name;
  struct RefreshRequests *next;
} RefreshRequest;

void _PG_init(void); // Punto de entrada para la carga de la librería
static PGconn *conn; // Conexión a la base de datos
static RefreshRequest *refresh_requests; // Lista vinculada de notificaciones recibidas
static IgnoredPID *ignored_pids; // Lista vinculada de PIDs ignorados 

/* Prototipo de funciones
 ******************************************************************************/
static void myLog(char *, ...);
static void exit_nicely(PGconn *);
static bool match(const char *, char *);
static bool is_pid_ignored(IgnoredPID *, int);
static bool its_an_old_ignored_pid(IgnoredPID *);
static IgnoredPID * create_ignored_pid_node(int);
static IgnoredPID * delete_ignored_pid_node(IgnoredPID *);
static IgnoredPID * add_pid_ignored(IgnoredPID *, int);
static IgnoredPID * delete_pid_ignored(IgnoredPID *, int);
static void start_ignore(PGnotify *);
static void stop_ignore(PGnotify *);
static IgnoredPID * purge_ignored_pids(IgnoredPID *);
static void ping();
static RefreshRequest * create_refresh_request_node(char *);
static void add_refresh_request(RefreshRequest *, char *);
static void save_refresh_request(PGnotify *);
static void refresh_matview(char *);
static void process_refresh_requests(RefreshRequest *);
static void process_notification(PGnotify *);
static void mv_updater_main(Datum);
void _PG_init(void);
