// Expected values are closed forms computed here (direct DFT sums, the
// Butterworth magnitude, polynomials, a full-precision Bessel series, sums and
// maxima of the inputs), never the library's own algorithms.

// First, so the patch is what lets mix.h compile on its own.
#include <signalsmith-dsp/mix.h>

#include <signalsmith-dsp/curves.h>
#include <signalsmith-dsp/delay.h>
#include <signalsmith-dsp/envelopes.h>
#include <signalsmith-dsp/fft.h>
#include <signalsmith-dsp/filters.h>
#include <signalsmith-dsp/perf.h>
#include <signalsmith-dsp/rates.h>
#include <signalsmith-dsp/spectral.h>
#include <signalsmith-dsp/windows.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

SIGNALSMITH_DSP_VERSION_CHECK(1, 7, 0)
static_assert(!signalsmith::versionCheck(1, 7, 1), "v1.7.3 reports 1.7.0");

namespace {

typedef std::complex<double> Complex;
const double pi = 3.14159265358979323846;
int failures = 0;

void check(bool ok, const char *what, int line) {
	if (!ok) {
		std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
		++failures;
	}
}
#define CHECK(x) check((x), #x, __LINE__)

// Reports the size or parameter `n` along with the measured error.
void checkError(double error, double tolerance, const char *what, double n, int line) {
	if (!(error <= tolerance)) {
		std::fprintf(stderr, "main.cc:%d: %s (n = %g): error %g > %g\n", line, what, n, error, tolerance);
		++failures;
	}
}
#define CHECK_ERROR(error, tolerance, what, n) checkError((error), (tolerance), (what), (n), __LINE__)

// Deterministic samples in [-1, 1).
struct Lcg {
	std::uint64_t state;
	explicit Lcg(std::uint64_t seed) : state(seed) {}
	double next() {
		state = state*6364136223846793005ULL + 1442695040888963407ULL;
		return double(state >> 11)/4503599627370496.0 - 1;
	}
};

// X[k] = sum_j x[j] exp(sign*i*pi*(2k + half)*j/n), the phase reduced exactly
// in integers; half = 1 gives the half-bin-shifted transform.
std::vector<Complex> dft(const std::vector<Complex> &x, std::size_t bins, int half, int sign) {
	std::size_t n = x.size();
	std::vector<Complex> out(bins);
	for (std::size_t k = 0; k < bins; ++k) {
		Complex sum = 0;
		for (std::size_t j = 0; j < n; ++j) {
			std::size_t m = ((2*k + half)*j)%(2*n);
			sum += x[j]*std::polar(1.0, sign*pi*double(m)/double(n));
		}
		out[k] = sum;
	}
	return out;
}

// Like std::max, but a NaN sticks, so it fails CHECK_ERROR.
double worst(double error, double e) {
	return std::isnan(error) || e <= error ? error : e;
}

double maxError(const std::vector<Complex> &got, const std::vector<Complex> &want, double scale) {
	double error = 0;
	for (std::size_t i = 0; i < want.size(); ++i) {
		error = worst(error, std::abs(got[i] - want[i]*scale));
	}
	return error;
}

bool smooth(std::size_t n) { // 2^a * 3^b
	while (n%2 == 0) n /= 2;
	while (n%3 == 0) n /= 3;
	return n == 1;
}

void testComplexFft() {
	// Radix 2, 3 and 4 steps, the generic step for other primes, and their mixes.
	const std::size_t sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16, 18, 24, 30, 49, 60, 64, 77, 96, 128, 210};
	Lcg rng(1);
	for (std::size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); ++s) {
		std::size_t n = sizes[s];
		signalsmith::fft::FFT<double> fft(n);
		CHECK(fft.size() == n);
		std::vector<Complex> x(n), spectrum(n), back(n);
		for (std::size_t j = 0; j < n; ++j) x[j] = Complex(rng.next(), rng.next());
		fft.fft(x, spectrum);
		CHECK_ERROR(maxError(spectrum, dft(x, n, 0, -1), 1), 1e-12*n, "FFT<double> forward", n);
		// The inverse is unscaled.
		fft.ifft(spectrum, back);
		CHECK_ERROR(maxError(back, x, double(n)), 1e-12*n, "FFT<double> round trip", n);
		std::vector<Complex> inverse(n);
		fft.ifft(x, inverse);
		CHECK_ERROR(maxError(inverse, dft(x, n, 0, 1), 1), 1e-12*n, "FFT<double> inverse", n);
	}

	// Large enough that the first radix-4 pass splits into separate sub-FFTs.
	const std::size_t n = 32768, k1 = 5, k2 = 12345;
	const Complex a(1, 0), b(0, 0.25);
	signalsmith::fft::FFT<double> fft(n);
	std::vector<Complex> x(n), spectrum(n), back(n), want(n);
	for (std::size_t j = 0; j < n; ++j) {
		x[j] = a*std::polar(1.0, 2*pi*double((k1*j)%n)/n) + b*std::polar(1.0, 2*pi*double((k2*j)%n)/n);
	}
	want[k1] = a*double(n);
	want[k2] = b*double(n);
	fft.fft(x, spectrum);
	CHECK_ERROR(maxError(spectrum, want, 1), 1e-9, "FFT<double> two tones", n);
	fft.ifft(spectrum, back);
	CHECK_ERROR(maxError(back, x, double(n)), 1e-8, "FFT<double> two-tone round trip", n);

	std::vector<std::complex<float>> xf(48), spectrumF(48);
	std::vector<Complex> xd(48), spectrumD(48);
	for (std::size_t j = 0; j < 48; ++j) {
		xf[j] = std::complex<float>(float(rng.next()), float(rng.next()));
		xd[j] = Complex(double(xf[j].real()), double(xf[j].imag()));
	}
	signalsmith::fft::FFT<float> fftF(48);
	fftF.fft(xf, spectrumF);
	for (std::size_t k = 0; k < 48; ++k) spectrumD[k] = Complex(double(spectrumF[k].real()), double(spectrumF[k].imag()));
	CHECK_ERROR(maxError(spectrumD, dft(xd, 48, 0, -1), 1), 1e-4, "FFT<float> forward", 48);

	for (std::size_t size = 1; size <= 2000; ++size) {
		std::size_t above = signalsmith::fft::FFT<double>::fastSizeAbove(size);
		std::size_t below = signalsmith::fft::FFT<double>::fastSizeBelow(size);
		if (!(above >= size && smooth(above) && below <= size && below >= 1 && smooth(below))) {
			CHECK_ERROR(1, 0, "fastSizeAbove/fastSizeBelow", size);
		}
	}
}

