#include <arrow-adbc/adbc.h>
#include <arrow-adbc/adbc_driver_manager.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                         \
    }                                                                     \
  } while (0)

struct Mock {
  int database_releases = 0;
  int driver_releases = 0;
  std::string last_key;
  std::string last_value;
};

Mock mock;

void FreeMockError(AdbcError* error) {
  delete[] error->message;
  error->message = nullptr;
  error->release = nullptr;
}

AdbcStatusCode DatabaseNew(AdbcDatabase* database, AdbcError*) {
  database->private_data = &mock;
  return ADBC_STATUS_OK;
}

// Accepts "uri" and rejects every other key with an error of its own.
AdbcStatusCode DatabaseSetOption(AdbcDatabase*, const char* key, const char* value,
                                 AdbcError* error) {
  mock.last_key = key;
  mock.last_value = value;
  if (mock.last_key == "uri") return ADBC_STATUS_OK;
  const std::string text = "mock: unknown option " + mock.last_key;
  if (error->release) error->release(error);
  error->message = new char[text.size() + 1];
  std::memcpy(error->message, text.c_str(), text.size() + 1);
  error->release = FreeMockError;
  return ADBC_STATUS_INVALID_ARGUMENT;
}

AdbcStatusCode DatabaseInit(AdbcDatabase*, AdbcError*) { return ADBC_STATUS_OK; }

AdbcStatusCode DatabaseRelease(AdbcDatabase* database, AdbcError*) {
  database->private_data = nullptr;
  ++mock.database_releases;
  return ADBC_STATUS_OK;
}

AdbcStatusCode ConnectionNew(AdbcConnection*, AdbcError*) { return ADBC_STATUS_OK; }
AdbcStatusCode ConnectionInit(AdbcConnection*, AdbcDatabase*, AdbcError*) {
  return ADBC_STATUS_OK;
}
AdbcStatusCode ConnectionRelease(AdbcConnection*, AdbcError*) { return ADBC_STATUS_OK; }
AdbcStatusCode StatementNew(AdbcConnection*, AdbcStatement*, AdbcError*) {
  return ADBC_STATUS_OK;
}
AdbcStatusCode StatementExecuteQuery(AdbcStatement*, ArrowArrayStream*, int64_t*,
                                     AdbcError*) {
  return ADBC_STATUS_OK;
}
AdbcStatusCode StatementRelease(AdbcStatement*, AdbcError*) { return ADBC_STATUS_OK; }

AdbcStatusCode Release(AdbcDriver*, AdbcError*) {
  ++mock.driver_releases;
  return ADBC_STATUS_OK;
}

AdbcStatusCode MockInit(int version, void* raw_driver, AdbcError*) {
  if (version != ADBC_VERSION_1_1_0) return ADBC_STATUS_NOT_IMPLEMENTED;
  AdbcDriver* driver = static_cast<AdbcDriver*>(raw_driver);
  driver->release = Release;
  driver->DatabaseNew = DatabaseNew;
  driver->DatabaseSetOption = DatabaseSetOption;
  driver->DatabaseInit = DatabaseInit;
  driver->DatabaseRelease = DatabaseRelease;
  driver->ConnectionNew = ConnectionNew;
  driver->ConnectionInit = ConnectionInit;
  driver->ConnectionRelease = ConnectionRelease;
  driver->StatementNew = StatementNew;
  driver->StatementExecuteQuery = StatementExecuteQuery;
  driver->StatementRelease = StatementRelease;
  return ADBC_STATUS_OK;
}

void ReleaseError(AdbcError* error) {
  if (error->release) error->release(error);
  CHECK(error->release == nullptr);
}

std::string Message(const AdbcError& error) {
  return error.message ? std::string(error.message) : std::string("<none>");
}

void TestLoadDriverFromInitFunc() {
  AdbcError error = ADBC_ERROR_INIT;
  CHECK(error.vendor_code == ADBC_ERROR_VENDOR_CODE_PRIVATE_DATA);
  CHECK(error.message == nullptr && error.release == nullptr);

  AdbcDriver driver;
  std::memset(&driver, 0, sizeof driver);
  CHECK(AdbcLoadDriverFromInitFunc(MockInit, ADBC_VERSION_1_1_0, &driver, &error) ==
        ADBC_STATUS_OK);
  CHECK(driver.DatabaseNew == DatabaseNew);
  // The functions the driver left out are filled in with the manager's stubs.
  CHECK(driver.ConnectionCommit != nullptr);
  CHECK(driver.StatementPrepare != nullptr);
  CHECK(driver.ErrorGetDetailCount != nullptr);
  CHECK(driver.DatabaseGetOption != nullptr);

  AdbcDatabase database;
  std::memset(&database, 0, sizeof database);
  char value[16] = {0};
  size_t length = sizeof value;
  CHECK(driver.DatabaseGetOption(&database, "uri", value, &length, &error) ==
        ADBC_STATUS_NOT_FOUND);
  CHECK(Message(error) == "[Driver Manager] AdbcDatabaseGetOption not implemented");
  ReleaseError(&error);
  CHECK(driver.ErrorGetDetailCount(&error) == 0);
  CHECK(driver.release(&driver, &error) == ADBC_STATUS_OK);
  CHECK(mock.driver_releases == 1);

  std::memset(&driver, 0, sizeof driver);
  CHECK(AdbcLoadDriverFromInitFunc(MockInit, 2000000, &driver, &error) ==
        ADBC_STATUS_NOT_IMPLEMENTED);
  CHECK(Message(error) == "[Driver Manager] Only ADBC 1.0.0 and 1.1.0 are supported");
  ReleaseError(&error);
}

