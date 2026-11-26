#include <CivetServer.h>
#include <civetweb.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

bool socketDenied = false;

int onLog(const struct mg_connection *, const char *msg) {
  if (std::strstr(msg, "cannot create socket") || std::strstr(msg, "cannot bind") ||
      std::strstr(msg, "cannot listen")) {
    socketDenied = true;
  }
  std::fprintf(stderr, "civetweb: %s\n", msg);
  return 1;
}

class Hello : public CivetHandler {
 public:
  bool handleGet(CivetServer *, struct mg_connection *conn) {
    const std::string body = "hello from civetweb-cpp";
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
              "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
              static_cast<int>(body.size()), body.c_str());
    return true;
  }
};

}  // namespace

int main() {
  std::vector<std::string> options;
  options.push_back("listening_ports");
  options.push_back("127.0.0.1:0");
  options.push_back("num_threads");
  options.push_back("2");

  mg_init_library(0);
  std::string decoded;
  CivetServer::urlDecode("a%20b%2Fc", decoded, false);
  check(decoded == "a b/c", "urlDecode");
  std::string encoded;
  CivetServer::urlEncode("a b/c", encoded, false);
  check(encoded == "a%20b%2fc", "urlEncode");
  check(std::strcmp(mg_version(), "1.16") == 0, "version");

  try {
    CivetCallbacks callbacks;
    callbacks.log_message = onLog;
    CivetServer server(options, &callbacks);
    Hello hello;
    server.addHandler("/hello", hello);

    std::vector<int> ports = server.getListeningPorts();
    check(ports.size() == 1 && ports[0] > 0, "listening port");
    if (ports.size() == 1) {
      char ebuf[256] = "";
      char buf[128];
      struct mg_connection *conn =
          mg_download("127.0.0.1", ports[0], 0, ebuf, sizeof ebuf,
                      "GET /hello HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n");
      check(conn != 0, "mg_download");
      if (conn != 0) {
        check(mg_get_response_info(conn)->status_code == 200, "status 200");
        int n = mg_read(conn, buf, sizeof buf - 1);
        check(n == static_cast<int>(std::strlen("hello from civetweb-cpp")), "body length");
        if (n > 0) {
          buf[n] = '\0';
          check(std::strcmp(buf, "hello from civetweb-cpp") == 0, "body");
        }
        mg_close_connection(conn);
      }
    }
  } catch (const CivetException &e) {
    if (socketDenied) {
      // The check sandbox has no loopback; only the pure functions run there.
      std::printf("loopback unavailable: server checks skipped (%s)\n", e.what());
    } else {
      check(false, e.what());
    }
  }
  mg_exit_library();
  return failures != 0;
}
