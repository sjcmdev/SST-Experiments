# Jak działa GPU-Experiment — od piksela do architektury

*Kompletne wyjaśnienie systemu, który zbudowałeś w pięciu fazach.*

---

## Prolog — zanim zaczniesz czytać

Ten dokument nie jest instrukcją. Nie mówi ci co masz robić. Mówi ci **dlaczego to co zrobiłeś działa**. Każda sekcja zakłada, że kod z poprzednich faz istnieje i funkcjonuje, i próbuje odpowiedzieć na pytanie, które powinno się rodzić przy każdym wciśnięciu przycisku „Uruchom": co tak naprawdę się wtedy dzieje?

Pisałem to z myślą o osobie, która wykonała instrukcje, zobaczyła że kod działa, ale ma poczucie że rozumie *co* napisała, nie do końca *dlaczego* to ma sens. Ten dokument ma wypełnić tę lukę. Jest długi, bo każda techniczna koncepcja, z którą się tu zetknąłeś, zasługuje na więcej niż jednolinijkowe wyjaśnienie.

Zaczynamy od matematyki, idziemy przez hardware, architekturę, wątki, kompilację w locie i kończmy na tym co widać na ekranie — renderowanym wykresie, który jest ostatecznym produktem łańcucha zdarzeń obejmującego kilka milionów operacji na sekundę.

---

## Rozdział 1 — Matematyka, która nie jest celem

Projekt obraca się wokół splotu dwóch sygnałów, ale splot sam w sobie jest tu pretekstem. Żeby jednak rozumieć po co cokolwiek uruchamiacie na GPU, warto przez chwilę zastanowić się nad tym co liczycie.

Sygnał dyskretny to po prostu ciąg liczb. Jeśli mierzysz temperatury co sekundę przez godzinę, masz ciąg 3600 liczb — to sygnał. Jeśli próbkujesz dźwięk 44100 razy na sekundę, każda sekunda to ciąg 44100 próbek. W tym projekcie sygnał to matematyczna funkcja czasu — Gaussian, prostokąt, sinus — próbkowana w N równomiernie rozłożonych punktach na odcinku `[0, 1]` sekundy.

Splot liniowy dwóch sygnałów A i B w punkcie n jest zdefiniowany jako:

```
C[n] = Σ(k=0 do N-1) A[k] · B[n - k]
```

Intuicja: wyobraź sobie, że odwracasz sygnał B, przesuwasz go po osi czasu, i w każdym punkcie n liczysz iloczyn skalarny A i przesuniętego B. Matematycznie splot reprezentuje "ile A jest w B" w każdym przesunięciu. W przetwarzaniu sygnałów to operacja fundamentalna — filtry, korelacje krzyżowe, odpowiedź impulsowa systemu, identyfikacja echa — wszystko to splot w różnych postaciach.

Dlaczego splot jest idealny jako ćwiczenie GPU? Bo obliczenie każdego punktu wyjściowego `C[n]` jest **niezależne** od wszystkich pozostałych punktów. Żeby policzyć `C[100]`, nie potrzebujesz wcześniej obliczonego `C[99]` ani `C[101]`. Każdy punkt to oddzielna pętla przez N elementów A i B — i każdy może być obliczony przez osobny wątek GPU jednocześnie z wszystkimi innymi. To jest definicja *embarrassingly parallel problem* — problemu, który sam woła o równoległość.

Dla N=4096 masz 4096 niezależnych pętli, każda sumuje 4096 iloczynów — razem ponad 16 milionów operacji zmiennoprzecinkowych. Na CPU seryjnym zajmuje to kilkadziesiąt milisekund. Nowoczesny GPU ma kilka tysięcy procesorów CUDA, które mogą robić to jednocześnie — i zajmuje to ułamek milisekundy. Właśnie tę przepaść chciałeś zbadać.

---

## Rozdział 2 — GPU: architektura, którą musisz mentalnie zobaczyć

Zanim zrozumiesz jak działa twój kod, musisz mieć poprawny model mentylny tego czym jest GPU. To nie jest „szybszy CPU". To jest zupełnie inne urządzenie z fundamentalnie inną filozofią projektową.

CPU jest zaprojektowany żeby jak najszybciej wykonać **jeden** ciąg instrukcji. Dlatego ma duże pamięci podręczne (cache), skomplikowaną predykcję rozgałęzień, potoki wykonania poza kolejnością, i relatywnie niewielką liczbę rdzeni — współczesne procesory desktopowe mają ich kilkanaście do kilkudziesięciu. Każdy rdzeń jest potężny, wyspecjalizowany w szybkim sekwencyjnym wykonaniu.

GPU jest zaprojektowany żeby jak najszybciej wykonać **tysiące** ciągów instrukcji **jednocześnie**. Kosztem jest to, że każdy pojedynczy rdzeń GPU jest znacznie prostszy i słabszy niż rdzeń CPU. RTX 2070 Super, na którym prawdopodobnie testowałeś ten kod, ma 2560 rdzeni CUDA — ale każdy z nich to uproszczona jednostka obliczeniowa, która błyszczy gdy wszyscy robią to samo.

Fundamentalna jednostka organizacyjna w CUDA to **wątek** (thread). Wątki są grupowane w **bloki** (thread blocks), a bloki tworzą **siatkę** (grid). Gdy uruchamiasz kernel, mówisz GPU: „wykonaj tę funkcję dla tysiąca (lub miliona) wątków jednocześnie". Każdy wątek wie gdzie jest w tej hierarchii przez wbudowane zmienne `blockIdx`, `threadIdx`, `blockDim`, `gridDim`.

W kernelu splotu:

```cuda
int n = blockDim.x * blockIdx.x + threadIdx.x;
if (n >= N) return;
```

Ta linia jest sercem całego równoległego obliczenia. Każdy z tysięcy wątków oblicza swój unikalny indeks `n` — i od tego momentu każdy robi dokładnie to samo dla *innego* elementu tablicy wyjściowej. Wątek 0 liczy C[0], wątek 1 liczy C[1], itd. Nie ma komunikacji między wątkami, nie ma synchronizacji, nie ma zależności — każdy jest kompletnie autonomiczny.

Wątki w jednym bloku dzielą **shared memory** — małą, bardzo szybką pamięć (kilkadziesiąt kilobajtów) dostępną tylko dla wątków danego bloku. W naszym kernelu splotu nie używamy shared memory, bo dostęp do A i B jest w pełni losowy dla każdego wątku i nie można go efektywnie buforować. Ale shared memory jest kluczowa w bardziej zaawansowanych kernelach (np. mnożenie macierzy).

Wątki są harmonogramowane przez **SM** (Streaming Multiprocessor) — to fizyczny cluster rdzeni CUDA. RTX 2070 Super ma 40 SM, każdy z 64 rdzeniami CUDA (razem 2560). SM harmonogramuje wątki w grupach 32 — taka grupa to **warp**. Wszystkie wątki w warpie wykonują tę samą instrukcję jednocześnie. Jeśli różne wątki w warpie pójdą różnymi ścieżkami (if-else), SM musi serialnie wykonać obie ścieżki — to się nazywa **warp divergence** i jest najczęstszą przyczyną nieefektywności kerneli z warunkami.

W kernelu splotu mamy jeden warunek: `if (n >= N) return`. Wątki które przekraczają N po prostu kończą bez działania. Wątki w ostatnim bloku, które są "za N", nie robią nic — to trochę marnotrawstwo, ale niezmiernie małe (maksymalnie 255 z 256 wątków w bloku, czyli jeden z setek bloków).

Rozumienie tej architektury jest kluczem do zrozumienia dlaczego GPU jest tak szybkie dla pewnych zadań i tak powolne dla innych. Obliczenia gdzie każdy element jest niezależny i operacje są jednorodne — GPU świeci. Obliczenia gdzie elementy zależą od siebie, gdzie jest dużo rozgałęzień, gdzie jest dużo komunikacji — GPU cierpi.

---

## Rozdział 3 — Pamięć: skąd dane i dokąd jadą

GPU ma swoją własną pamięć — **VRAM**. To oddzielna pamięć podłączona do karty graficznej przez szeroki magistrala (RTX 2070 Super: 256-bit). CPU nie może bezpośrednio pisać do VRAM ani czytać z niej. To izolowane środowisko.

Zanim GPU może przetworzyć dane, muszą być **skopiowane** z pamięci systemu (RAM) do VRAM. Zanim zobaczysz wyniki, muszą być skopiowane z VRAM z powrotem do RAM. To jest koszt który płacisz za każde obliczenie GPU — nazywa się **transfer host-device** (H2D) i **transfer device-host** (D2H), gdzie „host" to CPU+RAM a „device" to GPU+VRAM.