void testRealFft() {
	const std::size_t sizes[] = {2, 6, 24, 30, 64};
	Lcg rng(2);
	for (std::size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); ++s) {
		std::size_t n = sizes[s];
		std::vector<double> x(n), back(n);
		std::vector<Complex> xc(n), spectrum(n/2), backC(n);
		for (std::size_t j = 0; j < n; ++j) xc[j] = x[j] = rng.next();
		std::vector<Complex> full = dft(xc, n, 0, -1);

		// Bin 0 packs DC (real) and Nyquist (imaginary).
		signalsmith::fft::RealFFT<double> rfft(n);
		CHECK(rfft.size() == n);
		rfft.fft(x, spectrum);
		std::vector<Complex> want(full.begin(), full.begin() + n/2);
		want[0] = Complex(full[0].real(), full[n/2].real());
		CHECK_ERROR(maxError(spectrum, want, 1), 1e-12*n, "RealFFT forward", n);
		rfft.ifft(spectrum, back);
		for (std::size_t j = 0; j < n; ++j) backC[j] = back[j];
		CHECK_ERROR(maxError(backC, xc, double(n)), 1e-12*n, "RealFFT round trip", n);

		// Bins at (k + 0.5)/n.
		signalsmith::fft::ModifiedRealFFT<double> mrfft(n);
		mrfft.fft(x, spectrum);
		CHECK_ERROR(maxError(spectrum, dft(xc, n/2, 1, -1), 1), 1e-12*n, "ModifiedRealFFT forward", n);
		mrfft.ifft(spectrum, back);
		for (std::size_t j = 0; j < n; ++j) backC[j] = back[j];
		CHECK_ERROR(maxError(backC, xc, double(n)), 1e-12*n, "ModifiedRealFFT round trip", n);
	}
}