void TestDriverErrorOnInit() {
  mock = Mock();
  AdbcError error = ADBC_ERROR_INIT;
  AdbcDatabase database;
  std::memset(&database, 0, sizeof database);

  CHECK(AdbcDatabaseNew(&database, &error) == ADBC_STATUS_OK);
  CHECK(AdbcDatabaseSetOption(&database, "colour", "blue", &error) == ADBC_STATUS_OK);
  CHECK(AdbcDriverManagerDatabaseSetInitFunc(&database, MockInit, &error) ==
        ADBC_STATUS_OK);
  // The driver's own error reaches the caller, and the half-made database
  // and the driver are released.
  CHECK(AdbcDatabaseInit(&database, &error) == ADBC_STATUS_INVALID_ARGUMENT);
  CHECK(Message(error) == "mock: unknown option colour");
  CHECK(mock.last_value == "blue");
  CHECK(mock.database_releases == 1);
  CHECK(mock.driver_releases == 1);
  CHECK(database.private_driver == nullptr);
  ReleaseError(&error);
  CHECK(AdbcDatabaseRelease(&database, &error) == ADBC_STATUS_INVALID_STATE);
}

bool WriteFile(const std::string& path, const char* contents) {
  std::ofstream out(path.c_str(), std::ios::binary);
  out << contents;
  return static_cast<bool>(out);
}

void TestFindLoadDriver() {
  AdbcError error = ADBC_ERROR_INIT;
  AdbcDriver driver;
  std::memset(&driver, 0, sizeof driver);
  // Among the probe's build outputs: `cabin run` keeps the caller's working
  // directory.
  const char* dir = std::getenv("CABIN_PROFILE_DIR");
  CHECK(dir != nullptr);
  if (dir == nullptr) return;
  const std::string manifest = std::string(dir) + "/cabin_adbc_cc.toml";
  CHECK(WriteFile(manifest, "name = \"Cabin\"\nmanifest_version = 7\n"));

  // An absolute path to a manifest is parsed directly ...
  CHECK(AdbcFindLoadDriver(manifest.c_str(), nullptr, ADBC_VERSION_1_1_0, 0, nullptr,
                           &driver, &error) == ADBC_STATUS_INVALID_ARGUMENT);
  CHECK(Message(error) ==
        "[Driver Manager] Driver manifest version '7' is not supported by this driver "
        "manager.");
  ReleaseError(&error);

  // ... a relative one only when relative paths are allowed ...
  CHECK(AdbcFindLoadDriver("cabin_adbc_cc.toml", nullptr, ADBC_VERSION_1_1_0, 0, nullptr,
                           &driver, &error) == ADBC_STATUS_INVALID_ARGUMENT);
  CHECK(Message(error) ==
        "[Driver Manager] Driver path is relative and relative paths are not allowed");
  ReleaseError(&error);

  // ... and a bare driver name finds <name>.toml in an additional search path.
  CHECK(AdbcFindLoadDriver("cabin_adbc_cc", nullptr, ADBC_VERSION_1_1_0, 0, dir, &driver,
                           &error) == ADBC_STATUS_INVALID_ARGUMENT);
  CHECK(Message(error).find(
            "Driver manifest version '7' is not supported by this driver manager.") !=
        std::string::npos);
  CHECK(Message(error).find("additional search path: ") != std::string::npos);
  ReleaseError(&error);

  CHECK(AdbcFindLoadDriver(manifest.c_str(), nullptr, 3, ADBC_LOAD_FLAG_DEFAULT, nullptr,
                           &driver, &error) == ADBC_STATUS_NOT_IMPLEMENTED);
  CHECK(Message(error) == "[Driver Manager] Only ADBC 1.0.0 and 1.1.0 are supported");
  ReleaseError(&error);

  CHECK(std::remove(manifest.c_str()) == 0);
}

}  // namespace

int main() {
  CHECK(std::string(AdbcStatusCodeMessage(ADBC_STATUS_INVALID_STATE)) == "INVALID_STATE");

  TestLoadDriverFromInitFunc();
  TestDriverErrorOnInit();
  TestFindLoadDriver();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("arrow-adbc C++ consumer: ok");
  return 0;
}