W Fazie 1 i 2 widziałeś ten koszt bezpośrednio w statystykach: `transferToGpuMs` i `transferFromGpuMs`. Dla N=4096 i danych `double` (8 bajtów) to `4096 × 8 = 32768` bajtów — 32 KB. Przy przepustowości PCIe 4.0 rzędu 10 GB/s to trwa ułamek mikrosekundy. Ale dla N=65536 to już 512 KB, a dla rzeczywistych zastosowań (np. przetwarzanie obrazów 4K: 8 MB na kanał) transfer staje się wąskim gardłem.

Właśnie dlatego Faza 4 eliminuje transfery H2D dla sygnałów: sygnały są teraz generowane **bezpośrednio na GPU**, przez kernel `generateSignalA`. Nie ma po co kopiować sygnału Gaussowskiego z CPU na GPU skoro GPU może go obliczyć sam — i robi to o wiele szybciej niż transfer przez PCIe.

Paradoksalnie, teraz masz więcej transferów D2H (czytasz z powrotem sygnał A, sygnał B i wynik splotu żeby pokazać je na wykresie), ale te transfery są narzucone przez potrzebę wizualizacji — nie przez potrzebę obliczeń. W produkcyjnym systemie który nie wyświetla wyników, całe obliczenie — generacja sygnałów i splot — mogłoby się odbyć bez jakiegokolwiek transferu przez PCIe.

Funkcje zarządzania pamięcią GPU które widzisz w kodzie to:

**`cudaMalloc`** — alokuje pamięć w VRAM, podobnie jak `malloc` alokuje w RAM. Zwraca wskaźnik — ale ten wskaźnik jest **wskaźnikiem urządzenia**. Odwołanie się do niego z kodu CPU (np. `*ptr = 42`) to natychmiastowy crash lub UB, bo CPU nie może adresować VRAM.

**`cudaMemcpy`** — kopiuje dane między RAM a VRAM (lub w obrębie VRAM). Czwarty parametr mówi w którą stronę: `cudaMemcpyHostToDevice` (RAM→VRAM), `cudaMemcpyDeviceToHost` (VRAM→RAM), `cudaMemcpyDeviceToDevice` (VRAM→VRAM).

**`cudaFree`** — zwalnia pamięć w VRAM. Zapomnienie o tym to wyciek pamięci GPU — VRAM jest znacznie cenniejsza (mniej jej) niż RAM.

---

## Rozdział 4 — NVCC i NVRTC: dwie drogi kompilacji

To jest jeden z kluczowych konceptów całego projektu i zasługuje na szczegółowe wyjaśnienie, bo mylenie tych dwóch systemów kompilacji to źródło nieustannych nieporozumień.

### NVCC — kompilator czasu budowania

NVCC (NVIDIA CUDA Compiler) to kompilator który uruchamia się gdy budujesz projekt — kiedy klikasz Build w Visual Studio. Przetwarza pliki `.cu` i robi z nimi dwie rzeczy jednocześnie: kod hosta (fragmenty `void runConvolution(...)` bez `__global__`) przekazuje do MSVC (cl.exe) żeby skompilował to jak zwykłe C++, a kod urządzenia (funkcje `__global__`) kompiluje do PTX i/lub do binarnego kodu dla konkretnego GPU.

Wynik pracy NVCC to plik `.obj`, który trafia do linkera razem z innymi `.obj` plikami i razem tworzą plik wykonywalny. Funkcja `__global__ void convolution(...)` z Fazy 1 i 2 jest wbudowana w ten plik wykonywalny — jest skompilowana, zoptymalizowana, gotowa do uruchomienia.

Zaletą tego podejścia jest prostota i wydajność: NVCC może agresywnie optymalizować kod, wie dokładnie dla jakiego GPU go kompiluje, i wynik jest binarny — nie ma żadnego opóźnienia przed pierwszym uruchomieniem.

Wadą jest to, że jeśli chcesz zmienić cokolwiek w kernelu, musisz przebudować cały projekt, uruchomić ponownie, i dopiero zobaczyć wynik. Dla iteracyjnego procesu tworzenia sygnałów — gdzie chcesz szybko modyfikować matematykę i widzieć wynik — to zbyt wolne.

### NVRTC — kompilator czasu uruchomienia

NVRTC (NVIDIA Runtime Compilation) to biblioteka C++, która pozwala skompilować kod CUDA C++ **wewnątrz działającej aplikacji**, w trakcie jej działania. Nie w trakcie budowania — w trakcie działania.

Gdy użytkownik klika „Przeładuj kernel", aplikacja:
1. Czyta plik `kernels/convolution.cu` z dysku (lub generuje string z grafu węzłów)
2. Przekazuje ten string do `nvrtcCreateProgram` + `nvrtcCompileProgram`
3. NVRTC kompiluje kod CUDA C++ do **PTX** — pośredniej reprezentacji
4. Aplikacja pobiera PTX przez `nvrtcGetPTX`
5. Przekazuje PTX do `cuModuleLoadData` — sterownik NVIDIA kompiluje PTX do kodu maszynowego dla konkretnego GPU
6. Aplikacja pobiera uchwyt funkcji przez `cuModuleGetFunction`
7. Przy następnym uruchomieniu kernela używa nowego uchwytu

Cały ten proces trwa 0.3–2 sekundy i dzieje się **bez zatrzymywania ani restartowania aplikacji**. Okno ImGui dalej się renderuje, wykresy dalej są widoczne. Tylko wynik splotu zostaje zaktualizowany po zakończeniu kompilacji.

### PTX — język pośredni GPU

PTX (Parallel Thread eXecution) to assembler GPU, ale na poziomie abstrakcji wyższym niż kod maszynowy. Jest niezależny od konkretnej generacji GPU — PTX skompilowany dla compute capability 7.x będzie działać na wszystkich GPU tej generacji i nowszych. Dopiero sterownik NVIDIA (w `cuModuleLoadData`) kompiluje PTX do rzeczywistego kodu maszynowego dla konkretnej karty.

To jest podobna rola jak LLVM IR dla kompilatorów CPU — pośrednia reprezentacja która można przenosić między platformami i którą może generować wiele front-endów (NVCC, NVRTC, inne).

Dlaczego opcja `--gpu-architecture=compute_75` (a nie `sm_75`) w NVRTC? Wartość `compute_75` mówi: generuj PTX kompatybilny z compute capability 7.5. Opcja `sm_75` powiedziałaby: generuj kod binarny bezpośrednio dla sm_75. Dla NVRTC używamy PTX (prefiks `compute_`), bo PTX jest przenośny i to sterownik finalizuje kompilację do kodu maszynowego.

### Dlaczego extern "C" jest obowiązkowe

Gdy NVRTC kompiluje CUDA C++, stosuje **C++ name mangling** — transformację nazw funkcji w kodzie obiektowym. Funkcja `void convolution(...)` staje się czymś w stylu `_Z11convolutioniPdPKdS0_` po name manglingU. Gdy potem próbujesz znaleźć tę funkcję przez `cuModuleGetFunction(module, "convolution")`, szukasz literalnej nazwy `"convolution"` — i jej nie ma, bo NVRTC ją skompilował pod mangled name.

`extern "C"` wyłącza name mangling dla tej funkcji. Funkcja jest widoczna pod swoją oryginalną nazwą i `cuModuleGetFunction` może ją znaleźć.

---

## Rozdział 5 — Driver API i Runtime API: dwie twarze CUDA

CUDA udostępnia dwa API do zarządzania GPU: **Runtime API** i **Driver API**. Przez pierwsze trzy fazy projektu używałeś głównie Runtime API, nie wiedząc o tym że istnieje drugie. W Fazie 2 wprowadziliśmy Driver API i ważne jest żebyś rozumiał różnicę.

### Runtime API — wygodna abstrrakcja

Runtime API to wyższy poziom abstrakcji. Funkcje zaczynają się od `cuda` (małe litery): `cudaMalloc`, `cudaMemcpy`, `cudaDeviceSynchronize`, `cudaGetLastError`. Są łatwe w użyciu i pokrywają 95% potrzeb.

Kluczowe jest to że Runtime API **zarządza kontekstem CUDA automatycznie**. Kontekst CUDA to środowisko wykonawcze — analogia do procesu w systemie operacyjnym. Każde urządzenie CUDA ma swój **primary context** (główny kontekst) i Runtime API inicjalizuje go i ustawia jako aktywny automatycznie przy pierwszym wywołaniu dowolnej funkcji `cuda*`.