double sineWindow(double r) {
	return std::sin(pi*r);
}

void testWindowedFft() {
	const int n = 32;
	signalsmith::spectral::WindowedFFT<double> wfft(n, sineWindow);
	CHECK(wfft.size() == n);
	Lcg rng(3);
	std::vector<double> x(n), back(n);
	std::vector<Complex> windowed(n), spectrum(n/2), backC(n), want(n);
	for (int j = 0; j < n; ++j) {
		x[j] = rng.next();
		double w = sineWindow((j + 0.5)/n);
		windowed[j] = x[j]*w;
		want[j] = x[j]*w*w;
	}
	wfft.fft(x, spectrum);
	CHECK_ERROR(maxError(spectrum, dft(windowed, n/2, 1, -1), 1), 1e-12*n, "WindowedFFT forward", n);
	// The inverse applies the window again and the 1/n scaling.
	wfft.ifft(spectrum, back);
	for (int j = 0; j < n; ++j) backC[j] = back[j];
	CHECK_ERROR(maxError(backC, want, 1), 1e-13*n, "WindowedFFT round trip", n);
}

double quadratic(double t) {
	return t*t/8 - t + 3;
}
double quintic(double t) {
	return t*t*t*t*t/1e6 - t*t/4 + 2;
}

void testDelay() {
	using namespace signalsmith::delay;
	Delay<double> linear(64); // linear interpolation, no latency
	Delay<double, InterpolatorCubic> cubic(64);
	Delay<double, InterpolatorLagrange7> lagrange(64);
	Delay<double, InterpolatorKaiserSinc20> sinc(64);
	CHECK(linear.latency == 0 && cubic.latency == 1 && lagrange.latency == 3 && sinc.latency == 9);
	const int last = 199;
	const double freq = 0.05;
	for (int t = 0; t <= last; ++t) {
		linear.write(3.0*t - 7);
		cubic.write(quadratic(t));
		lagrange.write(quintic(t));
		sinc.write(std::sin(2*pi*freq*t));
	}
	CHECK(linear.read(0) == 3.0*last - 7);
	CHECK(linear.read(17) == 3.0*(last - 17) - 7);
	CHECK(linear.read(2.25) == 3.0*(last - 2.25) - 7);
	double cubicError = 0, lagrangeError = 0, sincError = 0;
	for (double d = 0; d <= 40; d += 0.125) {
		// The cubic spline reproduces quadratics and order-7 Lagrange
		// polynomials up to degree 7, both exactly.
		cubicError = worst(cubicError, std::abs(cubic.read(d) - quadratic(last - d - cubic.latency)));
		lagrangeError = worst(lagrangeError, std::abs(lagrange.read(d) - quintic(last - d - lagrange.latency)));
		sincError = worst(sincError, std::abs(sinc.read(d) - std::sin(2*pi*freq*(last - d - sinc.latency))));
	}
	CHECK_ERROR(cubicError, 1e-12, "InterpolatorCubic on a quadratic", 0);
	CHECK_ERROR(lagrangeError, 1e-7, "InterpolatorLagrange7 on a quintic", 0);
	CHECK_ERROR(sincError, 2e-3, "InterpolatorKaiserSinc20 on a sine", freq);
}

double bessel0(double x) {
	double sum = 0, term = 1;
	for (int m = 1; term > 1e-18*sum; ++m) {
		sum += term;
		term *= x*x/(4.0*m*m);
	}
	return sum;
}

