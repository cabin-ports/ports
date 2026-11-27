#include <arrow-adbc/adbc.h>
#include <arrow-adbc/adbc_driver_manager.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

/* What the mock driver below saw of the driver manager's calls. */
static struct {
  int init_versions[4];
  int init_calls;
  int database_inits;
  int database_releases;
  int connection_inits;
  int connection_releases;
  int statement_releases;
  int driver_releases;
  char option_key[32];
  char option_value[32];
} mock;

static void ResetMock(void) { memset(&mock, 0, sizeof mock); }

static AdbcStatusCode MockDatabaseNew(struct AdbcDatabase* database,
                                      struct AdbcError* error) {
  (void)error;
  database->private_data = &mock;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockDatabaseSetOption(struct AdbcDatabase* database,
                                            const char* key, const char* value,
                                            struct AdbcError* error) {
  (void)database;
  (void)error;
  snprintf(mock.option_key, sizeof mock.option_key, "%s", key);
  snprintf(mock.option_value, sizeof mock.option_value, "%s", value);
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockDatabaseInit(struct AdbcDatabase* database,
                                       struct AdbcError* error) {
  (void)error;
  CHECK(database->private_data == &mock);
  ++mock.database_inits;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockDatabaseRelease(struct AdbcDatabase* database,
                                          struct AdbcError* error) {
  (void)error;
  database->private_data = NULL;
  ++mock.database_releases;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockConnectionNew(struct AdbcConnection* connection,
                                        struct AdbcError* error) {
  (void)error;
  connection->private_data = &mock;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockConnectionInit(struct AdbcConnection* connection,
                                         struct AdbcDatabase* database,
                                         struct AdbcError* error) {
  (void)error;
  CHECK(connection->private_data == &mock);
  CHECK(database->private_data == &mock);
  ++mock.connection_inits;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockConnectionRelease(struct AdbcConnection* connection,
                                            struct AdbcError* error) {
  (void)error;
  connection->private_data = NULL;
  ++mock.connection_releases;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockStatementNew(struct AdbcConnection* connection,
                                       struct AdbcStatement* statement,
                                       struct AdbcError* error) {
  (void)connection;
  (void)error;
  statement->private_data = &mock;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockStatementExecuteQuery(struct AdbcStatement* statement,
                                                struct ArrowArrayStream* out,
                                                int64_t* rows_affected,
                                                struct AdbcError* error) {
  (void)error;
  CHECK(statement->private_data == &mock);
  CHECK(out == NULL);
  *rows_affected = 42;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockStatementRelease(struct AdbcStatement* statement,
                                           struct AdbcError* error) {
  (void)error;
  statement->private_data = NULL;
  ++mock.statement_releases;
  return ADBC_STATUS_OK;
}

static AdbcStatusCode MockRelease(struct AdbcDriver* driver, struct AdbcError* error) {
  (void)error;
  driver->private_data = NULL;
  ++mock.driver_releases;
  return ADBC_STATUS_OK;
}

static void FillMock(struct AdbcDriver* driver) {
  driver->release = MockRelease;
  driver->DatabaseNew = MockDatabaseNew;
  driver->DatabaseSetOption = MockDatabaseSetOption;
  driver->DatabaseInit = MockDatabaseInit;
  driver->DatabaseRelease = MockDatabaseRelease;
  driver->ConnectionNew = MockConnectionNew;
  driver->ConnectionInit = MockConnectionInit;
  driver->ConnectionRelease = MockConnectionRelease;
  driver->StatementNew = MockStatementNew;
  driver->StatementExecuteQuery = MockStatementExecuteQuery;
  driver->StatementRelease = MockStatementRelease;
}

static void RecordVersion(int version) {
  if (mock.init_calls < 4) mock.init_versions[mock.init_calls] = version;
  ++mock.init_calls;
}

/* A driver of ADBC 1.1.0 that leaves every optional function out. */
static AdbcStatusCode MockInit(int version, void* raw_driver, struct AdbcError* error) {
  (void)error;
  RecordVersion(version);
  if (version != ADBC_VERSION_1_1_0) return ADBC_STATUS_NOT_IMPLEMENTED;
  FillMock((struct AdbcDriver*)raw_driver);
  return ADBC_STATUS_OK;
}

/* A driver that only speaks ADBC 1.0.0. */
static AdbcStatusCode MockInit100(int version, void* raw_driver,
                                  struct AdbcError* error) {
  (void)error;
  RecordVersion(version);
  if (version != ADBC_VERSION_1_0_0) return ADBC_STATUS_NOT_IMPLEMENTED;
  FillMock((struct AdbcDriver*)raw_driver);
  return ADBC_STATUS_OK;
}

/* A driver missing StatementExecuteQuery, which the manager requires. */
static AdbcStatusCode MockInitIncomplete(int version, void* raw_driver,
                                         struct AdbcError* error) {
  AdbcStatusCode status = MockInit(version, raw_driver, error);
  ((struct AdbcDriver*)raw_driver)->StatementExecuteQuery = NULL;
  return status;
}

static void ReleaseError(struct AdbcError* error) {
  if (error->release) error->release(error);
  CHECK(error->release == NULL);
}

static int EndsWith(const char* text, const char* suffix) {
  size_t n = strlen(text), m = strlen(suffix);
  return n >= m && strcmp(text + n - m, suffix) == 0;
}

static void TestInitFuncDriver(void) {
  struct AdbcDatabase database;
  struct AdbcConnection connection;
  struct AdbcStatement statement;
  struct AdbcError error;
  int64_t rows = -1;
  memset(&database, 0, sizeof database);
  memset(&connection, 0, sizeof connection);
  memset(&statement, 0, sizeof statement);
  memset(&error, 0, sizeof error);
  ResetMock();

  CHECK(AdbcDatabaseNew(&database, &error) == ADBC_STATUS_OK);
  /* Buffered before Init, then handed to the driver's DatabaseSetOption. */
  CHECK(AdbcDatabaseSetOption(&database, "uri", "mock://cabin", &error) == ADBC_STATUS_OK);
  CHECK(AdbcDriverManagerDatabaseSetInitFunc(&database, MockInit, &error) ==
        ADBC_STATUS_OK);
  CHECK(mock.init_calls == 0);
  CHECK(AdbcDatabaseInit(&database, &error) == ADBC_STATUS_OK);
  CHECK(mock.init_calls == 1);
  CHECK(mock.init_versions[0] == ADBC_VERSION_1_1_0);
  CHECK(mock.database_inits == 1);
  CHECK(strcmp(mock.option_key, "uri") == 0);
  CHECK(strcmp(mock.option_value, "mock://cabin") == 0);

  CHECK(AdbcDriverManagerDatabaseSetInitFunc(&database, MockInit, &error) ==
        ADBC_STATUS_INVALID_STATE);
  CHECK(error.message != NULL &&
        strcmp(error.message,
               "[Driver Manager] Cannot SetInitFunc after AdbcDatabaseInit") == 0);
  ReleaseError(&error);

  CHECK(AdbcConnectionNew(&connection, &error) == ADBC_STATUS_OK);
  CHECK(AdbcConnectionInit(&connection, &database, &error) == ADBC_STATUS_OK);
  CHECK(mock.connection_inits == 1);

  /* The manager fills in the functions the driver left out. */
  CHECK(AdbcConnectionCommit(&connection, &error) == ADBC_STATUS_NOT_IMPLEMENTED);
  CHECK(error.message != NULL &&
        strcmp(error.message, "[Driver Manager] AdbcConnectionCommit not implemented") ==
            0);
  ReleaseError(&error);

  CHECK(AdbcStatementNew(&connection, &statement, &error) == ADBC_STATUS_OK);
  CHECK(AdbcStatementSetSqlQuery(&statement, "SELECT 1", &error) ==
        ADBC_STATUS_NOT_IMPLEMENTED);
  CHECK(error.message != NULL &&
        strcmp(error.message,
               "[Driver Manager] AdbcStatementSetSqlQuery not implemented") == 0);
  ReleaseError(&error);
  CHECK(AdbcStatementExecuteQuery(&statement, NULL, &rows, &error) == ADBC_STATUS_OK);
  CHECK(rows == 42);

  CHECK(AdbcStatementRelease(&statement, &error) == ADBC_STATUS_OK);
  CHECK(AdbcConnectionRelease(&connection, &error) == ADBC_STATUS_OK);
  CHECK(AdbcDatabaseRelease(&database, &error) == ADBC_STATUS_OK);
  CHECK(mock.statement_releases == 1);
  CHECK(mock.connection_releases == 1);
  CHECK(mock.database_releases == 1);
  CHECK(mock.driver_releases == 1);
  CHECK(database.private_driver == NULL);
  CHECK(AdbcDatabaseRelease(&database, &error) == ADBC_STATUS_INVALID_STATE);
  ReleaseError(&error);
}

static void TestVersionNegotiation(void) {
  struct AdbcDatabase database;
  struct AdbcError error;
  memset(&database, 0, sizeof database);
  memset(&error, 0, sizeof error);
  ResetMock();

  CHECK(AdbcDatabaseNew(&database, &error) == ADBC_STATUS_OK);
  CHECK(AdbcDatabaseSetOptionInt(&database, "batch_size", 4096, &error) ==
        ADBC_STATUS_OK);
  CHECK(AdbcDriverManagerDatabaseSetInitFunc(&database, MockInit100, &error) ==
        ADBC_STATUS_OK);
  CHECK(AdbcDatabaseInit(&database, &error) == ADBC_STATUS_OK);
  /* 1.1.0 is refused, so the manager retries with 1.0.0 ... */
  CHECK(mock.init_calls == 2);
  CHECK(mock.init_versions[0] == ADBC_VERSION_1_1_0);
  CHECK(mock.init_versions[1] == ADBC_VERSION_1_0_0);
  /* ... and passes the 1.1.0 integer option on as a 1.0.0 string option. */
  CHECK(strcmp(mock.option_key, "batch_size") == 0);
  CHECK(strcmp(mock.option_value, "4096") == 0);
  CHECK(AdbcDatabaseSetOptionDouble(&database, "timeout", 2.5, &error) ==
        ADBC_STATUS_OK);
  CHECK(strcmp(mock.option_key, "timeout") == 0);
  CHECK(strcmp(mock.option_value, "2.5") == 0);
  CHECK(AdbcDatabaseRelease(&database, &error) == ADBC_STATUS_OK);
  CHECK(mock.driver_releases == 1);
}

static void TestRequiredFunction(void) {
  struct AdbcDatabase database;
  struct AdbcError error;
  memset(&database, 0, sizeof database);
  memset(&error, 0, sizeof error);
  ResetMock();

  CHECK(AdbcDatabaseNew(&database, &error) == ADBC_STATUS_OK);
  CHECK(AdbcDriverManagerDatabaseSetInitFunc(&database, MockInitIncomplete, &error) ==
        ADBC_STATUS_OK);
  CHECK(AdbcDatabaseInit(&database, &error) == ADBC_STATUS_INTERNAL);
  CHECK(error.message != NULL &&
        strcmp(error.message,
               "[Driver Manager] Driver does not implement required function "
               "AdbcStatementExecuteQuery") == 0);
  ReleaseError(&error);
  CHECK(mock.database_inits == 0);
  CHECK(mock.driver_releases == 1);
  CHECK(AdbcDatabaseRelease(&database, &error) == ADBC_STATUS_OK);
}

/* Writes `contents` to the driver manifest `name` among the probe's build
 * outputs (`cabin run` keeps the caller's working directory), loads it as the
 * database's driver, and returns AdbcDatabaseInit's status with its message
 * in `message`. */
static AdbcStatusCode InitFromManifest(const char* name, const char* contents,
                                       char* message, size_t size) {
  struct AdbcDatabase database;
  struct AdbcError error;
  AdbcStatusCode status;
  char path[1024];
  const char* dir = getenv("CABIN_PROFILE_DIR");
  FILE* file;
  memset(&database, 0, sizeof database);
  memset(&error, 0, sizeof error);
  message[0] = '\0';
  CHECK(dir != NULL);
  if (!dir) return ADBC_STATUS_IO;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  file = fopen(path, "wb");
  CHECK(file != NULL);
  if (!file) return ADBC_STATUS_IO;
  CHECK(fputs(contents, file) >= 0);
  CHECK(fclose(file) == 0);

  CHECK(AdbcDatabaseNew(&database, &error) == ADBC_STATUS_OK);
  CHECK(AdbcDatabaseSetOption(&database, "driver", path, &error) == ADBC_STATUS_OK);
  status = AdbcDatabaseInit(&database, &error);
  if (error.message) snprintf(message, size, "%s", error.message);
  ReleaseError(&error);
  CHECK(AdbcDatabaseRelease(&database, &error) == ADBC_STATUS_OK);
  CHECK(remove(path) == 0);
  return status;
}

static void TestDriverManifests(void) {
  char message[4096];
  const char* found;

  CHECK(InitFromManifest("cabin_adbc_v99.toml", "manifest_version = 99\n", message,
                         sizeof message) == ADBC_STATUS_INVALID_ARGUMENT);
  CHECK(strcmp(message,
               "[Driver Manager] Driver manifest version '99' is not supported by "
               "this driver manager.") == 0);

  CHECK(InitFromManifest("cabin_adbc_arch.toml",
                         "name = \"Cabin\"\n"
                         "manifest_version = 1\n"
                         "\n"
                         "[Driver.shared]\n"
                         "cabin_os_arch = \"libcabin.so\"\n",
                         message, sizeof message) == ADBC_STATUS_NOT_FOUND);
  found = strstr(message, "[Driver Manager] Driver path not found in manifest '");
  CHECK(found == message);
  CHECK(strstr(message, "cabin_adbc_arch.toml' for current architecture '") != NULL);
  CHECK(EndsWith(message, "'. Architectures found: cabin_os_arch"));

  CHECK(InitFromManifest("cabin_adbc_syntax.toml", "name = \"Cabin\"\nmanifest_version =\n",
                         message, sizeof message) == ADBC_STATUS_INVALID_ARGUMENT);
  found = strstr(message, "[Driver Manager] Could not open manifest. ");
  CHECK(found == message);
  CHECK(strstr(message, " (line 2, column ") != NULL);
  CHECK(strstr(message, ". Manifest: ") != NULL);
  CHECK(EndsWith(message, "cabin_adbc_syntax.toml"));
}

int main(void) {
  CHECK(strcmp(AdbcStatusCodeMessage(ADBC_STATUS_NOT_FOUND), "NOT_FOUND") == 0);
  CHECK(strcmp(AdbcStatusCodeMessage(ADBC_STATUS_UNAUTHORIZED), "UNAUTHORIZED") == 0);
  CHECK(strcmp(AdbcStatusCodeMessage(99), "(invalid code)") == 0);

  TestInitFuncDriver();
  TestVersionNegotiation();
  TestRequiredFunction();
  TestDriverManifests();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("arrow-adbc C consumer: ok");
  return 0;
}