Dlatego w kodzie nigdy nie widzisz `cuInit()` ani `cuCtxCreate()` przed wywołaniami Runtime API — dzieje się to za kulisami.

### Driver API — bezpośredni dostęp

Driver API to niższy poziom. Funkcje zaczynają się od `cu` (małe litery, bez `da`): `cuModuleLoadData`, `cuModuleGetFunction`, `cuLaunchKernel`. Dają pełną kontrolę nad kontekstem, modułami, funkcjami.

Driver API jest wymagane do pracy z NVRTC, bo `cuModuleLoadData` (ładowanie PTX do GPU) i `cuModuleGetFunction` (pobieranie uchwytu do skompilowanej funkcji) nie mają odpowiedników w Runtime API. Runtime API nie potrafi dynamicznie ładować skompilowanych modułów.

### Koegzystencja i kolejność inicjalizacji

Oba API współdzielą ten sam kontekst CUDA — primary context urządzenia. Możesz mieszać wywołania `cudaMalloc` z `cuModuleLoadData` w tym samym programie i działają na tej samej pamięci i tym samym kontekście.

Jedyny warunek to **kolejność**: `cuInit(0)` musi być wywołane przed dowolną funkcją Driver API (ale może być po Runtime API). I kontekst musi być aktywny (a jest, bo Runtime API go stworzył). Dlatego w kodzie widzisz:

```cpp
queryCudaDevice(state.deviceInfo);  // Pierwsze wywołanie Runtime API → tworzy primary context
initCudaDriver();                   // cuInit(0) → aktywuje Driver API
kernelMgr.compile(...);            // cuModuleLoadData działa bo kontekst jest aktywny
```

Gdybyś odwrócił kolejność — wywołał `cuModuleLoadData` przed jakimkolwiek Runtime API — dostałbyś błąd `CUDA_ERROR_INVALID_CONTEXT`, bo kontekst nie byłby jeszcze stworzony.

### cuLaunchKernel zamiast <<<>>>

Składnia `<<<grid, block>>>` to rozszerzenie językowe wprowadzone przez NVCC. Kompilator zamienieja na wywołanie `cudaLaunchKernel` lub podobne wywołanie wewnętrzne Runtime API. Ta składnia działa tylko w plikach `.cu` kompilowanych przez NVCC.

Gdy masz `CUfunction` uzyskaną z `cuModuleGetFunction`, nie możesz użyć `<<<>>>` — bo nie masz statycznej funkcji którą NVCC mógłby oznaczyć jako `__global__`. Zamiast tego używasz `cuLaunchKernel`:

```cpp
void* args[] = { &d_A, &d_B, &d_C, &N };
cuLaunchKernel(func, gridDim, 1, 1, blockDim, 1, 1, 0, 0, args, nullptr);
```

Tablica `args` to tablica wskaźników do argumentów kernela. Jeśli pierwszy argument kernela to `const double* A`, to `args[0]` to `&d_A` — wskaźnik do wskaźnika urządzenia. Kernel API przeczyta wartość `d_A` (adres w VRAM), i to właśnie otrzyma kernel jako swój pierwszy argument.

To subtelne ale ważne: `args[0]` nie jest wskaźnikiem do danych — jest wskaźnikiem do wskaźnika do danych. Warstwa pośrednia wynika z faktu, że Driver API musi znać gdzie są argumenty (w pamięci CPU), żeby je skopiować do rejestrów GPU przed startem kernela.

---

## Rozdział 6 — Trzy kompilatory, jeden projekt

Być może jedną z najbardziej zaskakujących rzeczy w tym projekcie jest to, że różne pliki `.cpp` i `.cu` są kompilowane przez **różne kompilatory**:

**MSVC (cl.exe)** kompiluje:
- `main.cpp`
- `app.cpp`
- `gui.cpp`
- `kernel_manager.cpp`
- `gpu_worker.cpp`
- `signal_graph.cpp`
- `code_gen.cpp`
- `imnodes.cpp`
- Pliki ImGui, ImPlot, GLAD

**NVCC** kompiluje:
- `cuda_impl.cu`

**NVRTC** (w runtime, nie w czasie budowania) kompiluje:
- `kernels/convolution.cu` (wczytany jako string)
- Wygenerowane stringi `generateSignalA` i `generateSignalB`

I wszystko to musi razem działać. Pliki MSVC i NVCC produkują `.obj` pliki, które linker łączy w jeden wykonywalny. Runtime NVRTC produkuje PTX, który jest ładowany do pamięci GPU przez sterownik NVIDIA.

Kluczowe jest to, że `kernel_manager.cpp` — skompilowany przez MSVC — może includować `cuda.h` i `nvrtc.h` i wywoływać funkcje Driver API i NVRTC. To są zwykłe biblioteki C++ z nagłówkami C. Nie wymagają NVCC. NVCC jest potrzebny **tylko** do kompilacji plików zawierających kod urządzenia — funkcje `__global__`, wywołania kerneli przez `<<<>>>`, wbudowane zmienne `threadIdx` itp.

`gpu_worker.cpp` includuje `cuda_runtime_api.h` i wywołuje `cudaStreamCreate` — to też może robić MSVC, bo Runtime API to biblioteka C z nagłówkami. Działa.

Ta separacja jest celowa i elegancka: kod zarządzający GPU (alokacja, transfery, moduły) jest zwykłym C++ i może być edytowany bez rebuilda przez NVCC. Tylko rzeczy które faktycznie *biegną* na GPU (funkcje `__global__`) wymagają NVCC — i nawet te, w Fazach 2–5, są kompilowane przez NVRTC w runtime.

---

## Rozdział 7 — Faza 1 szczegółowo: pierwszy kontakt z GPU

Faza 1 to celowe uproszczenie — wszystko synchroniczne, wszystko na jednym wątku, żeby zobaczyć czysty GPU workflow bez żadnych komplikacji współbieżności.

Przepływ danych jest następujący. W `appInit` sygnały A (Gaussian) i B (Rectangle) są obliczane na CPU i przechowywane w wektorach `std::vector<double>`. Parametry — `mu`, `sigma` dla Gaussiana, `t0`, `t1` dla prostokąta — są definiowane jako stałe, potem (po modyfikacji) są edytowalne przez UI.

Gdy użytkownik zmienia parametr, ustawia się flaga `dirty = true`. W głównej pętli renderowania, jeśli `dirty == true`, wywoływane jest `runConvolution`. Ta funkcja:

1. Alokuje w VRAM trzy bufory: `d_A`, `d_B`, `d_C` — po N wartości `double` każdy
2. Kopiuje CPU→GPU (`cudaMemcpy` H2D) sygnał A i B do `d_A` i `d_B`
3. Uruchamia kernel splotu
4. Synchronizuje CPU z GPU (`cudaDeviceSynchronize`) — czeka aż GPU skończy
5. Kopiuje GPU→CPU wynik do `state.convOutput`
6. Zwalnia `d_A`, `d_B`, `d_C`
7. Oblicza CPU reference dla walidacji
8. Liczy `maxAbsError`

Kroki 3–4 to serce: kernel jest uruchamiany i CPU blokuje się czekając na wynik. Dla małego N jest to niezauważalne (kilka milisekund). Ale dla N=65536 CPU czekałby może 100ms — a przez ten czas okno ImGui nie reaguje na żadne zdarzenia. To problem, który Faza 3 rozwiązuje.

Walidacja `maxAbsError < 1e-9` to kluczowy test poprawności. GPU wykonuje arytmetykę zmiennoprzecinkową na 64-bitowych liczbach podwójnej precyzji (`double`). CPU reference robi dokładnie to samo, ale w innej kolejności sumowania. Numeryczne różnice wynikające z różnej kolejności operacji mogą dawać błędy rzędu `1e-15` — stąd tolerancja `1e-9` jest luzowna i nie powinna być nigdy przekroczona dla prostego splotu.

Jeśli `maxAbsError` jest duże (np. `1e-3`), oznacza to błąd w implementacji — najczęściej zły rozmiar siatki, zły indeks w kernelu, albo problem z transferem danych.

**Dirty flag** to wzorzec który pojawi się w każdej fazie. Zamiast przeliczać wynik przy każdym renderze klatki (60 fps × kilka ms = 360 ms/s spędzone na obliczeniach zamiast na renderowaniu), przeliczasz tylko gdy coś się zmieniło. To fundamentalny wzorzec w każdym interaktywnym systemie z kosztownymi obliczeniami.