void testWindows() {
	using namespace signalsmith::windows;
	// beta = 3*pi is alpha = 3, a main lobe of 2*sqrt(1 + alpha^2) bins.
	const double beta = 3*pi;
	CHECK(std::abs(Kaiser::betaToBandwidth(beta) - 2*std::sqrt(10.0)) < 1e-12);
	CHECK(std::abs(Kaiser::bandwidthToBeta(2*std::sqrt(10.0)) - beta) < 1e-12);

	// Sample i sits at r = (2i + 1)/n - 1; upstream's series stops at terms below 1e-4.
	const int n = 16;
	std::vector<double> kaiser(n);
	Kaiser(beta).fill(kaiser, n);
	double error = 0;
	for (int i = 0; i < n; ++i) {
		double r = (2*i + 1.0)/n - 1;
		error = worst(error, std::abs(kaiser[i] - bessel0(beta*std::sqrt(1 - r*r))/bessel0(beta)));
	}
	CHECK_ERROR(error, 1e-6, "Kaiser window", beta);

	// Confined: 1 at the centre, 0 at both ends, symmetric.
	ApproximateConfinedGaussian acg(0.1);
	CHECK(std::abs(acg(0.5) - 1) < 1e-12);
	CHECK(std::abs(acg(0)) < 1e-12 && std::abs(acg(1)) < 1e-12);
	CHECK(std::abs(acg(0.3) - acg(0.7)) < 1e-12);

	// After forcing perfect reconstruction, the squares of the samples one
	// interval apart sum to 1.
	const int length = 64, interval = 16;
	std::vector<double> window(length);
	Kaiser::withBandwidth(4, true).fill(window, length);
	forcePerfectReconstruction(window, length, interval);
	error = 0;
	for (int i = 0; i < interval; ++i) {
		double sum = 0;
		for (int j = i; j < length; j += interval) sum += window[j]*window[j];
		error = worst(error, std::abs(sum - 1));
	}
	CHECK_ERROR(error, 1e-12, "forcePerfectReconstruction", interval);
}

// Butterworth through the bilinear transform: |H(f)|^2 = 1/(1 + (t/tc)^4)
// for the lowpass and (t/tc)^4/(1 + (t/tc)^4) for the highpass, t = tan(pi f).
double butterworth(double f, double cutoff, bool highpass) {
	double ratio = std::tan(pi*f)/std::tan(pi*cutoff);
	double r4 = ratio*ratio*ratio*ratio;
	return std::sqrt((highpass ? r4 : 1)/(1 + r4));
}

// The steady-state amplitude of the filter's response to a sine.
template<class Filter>
double measuredGain(Filter &filter, double f) {
	filter.reset();
	const int total = 4000, tail = 400; // the tail spans whole periods for f = k/400
	double s = 0, c = 0;
	for (int t = 0; t < total; ++t) {
		double y = filter(std::sin(2*pi*f*t));
		if (t >= total - tail) {
			s += y*std::sin(2*pi*f*t);
			c += y*std::cos(2*pi*f*t);
		}
	}
	return 2*std::sqrt(s*s + c*c)/tail;
}

void testFilters() {
	const double cutoff = 0.1;
	signalsmith::filters::BiquadStatic<double> lowpass, highpass;
	lowpass.lowpass(cutoff); // default bandwidth: Butterworth
	highpass.highpass(cutoff);
	const double freqs[] = {0.02, 0.05, 0.1, 0.3};
	for (int i = 0; i < 4; ++i) {
		double f = freqs[i];
		CHECK_ERROR(std::abs(std::abs(lowpass.response(f)) - butterworth(f, cutoff, false)), 1e-12, "lowpass response", f);
		CHECK_ERROR(std::abs(std::abs(highpass.response(f)) - butterworth(f, cutoff, true)), 1e-12, "highpass response", f);
	}
	CHECK_ERROR(std::abs(measuredGain(lowpass, 0.05) - butterworth(0.05, cutoff, false)), 1e-9, "lowpass on a sine", 0.05);
	CHECK_ERROR(std::abs(measuredGain(highpass, 0.2) - butterworth(0.2, cutoff, true)), 1e-9, "highpass on a sine", 0.2);
}

