// Przeplyw ciepla 1D cpu
// u_i^{t+1} = 0.5 * (u_{i-1}^t + u_{i+1}^t)
// Uzycie:
//   cpu.exe [N] [kroki]          - goracy punkt (100) na srodku preta
//   cpu.exe plik.txt [kroki]     - stan poczatkowy wczytany z pliku
//   N      - liczba elementow preta      (domyslnie 2^24)
//   kroki  - liczba krokow czasowych     (domyslnie 1000)
// Dla malego N (<= 32) program wypisuje stan preta po kazdym kroku.
//
// Format pliku wejsciowego:
//   pierwsza linia z danymi : liczba segmentow N
//   kolejne linie           : [nr segmentu] [temperatura]
//   - segmenty numerowane od 0 do N-1, niewymienione mają temperature 0
//   - segmenty 0 i N-1 to brzegi: ich temperatura jest STALA przez calą symulacje

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>
#include <cstdlib>
#include <algorithm>

// Typ danych - float, zeby porownanie z GPU bylo uczciwe.
// Zmien na double, jesli GPU tez bedzie liczyc w double.
typedef float typ;

void drukuj(const std::vector<typ>& arr, int t) {
	std::cout << "t = " << std::left << std::setw(4) << t << ": ";
	for (size_t i = 0; i < arr.size(); i++) {
		std::cout << std::setw(5) << std::fixed << std::setprecision(1) << arr[i] << " | ";
	}
	std::cout << "\n";
}

// Czy tekst jest liczba calkowita (wtedy argument to N, a nie nazwa pliku)
bool to_liczba(const char* s) {
	char* koniec;
	std::strtoll(s, &koniec, 10);
	return koniec != s && *koniec == '\0';
}

// Wczytuje stan poczatkowy z pliku. Zwraca false przy bledzie.
bool wczytaj_plik(const char* nazwa, long long& N, std::vector<long long>& nr, std::vector<typ>& temp)
{
	std::ifstream plik(nazwa);
	if (!plik) {
		std::cerr << "Blad: nie mozna otworzyc pliku '" << nazwa << "'\n";
		return false;
	}

	N = -1;
	std::string linia;
	int nr_linii = 0;
	while (std::getline(plik, linia)) {
		nr_linii++;
		size_t hash = linia.find('#');                 // usun komentarz
		if (hash != std::string::npos) linia.erase(hash);

		std::istringstream ss(linia);
		std::string smieci;
		if (N < 0) {
			// pierwsza linia z danymi: liczba segmentow
			if (!(ss >> N)) {
				if (linia.find_first_not_of(" \t\r") == std::string::npos) continue; // pusta linia
				std::cerr << "Blad (linia " << nr_linii << "): oczekiwano liczby segmentow N\n";
				return false;
			}
			if (ss >> smieci || N < 3) {
				std::cerr << "Blad (linia " << nr_linii << "): niepoprawna liczba segmentow (N >= 3)\n";
				return false;
			}
			continue;
		}

		long long i;
		double t;
		if (!(ss >> i)) {
			if (linia.find_first_not_of(" \t\r") == std::string::npos) continue; // pusta linia
			std::cerr << "Blad (linia " << nr_linii << "): oczekiwano '[nr segmentu] [temperatura]'\n";
			return false;
		}
		if (!(ss >> t) || ss >> smieci) {
			std::cerr << "Blad (linia " << nr_linii << "): oczekiwano '[nr segmentu] [temperatura]'\n";
			return false;
		}
		if (i < 0 || i >= N) {
			std::cerr << "Blad (linia " << nr_linii << "): nr segmentu " << i
			          << " poza zakresem 0.." << N - 1 << "\n";
			return false;
		}
		nr.push_back(i);
		temp.push_back((typ)t);
	}

	if (N < 0) {
		std::cerr << "Blad: plik '" << nazwa << "' nie zawiera liczby segmentow\n";
		return false;
	}
	return true;
}

int main(int argc, char** argv)
{
	// wejscie to  albo plik, albo samo N (goracy punkt na srodku)
	const bool z_pliku = (argc > 1 && !to_liczba(argv[1]));
	long long N = 1LL << 24;                 // liczba elementow
	std::vector<long long> nr;               // numery segmentow z pliku
	std::vector<typ> temp;                   // ich temperatury

	if (z_pliku) {
		if (!wczytaj_plik(argv[1], N, nr, temp)) return 1;
	} else {
		if (argc > 1) N = std::atoll(argv[1]);
		if (N >= 3) { nr.push_back(N / 2); temp.push_back(100); } // default to goracy punkt na srodku
	}
	int kroki = (argc > 2) ? std::atoi(argv[2]) : 1000;       // liczba krokow czasowych
	if (N < 3 || kroki < 1) {
		std::cerr << "Blad: N musi byc >= 3, kroki >= 1\n";
		return 1;
	}
	const bool tryb_podgladu = (N <= 32);

	// Podwojne buforowanie: osobny bufor na krok t i na krok t+1.
	// Brzegi (u[0] i u[N-1]) maja stala temperature czyli petla ich nie przelicza,zostaja takie, jak ustawione na starcie.
	std::vector<typ> obecne(N, 0);
	std::vector<typ> nastepne(N, 0);

	// Warunek poczatkowy. Wpisujemy do OBU buforow: dla segmentow wewnetrznych
	// nie ma to znaczenia (nastepne i tak zostanie nadpisane), ale brzeg musi
	// miec te sama wartosc w obu, bo po zamianie buforow wrocilaby tam stara.
	for (size_t k = 0; k < nr.size(); k++) {
		obecne[nr[k]] = temp[k];
		nastepne[nr[k]] = temp[k];
	}
	if (tryb_podgladu) drukuj(obecne, 0);

	auto start = std::chrono::steady_clock::now();

	for (int t = 0; t < kroki; t++) {
		const typ* we = obecne.data();
		typ* wy = nastepne.data();

		for (long long i = 1; i < N - 1; i++) {
			wy[i] = typ(0.5) * (we[i - 1] + we[i + 1]);
		}

		// zamiana buforow
		obecne.swap(nastepne);

		if (tryb_podgladu) drukuj(obecne, t + 1);
	}

	auto stop = std::chrono::steady_clock::now();
	double sekundy = std::chrono::duration<double>(stop - start).count();

	double suma = 0;
	for (long long i = 0; i < N; i++) suma += obecne[i];
	typ maks = *std::max_element(obecne.begin(), obecne.end());

	std::cout << std::fixed;
	std::cout << "\n=== CPU, 1D stencil ===\n";
	std::cout << "dane wejsciowe    : " << (z_pliku ? argv[1] : "goracy punkt na srodku") << "\n";
	std::cout << "N                 : " << N << "\n";
	std::cout << "kroki             : " << kroki << "\n";
	std::cout << "typ               : " << sizeof(typ) * 8 << "-bit\n";
	if (tryb_podgladu || sekundy <= 0) {
		std::cout << "(tryb podgladu - pomiar czasu pominiety, uzyj duzego N)\n";
	} else {
		std::cout << std::setprecision(3);
		std::cout << "czas              : " << sekundy * 1000.0 << " ms\n";
		std::cout << "czas / krok       : " << sekundy * 1e6 / kroki << " us\n";
	}
	std::cout << std::setprecision(6);
	std::cout << "suma kontrolna    : " << suma << "\n";
	std::cout << "max temperatura   : " << maks << "\n";

	return 0;
}