### Jak działa ImGui i ImPlot

ImGui to biblioteka immediate mode GUI — to znaczy, że UI nie przechowuje swojego stanu. Przy każdym renderze klatki wywołujesz ImGui::Button, ImGui::SliderFloat itp. i ImGui natychmiast sprawdza czy użytkownik kliknął, czy przesunął suwak — i zwraca bool. Nie ma callbacków, nie ma event listenera — jest synchroniczne pytanie „czy ta kontrolka jest aktywna?" przy każdym renderze.

```cpp
if (ImGui::SliderFloat("mu", &state.gaussMu, 0.0f, 1.0f)) {
    // Wchodzisz tu TYLKO gdy suwak się poruszył w tej klatce
    regenerateSignalA(state);
    state.dirty = true;
}
```

ImPlot to biblioteka do wykresów zbudowana na szczycie ImGui. `ImPlot::PlotLine("Sygnał A", xData, yData, N)` renderuje linię łamaną przez N punktów danych. Dane są czytane z twojego wektora `state.signalA` — jeśli go zaktualizujesz, przy następnym renderze klatki wykres automatycznie pokaże nowe dane.

OpenGL i GLFW to infrastruktura poniżej ImGui: GLFW zarządza oknem i eventami (klawiatura, mysz), OpenGL renderuje geometrię na ekranie. ImGui_ImplOpenGL3 i ImGui_ImplGlfw to backendy które łączą ImGui z GLFW i OpenGL. Twój kod nigdy bezpośrednio nie pisze do OpenGL — robi to ImGui.

---

## Rozdział 8 — Faza 2 szczegółowo: kompilacja jako funkcja aplikacji

Faza 2 zmienia jedno fundamentalne założenie: kernel GPU nie jest już wbudowany w aplikację — jest wczytywany z pliku, kompilowany w trakcie działania, i może być wymieniony bez restartu.

Jest to zmiana filozoficzna. W Fazie 1 kernel jest artefaktem procesu budowania — tak samo stały jak kod zarządzający oknem ImGui. W Fazie 2 kernel jest zasobem — tak samo dynamiczny jak plik tekstowy który możesz edytować.

### KernelManager jako maszyna stanu

`KernelManager` to prosta maszyna stanu z dwoma stanami: "nie skompilowany" (`isReady() == false`) i "skompilowany" (`isReady() == true`). Przejście z pierwszego do drugiego to `compile()`, które może się nie powieść (błąd składni w kernelu). Przejście z powrotem do "nie skompilowany" to `unloadModule()`, które zwalnia załadowany PTX z VRAM.

Krytyczna własność `KernelManager`: jeśli `compile()` się nie powiedzie (błąd NVRTC), **stary moduł nadal działa**. To dlatego, że `unloadModule()` jest wywoływane dopiero po udanej kompilacji do PTX i przed `cuModuleLoadData`. Jeśli kompilacja zawiedzie, wracamy z `compile()` wcześnie, bez dotykania `m_module`. Stary `CUmodule` jest nadal załadowany i `getFunction()` nadal zwraca sprawny uchwyt.

To "bezpieczne" zachowanie przy błędzie (fail-safe) jest fundamentem dobrego hot-reloadu: użytkownik może wprowadzić błąd składni w kernelu, kliknąć "Przeładuj", zobaczyć log błędu, i stary wynik nadal jest widoczny. Aplikacja nie crashuje, nie ma pustego okna.

### Czas kompilacji NVRTC

Pierwsza kompilacja przez NVRTC jest wolniejsza (1–3 sekundy) bo NVRTC inicjalizuje swoje zasoby. Kolejne kompilacje tego samego lub podobnego kodu są szybsze (0.3–1 sekunda). Te czasy mogą zaskoczyć jeśli przywykłeś do kompilacji NVCC dla prostych kerneli (ułamki sekundy), ale NVRTC ma wyższy overhead inicjalizacji.

Czas kompilacji rośnie z złożonością kodu. Dla prostego kernela splotu (30 linii) jest to rzędu 0.3–0.5 s. Dla złożonego kodu z wygenerowanymi sygnałami (Faza 4) może być 0.5–2 s. To jest akceptowalne dla ręcznego hot-reloadu, ale za wolne żeby robić to co klatkę.

### Co wyświetla log NVRTC

Gdy `nvrtcCompileProgram` zwróci błąd, log kompilacji zawiera komunikat bardzo podobny do tego co widziałeś od MSVC czy g++. Na przykład:

```
kernels/convolution.cu(12): error: expected a ";"
   sum + A[k] * B[idx]
       ^
```

To jest kod błędu NVRTC który mówi: linia 12, brak średnika. Dokładnie ta sama informacja co od normalnego kompilatora C++. NVRTC to pełnoprawny kompilator — tylko uruchamiany w runtime zamiast w czasie budowania.

Nawet przy sukcesie warto sprawdzać log — NVRTC może raportować **ostrzeżenia** (warnings) bez zatrzymywania kompilacji. Ostrzeżenia jak `warning: variable X is declared but not used` pojawiają się i warto je widzieć.

---

## Rozdział 9 — Faza 3 szczegółowo: architektura wielowątkowa

Faza 3 to największy skok architektoniczny w całym projekcie — nie dlatego że jest trudna technicznie, ale dlatego że wymaga zmiany w sposobie myślenia o przepływie programu.

### Dwa wątki, dwa światy

Przed Fazą 3 aplikacja ma jeden wątek. Ten wątek robi wszystko: sprawdza eventy (klawiatura, mysz), renderuje UI, oblicza splot na GPU, sprawdza wyniki, rysuje wykresy. Gdy obliczenie GPU trwa, ten wątek **stoi i czeka**. Dla N=4096 to kilka milisekund — prawie niezauważalne. Dla N=65536 to kilkaset milisekund — okno freezuje.

Po Fazie 3 aplikacja ma dwa wątki. **Wątek główny** (main thread) jest wyłącznie odpowiedzialny za UI: zbiera eventy, renderuje ImGui, aktualizuje wykresy. Nigdy nie czeka na GPU — jeśli nie ma jeszcze wyników, po prostu renderuje "Obliczanie..." i idzie dalej.

**Wątek GPU** (GpuWorkerThread) leży śpiący przez większość czasu. Gdy dostanie zadanie, budzi się, wykonuje całe obliczenie GPU (cudaMalloc, transfer, kernele, synchronizacja, transfer z powrotem), pakuje wynik i zasypia z powrotem. Cały ten czas wątek główny swobodnie renderuje UI.

### Komunikacja przez promise/future

Jak wątek główny przekazuje zadanie wątkowi GPU? Przez `std::mutex` + `std::condition_variable` + `std::queue<GpuTask>`. Jak dostaje wynik z powrotem? Przez `std::promise<AsyncConvResult>` / `std::future<AsyncConvResult>`.

`std::promise` i `std::future` to mechanizm jednorazowego przesłania wartości między wątkami. Producent (wątek główny) tworzy promise, wyciąga z niego future, i przekazuje promise do wątku roboczego. Konsument (wątek GPU) wykonuje obliczenie i wywołuje `promise.set_value(result)`. Od tego momentu future po stronie wątku głównego jest "gotowy" — `wait_for(0ms)` zwraca `future_status::ready`.

Wątek główny **nie blokuje się** czekając na future. Zamiast tego każdą klatkę sprawdza:

```cpp
if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    auto result = future.get();  // natychmiastowe — wynik jest już gotowy
    updatePlots(result);
}
```

`wait_for(0ms)` to pytanie "czy wynik jest już gotowy?" bez żadnego czekania. Jeśli nie — wątek główny idzie dalej i renderuje kolejną klatkę. Jeśli tak — wynik jest dostępny natychmiastowo.

### Bezpieczeństwo danych

Gdy wątek GPU oblicza splot, wątek główny nadal renderuje UI — użytkownik może zmieniać parametry sygnałów. To powoduje pytanie: co jeśli parametry się zmienią podczas gdy wątek GPU oblicza coś ze starymi parametrami?

Odpowiedź jest prosta: `GpuTask` przechowuje **kopię** danych sygnałów (w Fazach 1–3) lub **kopię grafów węzłów** (w Fazach 4–5). Kopia jest wykonana w momencie `submit()`, zanim wątek GPU zacznie cokolwiek robić. Jeśli użytkownik zmieni parametry po `submit()`, zmiana dotyczy tylko `state.graphA` w AppState — kopia w GpuTask jest niezmieniona.