void testEnvelopes() {
	using namespace signalsmith::envelopes;
	Lcg rng(4);
	std::vector<double> in;
	BoxSum<double> boxSum(10);
	BoxFilter<double> boxFilter(8);
	boxFilter.set(5);
	PeakHold<double> peak(16);
	peak.set(5);
	int peakSize = 5;
	for (int t = 0; t < 120; ++t) {
		if (t == 50) peak.set(peakSize = 9); // regrows into the stored history
		if (t == 80) peak.set(peakSize = 3);
		// A peak that only the regrown window still holds at t = 50.
		double v = t == 43 ? 1000 : std::floor(rng.next()*100);
		in.push_back(v);
		const int count = int(in.size());

		// Integers, so the sums are exact; earlier samples are 0.
		int width = 1 + t%10;
		double sum = 0;
		for (int j = 0; j < width && j < count; ++j) sum += in[count - 1 - j];
		if (boxSum.readWrite(v, width) != sum) CHECK_ERROR(1, 0, "BoxSum", t);

		double sum5 = 0;
		for (int j = 0; j < 5 && j < count; ++j) sum5 += in[count - 1 - j];
		if (!(std::abs(boxFilter(v) - sum5/5) <= 1e-12)) CHECK_ERROR(1, 0, "BoxFilter", t);

		double max = -1e300;
		for (int j = 0; j < peakSize && j < count; ++j) max = std::max(max, in[count - 1 - j]);
		if (peak(v) != max) CHECK_ERROR(1, 0, "PeakHold", t);
	}
}

void testMix() {
	using namespace signalsmith::mix;
	// Unscaled Sylvester Hadamard: H[i][j] = (-1)^popcount(i & j).
	for (int col = 0; col < 8; ++col) {
		std::vector<double> a(8, 0), b(8, 0);
		a[col] = b[col] = 1;
		Hadamard<double, 8>::unscaledInPlace(a);
		Hadamard<double>(8).unscaledInPlace(b);
		for (int row = 0; row < 8; ++row) {
			int bits = row & col, parity = 0;
			while (bits) {
				parity ^= bits & 1;
				bits >>= 1;
			}
			double want = parity ? -1 : 1;
			if (a[row] != want || b[row] != want) CHECK_ERROR(1, 0, "Hadamard entry", row*8 + col);
		}
	}
	std::vector<double> x(8), y;
	Lcg rng(5);
	for (int i = 0; i < 8; ++i) x[i] = rng.next();
	y = x;
	Hadamard<double, 8>::inPlace(y);
	Hadamard<double, 8>::inPlace(y); // orthonormal and symmetric: its own inverse
	double error = 0;
	for (int i = 0; i < 8; ++i) error = worst(error, std::abs(y[i] - x[i]));
	CHECK_ERROR(error, 1e-15, "Hadamard twice", 8);

	// I - 2/n * ones.
	double e0[4] = {1, 0, 0, 0}, e0d[4] = {1, 0, 0, 0};
	Householder<double, 4>::inPlace(e0);
	Householder<double>(4).inPlace(e0d);
	CHECK(e0[0] == 0.5 && e0[1] == -0.5 && e0[2] == -0.5 && e0[3] == -0.5);
	CHECK(e0d[0] == 0.5 && e0d[1] == -0.5 && e0d[2] == -0.5 && e0d[3] == -0.5);
}

// Opaque, so the multiply stays inside the StopDenormals scope.
float halve(float x) {
	return x*0.5f;
}
float (*volatile opaqueHalve)(float) = halve;

void testStopDenormals() {
	volatile float smallest = std::numeric_limits<float>::min();
	CHECK(opaqueHalve(smallest) != 0); // a denormal
#if defined(__SSE__) || defined(_M_X64) || defined(__ARM_NEON) || defined(__ARM_NEON__)
	{
		signalsmith::perf::StopDenormals flushToZero;
		CHECK(opaqueHalve(smallest) == 0);
	}
#endif
#if defined(__SSE__) || defined(_M_X64)
	CHECK(opaqueHalve(smallest) != 0); // MXCSR restored; AArch64 keeps flushing (cabin.toml)
#endif
}

} // namespace

int main() {
	testComplexFft();
	testRealFft();
	testWindowedFft();
	testDelay();
	testWindows();
	testFilters();
	testEnvelopes();
	testMix();
	testStopDenormals();
	if (failures) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("signalsmith-dsp: ok\n");
	return 0;
}
