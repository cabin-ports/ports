#include <kangaru/kangaru.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// After the README's example: a scene shares the container's one camera.
struct Camera {
  int zoom;
  Camera() : zoom(1) {}
};
struct Scene {
  Camera& camera;
};
struct CameraService : kgr::single_service<Camera> {};
struct SceneService : kgr::service<Scene, kgr::dependency<CameraService>> {};

// A plain service makes a new instance each time.
struct Ticket {
  int number;
  Ticket() : number(next()++) {}
  static int& next() {
    static int n = 0;
    return n;
  }
};
struct TicketService : kgr::service<Ticket> {};

std::string& Log() {
  static std::string log;
  return log;
}
struct First {
  ~First() { Log() += 'A'; }
};
struct Second {
  ~Second() { Log() += 'B'; }
};
struct FirstService : kgr::single_service<First> {};
struct SecondService : kgr::single_service<Second> {};

int Zoomed(Camera& camera, Ticket ticket, int factor) {
  return camera.zoom * factor + ticket.number;
}

void TestServices() {
  kgr::container container;
  check(!container.contains<CameraService>(), "no camera before use");
  Scene scene = container.service<SceneService>();
  Camera& camera = container.service<CameraService>();
  check(&scene.camera == &camera && container.contains<CameraService>(),
        "the scene gets the single camera");
  camera.zoom = 3;
  check(container.service<SceneService>().camera.zoom == 3,
        "a second scene shares it");

  const int a = container.service<TicketService>().number;
  const int b = container.service<TicketService>().number;
  check(b == a + 1, "a plain service is built per request");

  const int zoomed =
      container.invoke<CameraService, TicketService>(Zoomed, 10);
  check(zoomed == 30 + b + 1, "invoke injects services, then the argument");
}

void TestReverseDestruction() {
  {
    kgr::container container;
    container.service<FirstService>();
    container.service<SecondService>();
  }
  // KGR_KANGARU_REVERSE_DESTRUCTION: the last single service built goes
  // first.
  check(Log() == "BA", "reverse destruction");
}

}  // namespace

int main() {
  TestServices();
  TestReverseDestruction();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("kangaru consumer test passed");
  return 0;
}