Gdy wątek GPU skończy z tymi starymi danymi, w następnej klatce UI `appPollAndSubmit` zobaczy `!computing && dirty && kernelReady` — i złoży nowe zadanie z nowymi parametrami. Wynik dla starych parametrów zostanie wyświetlony przez ułamek sekundy, a potem zastąpiony nowym. To jest poprawne zachowanie.

### Shutdown: ostatnia kwestia

Destrukcja `GpuWorkerThread` bez wcześniejszego `join()` to `std::terminate()` — aplikacja crashuje. Dlatego `shutdown()` musi być wywołane **przed** zniszczeniem AppState. I AppState musi być zniszczone **przed** zniszczeniem kontekstu CUDA (by wątek GPU nie próbował działać na już zniszczonym kontekście).

Stąd kolejność w `main()`:
1. `state.gpuWorker.shutdown()` — dołącz do wątku GPU, czekaj aż skończy
2. ImNodes, ImPlot, ImGui cleanup
3. GLFW cleanup
4. (Kontekst CUDA jest niszczony automatycznie przez CUDA Runtime przy zamknięciu procesu)

---

## Rozdział 10 — Grafy węzłów: co to jest i dlaczego to działa

Node editor to interfejs wizualnego programowania. Zamiast pisać `Gaussian(mu=0.3, sigma=0.05)`, rysujesz prostokąt z etykietą "Gaussian", wyciągasz z niego linię do prostokąta "Scale(2.0)", i ta z kolei leci do "Output". Efekt jest identyczny — opisałeś matematyczną funkcję — ale bez pisania kodu.

To co powstaje to **DAG** — Directed Acyclic Graph, skierowany graf acykliczny. "Skierowany" bo linie między węzłami mają kierunek: dane płyną od generatorów do operatorów do wyjścia. "Acykliczny" bo nie ma pętli — wartości zawsze płyną "do przodu" bez cofania się. Gdybyś podłączył Sum z powrotem do jednego ze swoich wejść (przez inny węzeł), miałbyś cykl — i nie byłoby sensu obliczeniowego jak to ewaluować.

### Dlaczego rekurencja jest naturalnym narzędziem

Zarówno ewaluacja grafu na CPU (`evaluateSignal`) jak i generacja kodu CUDA (`generateExpr`) są rekurencyjne. I to nie przypadek — rekurencja jest naturalną strukturą dla pracy z drzewami i grafami.

Ewaluacja węzła zależy od ewaluacji jego wejść. Ewaluacja wejść zależy od ich wejść. Baza rekurencji to węzły bez wejść — generatory — które obliczają wartość bezpośrednio z `t`. To klasyczny wzorzec **bottom-up evaluation** (od dołu do góry) realizowany przez rekurencję top-down.

Dla grafu:
```
Gaussian → Scale(2.0) → Sum → Output
Sine(10Hz) ──────────────┘
```

`evaluateSignal(graphA, outputNodeId, t=0.3)` wywołuje:
- `evaluateSignal(Sum, t=0.3)` wywołuje:
  - `evaluateSignal(Scale, t=0.3)` wywołuje:
    - `evaluateSignal(Gaussian, t=0.3)` → zwraca `exp(-0.5*((0.3-0.3)/0.05)^2) = 1.0`
    - mnoży przez 2.0 → zwraca `2.0`
  - `evaluateSignal(Sine, t=0.3)` → zwraca `sin(2π·10·0.3) = sin(18.85) ≈ -0.95`
  - sumuje → zwraca `2.0 + (-0.95) = 1.05`
- Output → zwraca `1.05`

Dokładnie to samo robi `generateExpr` — zamiast zwracać liczbę, zwraca **string wyrażenia CUDA**:
- `generateExpr(Gaussian, "t")` → `"(exp(-0.5*((t-0.30)/0.05)*((t-0.30)/0.05)))"`
- `generateExpr(Scale, "t")` → `"(2.0 * (exp(-0.5*((t-0.30)/0.05)*((t-0.30)/0.05))))"`
- `generateExpr(Sine, "t")` → `"(1.0 * sin(62.83...*t))"`
- `generateExpr(Sum, "t")` → `"((2.0*exp(...)) + (1.0*sin(...)))"`

Wynikowy kernel:
```cuda
output[i] = ((2.0*exp(-0.5*((t-0.30)/0.05)*((t-0.30)/0.05))) + (1.0*sin(62.83...*t)));
```

Jeden wiersz kodu CUDA, który oblicza sumę przeskalowanego Gaussiana i sinusoidy — wygenerowany automatycznie z grafu.

### TimeShift: dlaczego jest specjalny

Węzeł TimeShift przesuwa sygnał w czasie. Jeśli wejście to `f(t)`, wyjście to `f(t - τ)`. Implementacja w evaluatorze CPU jest trywialna: zamiast przekazywać `t` do rekurencji, przekazujesz `t - τ`.

W generatorze kodu CUDA jest to eleganckie: zamiast używać `"t"` jako zmiennej czasu dla podgrafu, generujesz wyrażenie `"(t - 0.10000000000000001)"` i używasz go jako zmiennej czasu:

```cpp
std::string shifted = "(" + timeVar + " - " + D(tau) + ")";
return generateExpr(graph, inputNode, shifted);  // Użyj "shifted" zamiast "t"
```

Wynikowy kod CUDA może wyglądać tak:
```cuda
output[i] = exp(-0.5*(((t - 0.10) - 0.30)/0.05)*(((t - 0.10) - 0.30)/0.05));
```

Co jest matematycznie poprawnym Gaussianem przesuniętym o 0.1 sekundy w prawo. Kompilator CUDA (i NVRTC) zoptymalizuje zagnieżdżone wyrażenia arytmetyczne w czasie kompilacji, więc dodatkowe nawiasy nie wpływają na wydajność runtime.

### Reflect: odbicie sygnału

Reflect to przypadek graniczny podobny do TimeShift: zamiast `t - τ` przekazujesz `T - t` gdzie `T = 1.0` sekundy. Gaussian w `t = 0.3` staje się Gaussianem w `t = 0.7`. Sygnał jest "odbity" względem środka czasu `T/2 = 0.5`.

W generowanym kodzie CUDA:
```cuda
output[i] = exp(-0.5*((1.0 - t - 0.30)/0.05)*((1.0 - t - 0.30)/0.05));
```

### Dlaczego code explosion nie jest problemem

Potencjalny problem z rekurencyjną generacją kodu: jeśli masz węzeł używany przez dwa różne operatory (shared subexpression), jego wyrażenie zostanie zduplikowane w wygenerowanym kodzie. Dla grafu z 10 poziomami zagnieżdżenia to może dać eksponencjalnie długi kod.

W praktyce dla typowych grafów sygnałów (kilka do kilkunastu węzłów, jeden węzeł OUTPUT) nie ma shared subexpressions — grafy sygnałów są naturalnie drzewami, nie sieciami. Nawet jeśli użytkownik stworzy skomplikowany graf z shared subexpressions (np. dwa operatory Sum podłączone do tego samego Gaussiana), wygenerowany kod będzie miał zduplikowane obliczenia, ale NVRTC podczas kompilacji do PTX i tak wykona **common subexpression elimination** (CSE) — optymalizację która eliminuje duplikaty. Więc final kernela jest efektywny niezależnie.

---

## Rozdział 11 — Nowy pipeline GPU w Fazach 4 i 5

Najważniejsza zmiana architektoniczna w Fazie 4 nie dotyczy node editora — dotyczy **pipeline'u GPU**. Sposób w jaki dane trafiają do splotu fundamentalnie się zmienia.

### Stary pipeline (Fazy 1–3)

```
CPU: oblicz sygnał A (Gaussian)
CPU: oblicz sygnał B (Rectangle)
H2D: skopiuj A na GPU (32 KB dla N=4096)
H2D: skopiuj B na GPU
GPU: kernel splotu (czyta A, B z VRAM, pisze C)
D2H: skopiuj wynik C z GPU (32 KB)
CPU: sprawdź CPU reference, walidacja
```

W tym pipeline GPU oblicza tylko **splot**. Sygnały wejściowe są generowane na CPU i muszą być przetransferowane przez PCIe.

### Nowy pipeline (Fazy 4–5)

```
GPU: kernel generateSignalA → d_A (oblicza Gaussian z t, bez CPU)
GPU: kernel generateSignalB → d_B (oblicza Rectangle lub inny sygnał)
GPU: kernel convolution(d_A, d_B) → d_C
D2H: skopiuj A, B, C z GPU (96 KB dla N=4096 — trzy tablice)
CPU (wątek GPU): oblicz CPU reference, walidacja
```

