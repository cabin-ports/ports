#include <celero/Callbacks.h>
#include <celero/Celero.h>
#include <celero/ExperimentResult.h>
#include <celero/Statistics.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_baseline = 0;
static int g_bench = 0;
static int g_complete = 0;
static bool g_results_ok = true;

BASELINE(Group, Base, 3, 10)
{
	++g_baseline;
}

BENCHMARK(Group, Bench, 2, 5)
{
	++g_bench;
}

#define CHECK(c)                                                  \
	do                                                            \
	{                                                             \
		if(!(c))                                                  \
		{                                                         \
			std::printf("%s:%d: %s\n", __FILE__, __LINE__, #c);   \
			return EXIT_FAILURE;                                  \
		}                                                         \
	} while(0)

int main(int argc, char** argv)
{
	celero::Statistics<int64_t> s;
	for(int64_t v : {2, 4, 4, 4, 5, 5, 7, 9})
	{
		s.addSample(v);
	}
	CHECK(s.getSize() == 8);
	CHECK(std::fabs(s.getMean() - 5.0) < 1e-12);
	CHECK(std::fabs(s.getVariance() - 32.0 / 7.0) < 1e-12);
	CHECK(std::fabs(s.getStandardDeviation() - std::sqrt(32.0 / 7.0)) < 1e-12);
	CHECK(s.getMin() == 2);
	CHECK(s.getMax() == 9);

	celero::Statistics<int64_t> a, b;
	for(int64_t v : {2, 4, 4, 4})
	{
		a.addSample(v);
	}
	for(int64_t v : {5, 5, 7, 9})
	{
		b.addSample(v);
	}
	const auto c = a + b;
	CHECK(c.getSize() == 8);
	CHECK(std::fabs(c.getMean() - 5.0) < 1e-12);
	CHECK(std::fabs(c.getVariance() - 32.0 / 7.0) < 1e-12);
	CHECK(c.getMin() == 2);
	CHECK(c.getMax() == 9);

	celero::AddExperimentResultCompleteFunction([](std::shared_ptr<celero::ExperimentResult> r) {
		++g_complete;
		const auto samples = r->getTimeStatistics().getSize();
		g_results_ok = g_results_ok && r->getComplete() && !r->getFailure() && r->getProblemSpaceIterations() == (samples == 3 ? 10u : 5u) && (samples == 3 || samples == 2);
	});

	celero::Run(argc, argv);
	CHECK(g_complete == 2);
	CHECK(g_results_ok);
	// Each experiment runs one unrecorded warm-up pass before its samples.
	CHECK(g_baseline == (3 + 1) * 10);
	CHECK(g_bench == (2 + 1) * 5);
	return EXIT_SUCCESS;
}
