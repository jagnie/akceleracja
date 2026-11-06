#include <iostream>
#include <vector>
#include <iomanip>

void drukuj(const std::vector<double>& arr, int t) {
	std::cout << "t = " << std::left << std::setw(4) << t << ": ";
	for (double val : arr) {
		std::cout << std::setw(5) << std::fixed << std::setprecision(1) << val << " | ";
	}
	std::cout << "\n";
}


int main()
{
	int N = 20; //liczba elementow
	int czas = 100; //liczba krokow

	//mamy bufor dla wartości w obecnym kroku t
	//i osobny bufor dla wartości w t+1
	std::vector<double> obecne(N, 0.0);
	std::vector<double> nastepne(N, 0.0);

	//dajmy przykładową temperaturę np na środek
	obecne[N / 2] = 100.0;
	drukuj(obecne, 0);

	for (int t = 0; t < czas; t++) {
		for (int i = 1; i < N - 1; i++) {
			nastepne[i] = 0.5 * (obecne[i - 1] + obecne[i + 1]);
		}

		//na brzegach dajemy np zero
		nastepne[0] = 0.0;
		nastepne[N - 1] = 0.0;

		//zamieniamy bufory
		obecne.swap(nastepne);
		drukuj(obecne, t + 1);
	}

	return 0;
}