Teraz GPU oblicza **wszystko** — generację sygnałów i splot. CPU jest potrzebne tylko do czytania wyników (dla wykresów) i walidacji.

Dlaczego to lepsze? Kilka powodów:

**Eliminacja H2D dla sygnałów**: Sygnały A i B nie muszą być transferowane z CPU na GPU. Dla N=4096 oszczędność jest mała (64 KB), ale dla N=65536 jest już to 1 MB. Przy stosie typowych operacji przetwarzania sygnałów (dziesiątki sygnałów) to robi różnicę.

**GPU oblicza sygnały równolegle**: Kernel `generateSignalA` uruchamia N wątków, każdy oblicza jedną próbkę sygnału. Dla N=65536 to 65536 wątków obliczających Gaussian jednocześnie. CPU z jednym wątkiem robi to sekwencyjnie — GPU jest szybszy o rząd wielkości.

**Przygotowanie pod przyszłe rozszerzenia**: Gdybyś chciał generować sygnały z bardziej złożoną matematyką (np. fraktale, symulacje numeryczne), obliczanie ich na GPU jest naturalne i skalowalne.

D2H jest teraz więcej (trzy tablice zamiast jednej) bo chcesz pokazać sygnały A i B na wykresie. Ale to koszt wizualizacji, nie koszt obliczeniowy — w systemie produkcyjnym bez UI D2H byłby tylko dla wyniku splotu.

### Trzy osobne kernele w jednym module

W Fazie 4, `KernelManager::compile()` kompiluje jeden string CUDA który zawiera dwa kernele: `generateSignalA` i `generateSignalB`. Jeden string, jedna kompilacja NVRTC, jeden `CUmodule` — ale dwa `CUfunction` uchwyty, pobrane przez dwa wywołania `cuModuleGetFunction`.

Kernel splotu (`convolution`) jest nadal w osobnym pliku `kernels/convolution.cu` i zarządzany przez osobny `KernelManager`. To pozwala niezależnie przeładowywać kernel splotu (jak w Fazie 2) i niezależnie regenerować kernele sygnałów (gdy graf węzłów się zmienia).

Gdybyś chciał, mógłbyś wszystkie trzy kernele umieścić w jednym module. Ale rozdzielenie jest architektonicznie czystsze: kernel splotu to infrastruktura (zmienia się rzadko, może być hot-reloadowany niezależnie), kernele sygnałów to generowane dane (zmieniają się przy każdej zmianie grafu).

### N jako dynamiczny parametr

W Fazie 5 N staje się parametrem konfigurowanym przez UI — suwak pozwala wybrać N od 1024 do 65536. Co się zmienia gdy N się zmienia?

Przede wszystkim zmieniają się **rozmiary buforów**: `state.signalA.resize(N)`, `state.signalB.resize(N)`, `state.convOutput.resize(N)`. To CPU-side bufory dla wyników readback.

Po stronie GPU: `cudaMalloc` w `runPipeline` alokuje `N * sizeof(double)` bajtów. Dla N=65536 to 512 KB na każdy bufor, czyli 1.5 MB razem — bez problemu dla GPU z kilkoma GB VRAM.

Wymiary siatki CUDA: `gridDim = (N + 255) / 256`. Dla N=1024: 4 bloki po 256 wątków = 1024 wątki. Dla N=65536: 256 bloków po 256 wątków = 65536 wątków. Kernel jest dokładnie tak samo napisany dla obu — warunek `if (n >= N) return` obsługuje ostatni niekompletny blok.

Zmiana N **nie wymaga** rekompilacji przez NVRTC — N jest przekazywane jako parametr kernela (`int N`), a nie zahardkodowane w kodzie. To jest celowa decyzja projektowa: parametry obliczeniowe są parametrami kernela, nie osadzonymi stałymi.

**CPU reference dla dużego N**: Dla N=65536 CPU reference to `65536^2 / 2 ≈ 2 × 10^9` operacji — przy 1 GFlops (typowo dla jednego wątku CPU) to 2 sekundy. Stąd flaga `skipCpuReference` dla `N > 8192`. Wątek GPU nadal wykonuje obliczenia GPU, ale pomija kosztowną walidację CPU.

---

## Rozdział 12 — imnodes: jak działa node editor

`imnodes` to biblioteka zbudowana na ImGui która dodaje interaktywny edytor grafów węzłów. Jej model jest identyczny z ImGui — **immediate mode** — co znaczy, że nie przechowuje stanu grafu. Ty przechowujesz stan, a imnodes renderuje go i raportuje interakcje.

Każda klatka wywołujesz `ImNodes::BeginNodeEditor()`, a potem dla każdego węzła w swojej liście wywołujesz `ImNodes::BeginNode(id)`, renderujesz zawartość (piny, parametry), i `ImNodes::EndNode()`. Dla każdego linku wywołujesz `ImNodes::Link(linkId, srcAttrId, dstAttrId)`. Na końcu `ImNodes::EndNodeEditor()`.

To powoduje że na ekranie pojawiają się węzły z pinami i liniami. imnodes zarządza tylko renderowaniem — nie przechowuje gdzie są węzły, jakie mają połączenia, co to za typ. Ty przechowujesz to w `NodeGraph`.

Po `ImNodes::EndNodeEditor()` możesz zapytać o interakcje:
- `IsLinkCreated(&srcAttr, &dstAttr)` — czy użytkownik właśnie przeciągnął nowe połączenie?
- `IsLinkDestroyed(&linkId)` — czy użytkownik właśnie usunął połączenie?
- `NumSelectedNodes()` + `GetSelectedNodes(ids)` — które węzły są zaznaczone (dla usuwania przez Delete)?

Te funkcje działają tak samo jak `ImGui::Button` — zwracają true/false w klatce gdy event się pojawił, false przez wszystkie pozostałe klatki.

### Unikalność ID: fundament stabilności

Najważniejszy wymóg imnodes to **unikalne i stabilne ID** dla węzłów i atrybutów. "Stabilne" znaczy: ID danego węzła musi być takie samo przez całe życie węzła — jeśli zmieniasz ID węzła między klatkami, imnodes traci ślad jego pozycji, połączeń i stanu selekcji.

Dlatego `NodeGraph` używa rosnącego licznika `nextId` który nigdy nie spada i nigdy nie jest reużywany — nawet po usunięciu węzła. Jeśli węzeł z ID=5 zostaje usunięty, i potem dodajesz nowy węzeł, dostanie ID=13 (lub cokolwiek jest aktualnym nextId), nie ID=5. To gwarantuje że imnodes nigdy nie pomyli starego usuniętego węzła z nowym.

### Kontekst imnodes

`ImNodes::CreateContext()` i `ImNodes::DestroyContext()` tworzą i niszczą kontekst imnodes — analogicznie do `ImGui::CreateContext()`. Jeden kontekst = jeden edytor na raz. Jeśli chcesz renderować dwa niezależne edytory jednocześnie (jak w Fazie 5 dla Signal A i Signal B), używasz jednego kontekstu imnodes ale dwa oddzielne okna ImGui — imnodes obsługuje to automatycznie jeśli wywołujesz `BeginNodeEditor/EndNodeEditor` w każdym oknie osobno.

### Pozycje węzłów: skąd imnodes wie gdzie je narysować

imnodes śledzi pozycje węzłów wewnętrznie, indeksowane po ID węzła. Gdy tworzysz nowy węzeł (nowe ID), imnodes nie zna jeszcze jego pozycji — domyślnie umieszcza go w centrum edytora lub w (0,0). Żeby uniknąć nakładania się węzłów na starcie, możesz użyć:

```cpp
ImNodes::SetNodeEditorSpacePos(nodeId, ImVec2(x, y));
```

To musi być wywołane **przed** pierwszym renderowaniem węzła o tym ID (zazwyczaj tuż po `addNode`).

---

## Rozdział 13 — Cały system: od zmiany parametru do piksela

Spróbujmy prześledzić co się dzieje krok po kroku gdy użytkownik zmienia parametr węzła Gaussian (np. przesuwa suwak `mu` z 0.30 na 0.35) i klika "Generuj kod + Kompiluj":

**Klatka K (moment zmiany):**
1. ImGui wywołuje `DragFloat("mu", &node.params[0], 0.005f)` — użytkownik przesunął suwak
2. `DragFloat` zwraca `true` — ustawiamy `state.codeDirty = true`
3. Reszta klatki renderuje się normalnie — nowy `mu` jest widoczny w suwaku

