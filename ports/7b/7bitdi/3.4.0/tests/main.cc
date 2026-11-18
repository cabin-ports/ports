#include <SevenBit/DI.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#ifdef _WIN32
// 7bitDI defines the empty macros INLINE and EXPORT in every TU; this checks
// that the SDK headers still compile after them.
#include <windows.h>
#endif

using sb::di::ServiceCollection;
using sb::di::ServiceNotFoundException;
using sb::di::ServiceProvider;

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// README.md's sample usage.
struct IServiceA
{
    virtual std::string actionA() = 0;
    virtual ~IServiceA() = default;
};

struct IServiceB
{
    virtual std::string actionB() = 0;
    virtual ~IServiceB() = default;
};

struct ServiceA final : IServiceA
{
    std::string actionA() override { return "actionA"; }
};

struct ServiceB final : IServiceB
{
    std::string actionB() override { return "actionB"; }
};

class ServiceExecutor
{
    IServiceA &_serviceA;
    std::unique_ptr<IServiceB> _serviceB;

  public:
    ServiceExecutor(IServiceA &serviceA, std::unique_ptr<IServiceB> serviceB)
        : _serviceA(serviceA), _serviceB(std::move(serviceB))
    {
    }

    std::string execute() const { return _serviceA.actionA() + ", " + _serviceB->actionB() + " executed."; }
};

void testReadmeSample()
{
    ServiceProvider provider = ServiceCollection{}
                                   .addSingleton<IServiceA, ServiceA>()
                                   .addTransient<IServiceB, ServiceB>()
                                   .addScoped<ServiceExecutor>()
                                   .buildServiceProvider();

    const ServiceExecutor &executor = provider.getService<ServiceExecutor>();
    check(executor.execute() == "actionA, actionB executed.", "README sample output");
    check(&provider.getService<ServiceExecutor>() == &executor, "scoped executor is reused");
    check(provider.getService<IServiceA>().actionA() == "actionA", "singleton resolves to ServiceA");
    check(provider.createService<IServiceB>()->actionB() == "actionB", "transient resolves to ServiceB");
}

int singletonsMade = 0;
int scopedMade = 0;
int scopedDestroyed = 0;
int transientsMade = 0;

struct SingletonService
{
    SingletonService() { ++singletonsMade; }
};

struct ScopedService
{
    ScopedService() { ++scopedMade; }
    ~ScopedService() { ++scopedDestroyed; }
};

struct TransientService
{
    int id;
    TransientService() : id(++transientsMade) {}
};

struct Consumer
{
    SingletonService &singleton;
    ScopedService &scoped;
    std::unique_ptr<TransientService> transient;

    Consumer(SingletonService &singleton, ScopedService &scoped, std::unique_ptr<TransientService> transient)
        : singleton(singleton), scoped(scoped), transient(std::move(transient))
    {
    }
};

struct Unregistered
{
};

void testLifetimes()
{
    {
        ServiceProvider root = ServiceCollection{}
                                   .addSingleton<SingletonService>()
                                   .addScoped<ScopedService>()
                                   .addTransient<TransientService>()
                                   .addTransient<Consumer>()
                                   .buildServiceProvider();
        check(singletonsMade == 0, "singletons are built on first request");

        SingletonService &rootSingleton = root.getService<SingletonService>();
        check(&root.getService<SingletonService>() == &rootSingleton, "singleton identity");
        ScopedService &rootScoped = root.getService<ScopedService>();
        check(&root.getService<ScopedService>() == &rootScoped, "scoped identity within the root");

        {
            ServiceProvider scope = root.createScope();
            check(&scope.getService<SingletonService>() == &rootSingleton, "a scope shares the singleton");
            ScopedService &scoped = scope.getService<ScopedService>();
            check(&scoped != &rootScoped, "a scope gets its own scoped service");
            check(&scope.getService<ScopedService>() == &scoped, "scoped identity within the scope");

            std::unique_ptr<Consumer> consumer = scope.createService<Consumer>();
            check(&consumer->singleton == &rootSingleton, "injected singleton");
            check(&consumer->scoped == &scoped, "injected scoped service comes from the requesting scope");
            check(consumer->transient && consumer->transient->id == 1, "injected transient");
        }
        check(singletonsMade == 1, "one singleton");
        check(scopedMade == 2, "one scoped service per scope");
        check(scopedDestroyed == 1, "a scope destroys its scoped service");

        std::unique_ptr<TransientService> first = root.createService<TransientService>();
        std::unique_ptr<TransientService> second = root.createService<TransientService>();
        check(first && second && first->id == 2 && second->id == 3, "each transient is new");
        TransientService inPlace = root.createServiceInPlace<TransientService>();
        check(inPlace.id == 4, "in-place transient");
        check(transientsMade == 4, "one transient per request");

        check(root.tryGetService<TransientService>() == nullptr, "transients cannot be got");
        check(root.tryGetService<Unregistered>() == nullptr, "unregistered tryGetService");
        bool threw = false;
        try
        {
            root.getService<Unregistered>();
        }
        catch (const ServiceNotFoundException &)
        {
            threw = true;
        }
        check(threw, "unregistered getService throws ServiceNotFoundException");
    }
    check(scopedDestroyed == 2, "the root destroys its scoped service");
}
} // namespace

int main()
{
    testReadmeSample();
    testLifetimes();
    if (failures != 0)
    {
        return 1;
    }
    std::puts("7bitdi consumer test passed");
    return 0;
}
