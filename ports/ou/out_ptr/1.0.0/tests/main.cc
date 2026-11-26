#include <ztd/out_ptr.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

int g_created = 0;
int g_freed = 0;
int g_last_freed = 0;

struct counting_deleter {
	void operator()(int* p) const {
		++g_freed;
		g_last_freed = *p;
		delete p;
	}
};

int create(int** out) {
	*out = new int(40 + ++g_created);
	return 0;
}

// Takes ownership of *io: frees the old value itself and stores a new one.
int g_recreate_freed = 0;
int recreate(int** io) {
	if (*io) {
		++g_recreate_freed;
		g_last_freed = **io;
		delete *io;
	}
	*io = new int(40 + ++g_created);
	return 0;
}

int create_void(void** out) {
	*out = new int(100 + ++g_created);
	return 0;
}

bool check(bool ok, const char* what) {
	if (!ok)
		std::cerr << "FAILED: " << what << '\n';
	return ok;
}

} // namespace

int main() {
	bool ok = true;

	{
		std::unique_ptr<int, counting_deleter> p;
		int rc = create(ztd::out_ptr::out_ptr(p));
		ok &= check(rc == 0 && p && *p == 41, "out_ptr into unique_ptr");
		ok &= check(g_freed == 0, "out_ptr does not free a null pointer");
	}
	ok &= check(g_freed == 1 && g_last_freed == 41, "unique_ptr deleter ran once on scope exit");

	{
		std::unique_ptr<int, counting_deleter> p(new int(7));
		const int freed_before = g_freed;
		int rc = create(ztd::out_ptr::out_ptr(p));
		ok &= check(rc == 0 && p && *p == 42, "out_ptr reset replaces the value");
		ok &= check(g_freed == freed_before + 1 && g_last_freed == 7, "out_ptr freed the old value once");
	}
	ok &= check(g_freed == 3 && g_last_freed == 42, "second unique_ptr freed on scope exit");

	{
		std::unique_ptr<int, counting_deleter> p(new int(5));
		const int freed_before = g_freed;
		int rc = recreate(ztd::out_ptr::inout_ptr(p));
		ok &= check(rc == 0 && p && *p == 43, "inout_ptr adopts the new value");
		ok &= check(g_recreate_freed == 1 && g_last_freed == 5, "the function freed the old value exactly once");
		ok &= check(g_freed == freed_before, "the deleter did not free the old value again");
	}
	ok &= check(g_freed == 4 && g_last_freed == 43, "inout_ptr result freed on scope exit");

	{
		std::unique_ptr<int, counting_deleter> p;
		int rc = create_void(ztd::out_ptr::out_ptr<void*>(p));
		ok &= check(rc == 0 && p && *p == 104, "out_ptr<void*> fills a void** parameter");
	}
	ok &= check(g_freed == 5 && g_last_freed == 104, "void** result freed on scope exit");

	{
		int shared_freed = 0;
		{
			std::shared_ptr<int> p;
			int rc = create(ztd::out_ptr::out_ptr(p, [&shared_freed](int* q) {
				++shared_freed;
				delete q;
			}));
			ok &= check(rc == 0 && p && *p == 45, "out_ptr into shared_ptr with deleter");
			ok &= check(shared_freed == 0, "shared_ptr deleter not yet run");
		}
		ok &= check(shared_freed == 1, "shared_ptr custom deleter ran once");
	}

	if (ok)
		std::cout << "out_ptr ok\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