**Klatka K+1..M (do kliknięcia "Generuj"):**
4. `ImGui::Button("Generuj kod + Kompiluj")` zwraca `false` — nie kliknięto
5. Wyświetla się `"⚠ Kod nieaktualny"` bo `state.codeDirty == true`
6. Wykres pokazuje stary wynik — nowe `mu` nie zmieniło jeszcze obliczenia

**Klatka N (kliknięcie "Generuj"):**
7. `ImGui::Button(...)` zwraca `true`
8. `buildSignalModule(graphA, graphB)` wywołuje `buildSignalKernel(graphA, "generateSignalA")`
   - `buildSignalKernel` wywołuje `generateExpr(graphA, srcNodeId, "t")`
   - `generateExpr` rekurencyjnie buduje string: `"(exp(-0.5*((t-0.35)/0.05)*((t-0.35)/0.05)))"`
   - Wrapper kernel: `"extern \"C\" __global__ void generateSignalA(double* out, int N, double dt) { ... out[i] = expr; }"`
9. Wygenerowany string (kilkaset znaków) jest zapisywany w `state.generatedCode`
10. `state.signalKernelMgr.compile(state.generatedCode, cap, {"generateSignalA","generateSignalB"})` jest wywoływane
11. Wewnątrz `compile()`:
    - `nvrtcCreateProgram(&prog, sourceString, "signal.cu", 0, null, null)`
    - `nvrtcCompileProgram(prog, numOpts, opts)` — NVRTC kompiluje string CUDA C++ do PTX
      *(to trwa 300ms – 1s, główny wątek jest zablokowany przez ten czas!)*
    - `nvrtcGetPTX(prog, ptxBuffer)` — pobieramy PTX
    - `cuModuleLoadData(&m_module, ptxBuffer)` — sterownik kompiluje PTX do kodu maszynowego GPU i ładuje do VRAM
    - `cuModuleGetFunction(&m_functions["generateSignalA"], m_module, "generateSignalA")`
    - `cuModuleGetFunction(&m_functions["generateSignalB"], m_module, "generateSignalB")`
12. `state.codeDirty = false`, `state.dirty = true`
13. Klatka renderuje się z `"OK (487 ms)"` zamiast `"⚠ Kod nieaktualny"`

**Klatka N+1 (pierwszy render po kompilacji):**
14. `appPollAndSubmit(state)` jest wywołane na początku `guiRender`
15. `state.dirty == true && !state.computing && kernelsReady` → zlecamy zadanie
16. `submitPipelineTask(state)`:
    - Tworzymy `GpuTask` z `N`, `dt`, `genAFunc`, `genBFunc`, `convFunc`, kopią grafów
    - `gpuWorker.submitTask(std::move(task))` — task trafia do kolejki wątku GPU
    - Wątek GPU się budzi (condition_variable)
    - `state.gpuFuture = fut`, `state.computing = true`, `state.dirty = false`
17. Wątek główny renderuje klatki dalej — wyświetla spinner "Obliczanie..."

**Wątek GPU (równolegle do klatek N+2, N+3...):**
18. Wątek GPU pobiera task z kolejki
19. `runPipeline(genAFunc, genBFunc, convFunc, N, dt, ...)`:
    - `cudaMalloc(&d_A, N*8)`, `cudaMalloc(&d_B, N*8)`, `cudaMalloc(&d_C, N*8)` — alokacja w VRAM
    - `cuLaunchKernel(genAFunc, N/256, 1,1, 256,1,1, ...)` — N wątków GPU liczy sygnał A:
      każdy wątek liczy `t = i * dt`, a następnie `exp(-0.5*((t-0.35)/0.05)^2)` i zapisuje do `d_A[i]`
    - `cudaEventSynchronize(...)` — CPU czeka aż GPU skończy generateSignalA
    - `cuLaunchKernel(genBFunc, ...)` — N wątków liczy sygnał B (Rectangle)
    - `cuLaunchKernel(convFunc, ...)` — N wątków liczy splot: każdy liczy `C[n] = Σ A[k]*B[n-k]`
    - `cudaMemcpy(signalA_out, d_A, ...)` — readback A do CPU buffer w AsyncConvResult
    - `cudaMemcpy(signalB_out, d_B, ...)` — readback B
    - `cudaMemcpy(convOut, d_C, ...)` — readback wynik splotu
    - `cudaFree(d_A)`, `cudaFree(d_B)`, `cudaFree(d_C)` — zwolnienie VRAM
20. CPU reference (opcjonalne dla N ≤ 8192): wątek GPU oblicza sygnały i splot na CPU dla walidacji
21. `m_busy.store(false)`, `task.promise.set_value(std::move(asyncResult))` — przesyłamy wynik przez future

**Klatka N+K (wynik gotowy):**
22. `appPollAndSubmit`: `state.gpuFuture.wait_for(0ms) == ready`
23. `AsyncConvResult res = state.gpuFuture.get()` — pobieramy wynik (natychmiastowe)
24. `state.signalA = std::move(res.signalA)` — N wartości double dla wykresu
25. `state.signalB = std::move(res.signalB)` — N wartości double
26. `state.convOutput = std::move(res.convOut)` — N wartości double
27. `state.computing = false`
28. W tej samej klatce ImPlot renderuje wykresy z nowymi danymi:
    - `ImPlot::PlotLine("Sygnał A", state.t.data(), state.signalA.data(), N)`
    - `ImPlot::PlotLine("Sygnał B", state.t.data(), state.signalB.data(), N)`
    - `ImPlot::PlotLine("Splot", state.t.data(), state.convOutput.data(), N)`
29. Użytkownik widzi zaktualizowany wykres z Gaussianem przesuniętym do mu=0.35

Całość od kliknięcia "Generuj" do pojawienia się nowego wykresu: 0.5–2.5 sekundy (zależnie od czasu kompilacji NVRTC). Obliczenia GPU dla N=4096 zajmują ułamek milisekundy — zdecydowana większość czasu to kompilacja NVRTC.

---

## Rozdział 14 — Wzorce projektowe: co naprawdę się tu nauczyłeś

Ten projekt nie jest tylko o GPU. Jest o wzorcach architektonicznych, które używasz w każdym złożonym programie. Warto je nazwać wprost.

### Dirty flag (flaga "brudna")

To wzorzec opóźnionego obliczania. Zamiast przeliczać coś przy każdej zmianie (co mogłoby być drogie), oznaczasz że "coś się zmieniło" flagą, i przeliczasz tylko gdy wynik jest potrzebny — albo w określonym rytmie (co klatkę, co sekundę).

W tym projekcie masz **wiele poziomów** dirty flags:
- `state.dirty` — czy sygnał lub parametry się zmieniły, potrzebne nowe obliczenie GPU
- `state.codeDirty` — czy graf węzłów się zmienił, potrzebna regeneracja kodu CUDA

Oba flagi są **niezależne** — możesz zmienić `mu` (ustawia `dirty=true`) bez konieczności regenerowania kodu (`codeDirty` nie jest zmieniane, bo kod jest już wygenerowany dla danego grafu, tylko parametry się zmieniły... chwila, w Fazie 4+ parametry są ZAHARDKODOWANE w generowanym kodzie, więc zmiana `mu` faktycznie wymaga regeneracji kodu). To jest miejsce gdzie architektura Faz 4–5 różni się od Faz 1–3: w Fazach 1–3 parametry sygnałów są przekazywane w runtime przez CPU, więc `dirty=true` wystarczy. W Fazach 4–5 parametry są osadzone w generowanym kodzie CUDA przez `D(node->params[0])`, więc zmiana parametru wymaga regeneracji kodu (`codeDirty=true`) a potem rekompilacji NVRTC.

### Manager zasobów (RAII-like)

`KernelManager` to manager zasobów. Przechowuje `CUmodule` i mapę `CUfunction`. Destruktor zwalnia `CUmodule` przez `cuModuleUnload`. Jeśli `compile()` się nie powiedzie, stary moduł jest zachowywany. Jeśli się powiedzie, stary jest zwalniany i nowy ładowany.

To jest RAII (Resource Acquisition Is Initialization) — wzorzec C++ gdzie zasób (VRAM, uchwyt systemu, połączenie sieciowe) jest związany z czasem życia obiektu. Gdy obiekt jest niszczony, zasób jest automatycznie zwalniany.

`GpuWorkerThread` to inny manager zasobów: zarządza `std::thread`, `cudaStream_t`, i zapewnia `join()` w destruktorze. Bez tego mielibyśmy wycieki wątków.

### Producent-konsument

`GpuWorkerThread` implementuje klasyczny wzorzec producent-konsument: wątek główny (producent) wkłada zadania do kolejki, wątek GPU (konsument) je pobiera i przetwarza. Mutex + condition_variable to standardowy mechanizm synchronizacji dla tego wzorca.

Kluczowa właściwość: producent i konsument działają **asynchronicznie** — producent nie czeka na konsumenta. Kolejka działa jako bufor, który absorbuje różnicę szybkości.

### Stratified compilation (warstwowa kompilacja)

Projekt ma trzy warstwy kompilacji działające w różnych momentach:
1. MSVC w czasie budowania: zarządzanie, UI, logika aplikacji
2. NVCC w czasie budowania: kernel splotu (w Fazach 1–2), later obsoleted
3. NVRTC w runtime: kernele sygnałów, kernel splotu (hot-reload)

Ta stratyfikacja pozwala zoptymalizować każdą warstwę osobno: kod zarządzający jest kompilowany z pełnymi optymalizacjami MSVC, kernele GPU są kompilowane przez NVRTC z opcjami GPU-specificznymi. W bardziej złożonym systemie (shader materiałów, skrypty gry) ta wielowarstwowość jest standardem.

---

## Rozdział 15 — Rzeczy których nie widać

Każdy działający system ukrywa kompromisy. Warto wiedzieć o tych w tym projekcie.

### Brak shared memory w kernelu splotu

Kernel splotu czyta A i B z globalnej pamięci GPU (VRAM) — dla każdego z N wątków, każdy czyta N elementów = N² czytań globalnej pamięci. Global memory GPU ma przepustowość rzędu 400–600 GB/s (RTX 2070 Super), ale latency jest rzędu 200–400 ns. Wątki w tym kernelu spędzają większość czasu czekając na dane z pamięci.

Zoptymalizowana implementacja splotu użyłaby shared memory (L1 cache on-chip): wczytałaby kawałek A i B do shared memory (dostęp ~1 ns), i z niego czytała wielokrotnie. Dla N=4096 przyspieszyłoby to kernel wielokrotnie. Ale celem projektu było GPU plumbing, nie optymalizacja kerneli — stąd prosta implementacja.

### Blokowanie głównego wątku przez NVRTC

W tej implementacji `KernelManager::compile()` blokuje wywołujący wątek. Wywołujesz go z wątku głównego (gdy klikasz "Generuj"), więc przez 0.5–2 sekundy okno nie reaguje.

Idealne rozwiązanie: kompilacja NVRTC też powinna być w osobnym wątku (albo w wątku GPU). Ale dodaje to kolejną warstwę złożoności: musisz sobie poradzić z "NVRTC w toku, kernele jeszcze niedostępne". Dla projektu edukacyjnego blokowanie jest akceptowalne — użytkownik wie że kliknął "Kompiluj" i czeka.

### Brak serializacji stanu grafu

Gdy zamkniesz aplikację, graf węzłów jest utracony. Przy ponownym uruchomieniu zawsze widzisz domyślny Gaussian. W pełnym projekcie chciałbyś serializować stan grafu do pliku JSON lub binarnego i przywracać przy starcie. To nie zostało zaimplementowane, bo to infrastruktura, nie nowe koncepcje GPU.

### CPU reference dla dużego N

Dla N > 8192 CPU reference jest pomijany. To znaczy że przy dużym N nie masz walidacji poprawności wyników GPU. Jeśli kernel ma błąd przy dużym N (np. problem z indeksowaniem przekraczającym zakres), nie zobaczysz tego przez porównanie z CPU. To akceptowalny kompromis dla projektu edukacyjnego.

### NVRTC bez nagłówków matematycznych

Kernele generowane przez NVRTC używają `sin`, `cos`, `exp`, `floor`, `fabs` bez żadnych `#include`. Działa to dlatego że są to wbudowane funkcje urządzenia CUDA — kompilator je zna bez nagłówków. Ale jeśli kernel potrzebowałby czegoś z `<cuda_fp16.h>` (half precision) albo `<cooperative_groups.h>`, musiałbyś przekazać te nagłówki do NVRTC przez parametry `numHeaders`/`headers`/`headerNames` w `nvrtcCreateProgram`. To zaawansowana funkcja NVRTC której nie potrzebowałeś.

---

## Rozdział 16 — Gdzie jest granica: co GPU nie jest w stanie zrobić (w tym projekcie)

Projekt nie ma kilku rzeczy, które naturalnie by się z nim kojarzyły.

**Brak sieciowania wątków GPU**: GPU ma tysiące rdzeni, ale wszystkie wykonują ten sam program (kernel). Jeden kernel = jeden "program" dla wszystkich wątków. Nie możesz kazać wątkom GPU robić różnych rzeczy — to model SIMD (Single Instruction, Multiple Data). Jeśli masz różne przypadki w kernelu (if-else), GPU serialnie wykonuje obie ścieżki dla każdego warpu — stąd warp divergence.

**Brak rekurencji na GPU**: Starsze GPU (przed compute capability 2.0) nie obsługiwały rekurencji w kernelach. Nowsze obsługują, ale z ograniczoną głębokością (stos jest ograniczony). Ewaluator grafu jest rekurencyjny na CPU — słusznie, bo CPU jest zaprojektowany dla takich operacji. Generacja kodu CUDA jest rekurencyjna na CPU i generuje **płaski** (nierekurencyjny) kod dla GPU.

**Brak komunikacji między blokami**: Wątki w jednym bloku mogą się synchronizować przez `__syncthreads()` i komunikować przez shared memory. Wątki w różnych blokach nie mają żadnego mechanizmu bezpośredniej komunikacji. Jedyna komunikacja między blokami to pisanie do global memory i czytanie przez inne — z synchronizacją przez `cudaDeviceSynchronize()`. Splot jest naturalnie paralelny bez potrzeby komunikacji między blokami, więc to nie jest problem.

---

## Epilog — Czego się naprawdę nauczyłeś

Patrząc wstecz na pięć faz, projekt uczył nie jednej ale kilku konceptualnie odrębnych umiejętności naraz.

**Faza 1** nauczyła podstawowego GPU pipeline: alokacja VRAM, transfer danych, kernel, synchronizacja, readback. To jest szkielet każdego programu GPU i rozumiesz go teraz instynktownie, bo go napisałeś, zdebuggowałeś i naprawiłeś.

**Faza 2** nauczyła że kompilacja to operacja, nie tylko proces budowania. NVRTC uczyniło kompilację funkcją pierwszej klasy w aplikacji — czymś co możesz wywoływać, obserwować (log), mierzyć (czas), i od którego możesz się odbić gdy coś idzie nie tak.

**Faza 3** nauczyła wielowątkowości w kontekście asynchronicznych operacji I/O (tutaj: GPU). Wzorzec wątek roboczy + kolejka zadań + future/promise jest uniwersalny — dokładnie to samo używa się do asynchronicznych zapytań sieciowych, czytania plików, renderowania offscreen. Zrozumiałeś dlaczego UI musi być nieblokowane i jak to osiągnąć.

**Fazy 4 i 5** nauczyły meta-programowania: generowania programu (kodu CUDA) przez program (generator kodu). Rekurencywny ewaluator grafu, który produkuje zarówno liczby (CPU reference) jak i stringi kodu (CUDA kernel) — to jest dualność kompilatora i interpretera w miniaturze.

Razem: nauczyłeś się jak GPU "myśli" (równoległy, SIMD, pamięć jako bottleneck), jak pisać kod który działa na wielu poziomach jednocześnie (buildtime MSVC, buildtime NVCC, runtime NVRTC), jak projektować asynchroniczny system z odpowiednią synchronizacją, i jak generować kod z danych — co jest fundamentem języków skryptowych, shaderów, i każdego systemu gdzie użytkownik opisuje obliczenie w domenie problemu i system tłumaczy to na instrukcje maszyny.

Splot dwóch sygnałów — Gaussiana i prostokąta — był tylko pretekstem. Pretekstem który zmusił cię do napisania czegoś wystarczająco realnego żeby napotkać wszystkie te problemy. I to jest najlepsza metoda nauki.

---

*Ten dokument był zbyt długi? Może. Ale krótkie wyjaśnienia zostawiają luki. Luki w zrozumieniu GPU wracają jako nieintuicyjne błędy, dziwne wyniki i aplikacje które "nie wiadomo dlaczego" crashują. Lepiej raz przeczytać za dużo niż dziesięć razy debuggować w ciemności.*
