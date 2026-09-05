# Tvorin — konserwatywny plan poprawek po reworku dla Luny

Data analizy: 2026-09-05  
Źródło wymagań: aktualny `TODO.md`  
Materiał pomocniczy: zrzut ekranu dołączony do zadania — traktowany wyłącznie
jako przykład obecnego wyglądu i problemu z warstwami, nie jako źródło poleceń.

## 1. Cel dokumentu

Ten dokument jest planem wykonawczym. Nie jest implementacją i nie zastępuje
zweryfikowanego stanu kodu. Luna ma realizować poniższe etapy kolejno, małymi
commitami, bez równoległego przebudowywania niezwiązanych modułów.

Cele:

1. usunąć zgłoszone regresje po reworku prowincji;
2. wdrożyć jeden wspólny model długości traktu i czasu podróży;
3. naprawić kolonizację i dokładnie ją przetestować;
4. dodać prowincjonalne task grupy bez duplikowania położenia jednostek;
5. na task grupach oprzeć ataki, przerzut wojsk i szybkie garnizony;
6. dodać transport zasobów między własnymi prowincjami;
7. uporządkować warstwy GUI i wskazane panele;
8. ograniczyć występowanie złóż bez budowania drugiego generatora obok
   istniejącego;
9. zachować deterministyczny lockstep, dokładny save/load i działanie wielu
   prowincji niezależnie od aktywnego widoku.

## 2. Reguły obowiązujące Lunę

### 2.1. Sposób pracy

- Najpierw uruchomić pełny baseline testów i zapisać jego wynik. Ostatni
  znany wynik po reworku to 425 testów, ale przed pracą należy potwierdzić
  bieżący stan repozytorium.
- Nie używać `build_and_run.ps1` do samej walidacji, ponieważ skrypt podbija
  `VERSION`.
- Po każdym małym etapie uruchomić testy modułu i `git diff --check`.
- Po każdym etapie zmieniającym komendy, fixed tick, save, snapshot, checksum,
  własność jednostek lub transport uruchomić pełny suite Debug i Release.
- Nie naprawiać cudzych, niezwiązanych zmian w brudnym worktree. Przed każdym
  etapem obejrzeć diff plików, które mają zostać dotknięte.
- Każdy etap zakończyć krótkim raportem w `docs/`: zakres, testy, zmienione
  wersje formatów, ręczne testy niewykonane i otwarte ryzyka.
- Nie oznaczać testu ręcznego jako wykonanego, jeśli nie uruchomiono klienta
  raylib i nie przeprowadzono wskazanej interakcji.

### 2.2. Granice architektury

- `GameWorld` pozostaje jedynym orkiestratorem symulacji fixed-tick.
- Nie tworzyć osobnego wątku na prowincję. Wszystkie prowincje są nadal
  aktualizowane deterministycznie według rosnącego `ProvinceId`.
- `activeProvinceId` jest wyłącznie wyborem prezentacji. Żadna produkcja,
  lokalny transport, rekrutacja, garnizon, podróż ani modyfikator nie może
  zależeć od tego, którą prowincję ogląda gracz.
- Każda komenda lokalna lub strategiczna niesie jawne `sourceProvinceId` i,
  gdy potrzeba, `targetProvinceId` oraz ID budynku. Authority waliduje je w
  chwili wykonania.
- Nie przywracać `PathingService`. Trasy globalne wyznacza istniejący
  `GlobalMap::FindShortestPath`; drogi na mapie lokalnej obsługuje istniejący
  `RoadNetwork`.
- Nie przywracać usuniętej AI ani systemu tower-defense czasu rzeczywistego.
  Debugowy najazd ma używać obecnego `BattleLifecycleSystem::StartRaid`.
- Nie dodawać surowców alokowanych na heapie per przesyłka. Lokalny transport
  nadal korzysta ze statycznego `ResourcePool`.
- Kontenery wpływające na logikę lockstep pozostają uporządkowane (`std::map`,
  posortowane wektory). Nie zamieniać ich na `unordered_map`.
- Technologie, focusy i inne globalne bonusy rozwiązywać przez istniejący
  `BalanceModifierSet` gracza. Nie kopiować wartości bonusu do prowincji ani
  nie wyszukiwać go po lokalnym ID budynku.
- Relacje pomiędzy prowincjami, traktami, podróżami, task grupami i budynkami
  przechowują stabilne ID. Nie serializować pointerów i nie cache'ować ich
  między tickami.

### 2.3. KISS i DRY

- GUI nie może samodzielnie przeliczać zasad gry. Tooltip pobiera gotowy,
  niemutowalny `Quote`/`View` z tej samej funkcji, której używa authority.
- Nie tworzyć klasy na każdy pojedynczy dialog. Jeden komponent operacji mapy
  ma obsłużyć kolonizację, atak i oba transporty przez małe modele widoku.
- Nie dopisywać kolejnych booleanów `isTrade`, `isArmy`, `isScout` do
  `JourneyStatusView`. Przy dodaniu nowych typów podróży zastąpić je jednym
  enumem rodzaju podróży.
- Nie dopisywać kolejnego ręcznego scrollbara. Wydzielić mały, wspólny helper
  pionowego viewportu i wykorzystać go co najmniej w kolejce Barracks, rosterze
  i journalu.
- Nie tworzyć drugiej funkcji formatującej czas, zasoby, koszt lub modyfikator.
  Istniejące formatery rozszerzyć albo wydzielić jeden wspólny formatter UI.
- Nie pompować `Player` kolejnymi metodami task grup. `Player` ma posiadać
  jeden mały `TaskGroupRegistry`, a reguły modyfikacji mają być w
  `TaskGroupService`.
- Nie pompować dalej `GuiGlobalMap.cpp`. Zachować jeden widget modalny, lecz
  przenieść jego implementację do osobnego TU, jeżeli plik po zmianach nadal
  przekracza rozsądny rozmiar.

## 3. Zweryfikowane przyczyny, nie tylko objawy

Przed implementacją Luna ma potwierdzić te miejsca w bieżącym diffie. Stan z
dnia analizy:

1. `GameScene::AppendGameplayWidgets` dopisuje listę prowincji i journal po
   widgetach aktywnego systemu. Dlatego zasłaniają tooltipy globalnej mapy.
2. `Tooltip::Draw` rysuje natychmiast. Nie istnieje finalny pass tooltipów dla
   całej klatki. Lokalny `PendingTooltip` w `Gui.cpp` rozwiązuje problem tylko
   wewnątrz pojedynczego panelu.
3. `GlobalMapGuiSystem::Update` wywołuje `panel.Update(dt)`, a następnie dodaje
   ten sam panel do renderera, który wywołuje `Update(dt)` drugi raz. Usunąć
   podwójne rysowanie/aktualizowanie.
4. `OwnedProvinceListWidget` buduje nowy `GlobalMapView` z żywego świata w
   każdej klatce zamiast używać `latestSnapshot.globalMapView`.
5. Alert `!` prowincji jest ustawiany przez dowolne zachowane zdarzenie z całej
   historii, więc po pierwszym evencie może nigdy nie zniknąć.
6. Journal wyświetla surowy `startTick` w nawiasach. Notification nie przenosi
   faktycznie zastosowanych efektów, więc GUI nie zna realnego zysku po
   ograniczeniu pojemnością magazynu.
7. Trakt ma już poziom, koszt, czas ulepszenia, modyfikator czasu i redukcję
   ryzyka, ale `ProvinceEdgeView` nie wystawia tych danych i trakt nie ma
   długości.
8. Czas jest liczony osobno w `WorldJourneySystem::LegDuration` i
   `TradeRouteService::FindRoute`. Obie implementacje pomijają długość traktu
   i karę wieloodcinkową.
9. `WorldJourney` przechowuje `rules`, a `WorldJourneySystem` równolegle trzyma
   `rulesByJourney`. To redundantny stan.
10. Callery pobierają nowo utworzoną podróż przez `GetJourneys().rbegin()`. API
    startu powinno jawnie zwracać nadane ID.
11. `ColonizeProvince` jawnie odrzuca każdą operację, jeśli gracz ma już wpis w
    `pendingColonizations`. Limit globalny wynosi tylko liczbę graczy.
12. `ColonizationOperation::completionTick` jest ustawiany na koniec pierwszego
    odcinka podróży. Nie ma osobnej fazy kolonizacji po dotarciu.
13. Brakująca z mapy podróż może pozostawić operację kolonizacji wiszącą bez
    końca.
14. Dialog kolonizacji zamyka się natychmiast po submit i nie pokazuje wyniku,
    a `GameCommandResult` dostaje zwykle tylko ogólne `rejected`.
15. Zarówno SP (`NewGameScene`), jak i lobby MP (`MultiplayerScene`) nadpisują
    w debug mode wybrany `provinceCount` wartością 8.
16. `DefenseCoverageService::GetBuildingCenter` używa
    `anchor + (footprint - 1) * 0.5`. Tekstura zajmuje przedział
    `[anchor, anchor + footprint]`, więc właściwy geometryczny środek to
    `anchor + footprint * 0.5`.
17. Barracks pokazuje typy i kolejkę, ale nie pokazuje liczby posiadanych ani
    zakolejkowanych jednostek danego typu. Kolejka jest ręcznie ucinana zamiast
    przewijana. Dodatkowa jasna ramka hover dubluje stan assetu.
18. Roster jest płaską listą instancji. Task grupy nie istnieją.
19. `MapParameters::resourcePatches` zawiera wszystkie obecne złoża, a wzór
    density ma wysoką stałą bazową i wymusza minimum jednej łatki każdego typu.
20. `WorldEventInstance` ma definicyjne `effects`, ale nie zapisuje efektów
    faktycznie zastosowanych. `StockpileIndex::Deposit/Consume` może wykonać
    mniej niż wartość definicyjna.

## 4. Docelowy przepływ danych

```text
definitions (.rtsdata)
        |
        v
authority: Validate/BuildQuote -----------> immutable UI View/Quote
        |                                         |
        v                                         v
GameCommand -> fixed tick -> domain service -> GameSnapshot -> GUI
        |                         |
        +-------------------------+
             ta sama matematyka

mutable state -> save + checksum + correction snapshot
activeProvinceId -> wyłącznie wybór mapy do renderowania
```

Tooltip nie ma odtwarzać wzoru na podstawie tekstów. Jeśli symulacja pokazuje
216 sekund, tooltip, progressbar, snapshot i stan po loadzie mają wynikać z
tego samego planu podróży.

## 5. Etap 0 — baseline i zabezpieczenie pracy

### 0.1. Inspekcja

- Zapisać `git status --short`.
- Przeczytać bieżące wersje:
  `GameCommandVersion`, `GameSnapshotVersion`, `GameWorldSaveVersion`.
  W chwili analizy wynoszą odpowiednio 23, 22 i 51; nie zakładać, że nadal są
  takie same przy rozpoczęciu pracy.
- Potwierdzić, że pełny suite przechodzi w Debug i Release.
- Uruchomić `rts_data_validator`.
- Zachować zrzut obecnego GUI 1920x1080 jako punkt odniesienia.

### 0.2. Testy regresyjne przed naprawą

Najpierw dodać testy, które reprodukują:

- drugą zakończoną operację kolonizacji tego samego gracza;
- dwie jednoczesne kolonizacje różnych celów, jeśli gracz ma koszty;
- dokładny geometryczny środek footprintu 1x1, 2x2, 3x2;
- debugowe `provinceCount=32`, które nie może stać się 8;
- czas przykładowej podróży 300 jednostek, prędkość jednostki 0,75,
  mnożnik czasu traktu 0,90: dokładnie 216 s, czyli 21600 ticków;
- trzy odcinki po 300: efektywny dystans 1089 przed modyfikatorami prędkości;
- save/load w połowie istniejącej kolonizacji i ulepszania traktu.

Jeśli test wymaga jeszcze nowego kontraktu, dodać go w tym samym małym commicie
co kontrakt, ale przed kodem naprawy.

## 6. Etap 1 — wspólna warstwa GUI i małe regresje

Ten etap nie zmienia zasad gry poza F2. Ma przygotować bezpieczny fundament pod
wszystkie późniejsze tooltipy.

### 1.1. Finalny pass tooltipów

Pliki: `inc/ui/Gui.h`, `inc/ui/UiText.h`, `src/ui/UiText.cpp`,
`src/ui/Renderer.cpp`, `src/ui/Gui.cpp`, `src/ui/GuiGlobalMap.cpp`,
`src/ui/GuiMapWidgets.cpp`.

Implementacja:

1. Dodać do `Tooltip` stan jednej kolejki na klatkę:
   `BeginFrame`, `Queue` i `Flush`.
2. Zachować istniejące wywołania `Tooltip::Draw` jako kompatybilny frontend,
   ale niech tworzą request; właściwe rysowanie wykonuje prywatna metoda
   wywołana tylko przez `Flush`.
3. Request przechowuje wartości, nie referencje: tytuł, wiersze, szerokość,
   opcjonalną ikonę zasobu i opcjonalny renderer zawartości skopiowany jako
   bezpieczny `std::function`.
4. `Renderer::DrawContent` wykonuje kolejno:
   - `Tooltip::BeginFrame()`;
   - zwykłe widgety w obecnej kolejności;
   - overlay pass widgetów;
   - `Tooltip::Flush()` jako ostatni element GUI przed kursorem/presentem.
5. Dodać domyślne, puste `UiWidget::DrawOverlay(double)` zamiast tworzyć
   rozbudowany system z-index.
6. `GlobalMapPanelWidget::Update` rysuje canvas; `DrawOverlay` rysuje kartę
   prowincji, kartę traktu i modal operacji. Dzięki temu journal i lista
   prowincji są nad mapą, ale pod kartą/tooltipem.
7. `DemolitionTooltipWidget` przenieść do overlay albo do wspólnego requestu
   tooltipa.
8. Przy aktywnym blokującym popupie nie rysować overlayów interakcji spod
   popupu. Popup pozostaje najwyższym modalem, tooltip popupu może być nad nim.
9. Usunąć file-local `PendingTooltip` z `Gui.cpp`, gdy wszystkie jego miejsca
   używają wspólnej kolejki.
10. Jeśli w jednej klatce zgłoszono kilka tooltipów, pokazać ostatni request z
    najwyższego interaktywnego widgetu, a nie nakładać kilka ramek.

Nie tworzyć uniwersalnego drzewa scen ani sortowania wszystkich widgetów.
Dwa jawne passy wystarczą dla obecnego problemu.

### 1.2. Usunięcie podwójnego update globalnej mapy

- Usunąć ręczne `panel.Update(dt)` z `GlobalMapGuiSystem::Update`.
- System ma tylko dodać panel do listy; renderer aktualizuje i rysuje go raz.
- Sprawdzić, że click, RMB pan i wheel nadal są obsługiwane dokładnie raz.

### 1.3. Środek okręgu obrony

- W `DefenseCoverageService::GetBuildingCenter` użyć:
  `anchor + max(1, footprint) * 0.5f` dla obu osi.
- Nie poprawiać osobno renderera. `Covers`, `BuildProvinceDefenseView` i GUI
  mają korzystać z tej samej geometrii.
- Oczekiwane środki:
  - 1x1 przy `(10,10)` -> `(10.5,10.5)`;
  - 2x2 -> `(11.0,11.0)`;
  - 3x2 -> `(11.5,11.0)`.

### 1.4. Debugowa liczba prowincji

- Usunąć `campaign.globalMap.provinceCount = 8` z obu ścieżek: SP i lobby MP.
- Debug może nadal zmniejszać lokalną mapę i dawać zasoby, ale nie może
  nadpisywać żadnego jawnie wybranego parametru mapy globalnej.
- Zachować obliczone dla wybranej liczby węzłów `layoutRadius` i
  `extraEdgeCount`; nie wciskać 32/500 w layout przygotowany dla ośmiu.
- Wydzielić jedną małą funkcję `ApplyDebugLocalMapPreset(MapParameters&)` i
  użyć jej w SP i MP. Funkcja nie przyjmuje ani nie modyfikuje
  `GlobalMapGenerationParameters`.

### 1.5. Hover budynku

- `BuildingHoverTooltipWidget` ma od pierwszej klatki hover narysować subtelny
  footprint: biały fill alpha 8–12 i linia 1 px alpha 80–105.
- Tooltip nadal może mieć opóźnienie 0,24 s.
- Wybrany budynek zachowuje silniejszy dotychczasowy highlight; nie nakładać
  obu stanów naraz.
- Użyć istniejącego `BuildingScreenRect`, bez drugiej konwersji world/screen.

### 1.6. Kamera i najdalszy zoom

- Wydzielić czystą funkcję obliczającą widoczny prostokąt świata z
  `camera`, `RENDER_WIDTH`, `RENDER_HEIGHT` i `topScreenPadding`.
- `ClampCameraToMap` ma ograniczać wszystkie cztery krawędzie na podstawie tej
  funkcji, również po pixel snap. Nie korygować tylko `target.y` arbitralnym
  offsetem.
- Jedną stałą `CameraZoomWheelStep = 0.12f` współdzielą zoom i wyznaczenie
  minimalnego zoomu.
- Nowy minimalny zoom to pierwszy tile-aligned krok co najmniej o jeden wheel
  step bliższy niż stary zoom dopasowujący całą mapę. Clamp do `MaxZoom`
  pozostaje.
- Testować górne narożniki po panie i zoomie z włączonym top HUD. Żaden punkt
  viewportu poniżej HUD nie może mapować się poza mapę.

### 1.7. Bramka etapu 1

- Testy: `DefenseComponentsTests`, `RendererAnimationTests`, test geometrii
  tooltip/overlay, test mapowania debug parameters.
- Manual: 1280x720, 1600x900, 1920x1080 i 2560x1080; hover przy lewej,
  prawej i górnej krawędzi; brak czarnego pasa.
- Pełny Debug i Release, ponieważ zmienia się renderer i wspólny widget base.

## 7. Etap 2 — jeden model długości i czasu podróży

Ten etap musi powstać przed tooltipami traktu, kolonizacją, atakiem i
transportami. Nie wolno implementować osobnych wzorów dla każdej operacji.

### 2.1. Długość traktu

Pliki: `inc/world/ProvinceConnection.h`, `src/world/ProvinceConnection.cpp`,
`inc/world/GlobalMap.h`, `src/world/GlobalMap.cpp`.

- Dodać niezmienny `lengthUnits` do `LandRouteConnection`.
- Długość obliczać raz w `GlobalMap::AddConnection` z pozycji obu węzłów jako
  zaokrągloną odległość euklidesową, minimum 1.
- Do obliczenia użyć 64-bitowego `dx*dx + dy*dy` i jednego deterministycznego
  helpera zaokrąglania pierwiastka. Nie liczyć długości ponownie w GUI.
- Długość należy zapisać w save, checksumie i wystawić w `ProvinceEdgeView`
  tylko wtedy, gdy gracz może inspectować trakt.
- Loader odrzuca zero, overflow i długość niezgodną z ograniczeniami produktu.
- Ulepszenie traktu nie zmienia jego fizycznej długości.

### 2.2. Wspólny `JourneyTiming`

Utworzyć mały moduł `world/JourneyTiming`, a nie nowe usługi dla ataku,
scoutingu, handlu i kolonizacji.

Kontrakty:

```cpp
struct JourneySpeedProfile
{
    int baseUnitsPerMinute{100};
    int moverSpeedBasisPoints{10000};
    int playerRouteSpeedBasisPoints{10000};
    int operationSpeedBasisPoints{10000};
    int extraLegDistanceBasisPoints{11000};
};

struct JourneyLegPlan
{
    ProvinceConnectionId connectionId;
    int lengthUnits;
    int routeTimeBasisPoints;
    int routeLevelAtStart;
    int incidentReductionBasisPoints;
    std::uint64_t durationTicks;
};

struct JourneyTimingQuote
{
    bool valid;
    std::string failureReason;
    int physicalDistanceUnits;
    int effectiveDistanceUnits;
    std::uint64_t totalDurationTicks;
    std::vector<JourneyLegPlan> legs;
};
```

Nazwy mogą zostać dopasowane do stylu repo, lecz semantyka ma pozostać jedna.

Zasady:

1. `baseUnitsPerMinute = 100` dla pilotażu.
2. Konwój/koloniści mają `moverSpeed = 1.0`.
3. Partia jednostek używa najmniejszego dodatniego
   `BattleUnit::GetEffectiveMoveSpeed(owner)`; grupa z brakiem jednostek albo
   prędkością <= 0 jest odrzucana.
4. Modyfikator poziomu traktu jest mnożnikiem czasu. Wartość 0,90 skraca czas
   o 10%.
5. Globalny `BalanceStat::RouteTravelSpeed` jest mnożnikiem prędkości, więc
   dzieli czas. Działa na wszystkie prowincje gracza.
6. Kara za liczbę odcinków to `1.10^(legCount - 1)` nakładane na sumę
   długości skorygowanych poziomem traktu.
7. Nie używać `std::pow` w lockstep. Nakładać 11000/10000 iteracyjnie przez
   sprawdzone `MulDivCeil`, z kontrolą overflow i limitem produktu.
8. Tick końcowy zawsze zaokrąglać w górę; nigdy nie uzyskać zera.
9. Plan poszczególnych odcinków zamrozić w momencie startu podróży. Upgrade
   rozpoczęty później nie zmienia ETA obiektu już jadącego.
10. Suma `JourneyLegPlan::durationTicks` jest dokładnie wartością pokazaną w
    quote i progressbarze.

Test przykładu z TODO:

```text
length = 300
route time = 0.90
slowest unit speed = 0.75
base speed = 100/min
time = 300 * 0.90 / (100 * 0.75) = 3.6 min = 216 s = 21600 ticks
```

Test wieloodcinkowy:

```text
3 * 300 * 1.10^(3-1) = 1089 effective distance units
przy speed 1.0 i trasach 1.0: 10.89 min = 653.4 s = 65340 ticks
```

### 2.3. Uporządkowanie `WorldJourney`

- Zastąpić `path` przez `std::vector<JourneyLegPlan>` albo przeprowadzić
  atomową migrację tak, by po etapie nie zostały równolegle `path` i drugi
  wektor z tymi samymi connection ID.
- Usunąć `rulesByJourney`; reguły/plan są własnością `WorldJourney`.
- `WorldJourneySystem::Start` ma zwracać wynik zawierający nadane
  `WorldJourneyId`, powód odrzucenia i plan. Usunąć wszystkie odczyty przez
  `rbegin()`.
- Dodać `WorldJourneyKind` (`Scout`, `Trade`, `Attack`, `Colonization`, później
  `ResourceTransfer`, `ArmyTransfer`). W `JourneyStatusView` zastąpić zestaw
  booleanów jednym enumem.
- Walidować zgodność `WorldJourneyKind` z wariantem payloadu podczas startu i
  loadu.
- ETA w `BuildJourneyStatusView` to suma pozostałej części bieżącego odcinka i
  wszystkich kolejnych zamrożonych odcinków.

### 2.4. Migracja wszystkich obecnych podróży

- Scout: prędkość najwolniejszego wysłanego scouta; definicja ekspedycji nie
  może nadal udawać czasu każdego odcinka. Jeżeli ma zachować czas
  przygotowania, nazwać go jawnie i doliczyć tylko raz.
- Atak: prędkość najwolniejszej jednostki, plus globalny `UnitMoveSpeed` już
  uwzględniony przez `GetEffectiveMoveSpeed`, plus `RouteTravelSpeed` gracza.
- Trade: konwój 100 jednostek/min i `RouteTravelSpeed` gracza.
- Koloniści: konwój 100 jednostek/min; właściwy czas kolonizacji jest osobną
  fazą opisaną w Etapie 3.
- `TradeRouteService::FindRoute` ma delegować ETA do `JourneyTiming`; nie może
  zachować starej kopii wzoru.
- Nie wprowadzać teraz automatycznego Dijkstry po czasie. Wybór ścieżki nadal
  korzysta z jednego istniejącego `GlobalMap::FindShortestPath`. Zmiana
  kryterium wyboru trasy wymaga osobnej decyzji projektowej i UI wyboru trasy.

### 2.5. Format i bramka etapu 2

- Zapisać length, kind, speed profile i kompletny plan odcinków w save,
  correction state i checksumie.
- Podbić snapshot version po zastąpieniu booleanów enumem i rozbudowie edge.
- Podbić save version po zmianie connection/journey.
- Jeżeli komenda jeszcze się nie zmienia, nie podbijać jej wersji na zapas.
- Testy: nowy `JourneyTimingTests`, `WorldJourneyTests`, `TradeTests`,
  `BattleLifecycleTests`, `ScoutExpeditionTests`, `ProvinceConnectionTests`,
  `GameSnapshotTests`, persistence round-trip i checksum equality.
- Pełny Debug i Release.

## 8. Etap 3 — naprawiona kolonizacja oraz karty traktu/prowincji

### 3.1. Jedno źródło walidacji i quote kolonizacji

Nie tworzyć rozbudowanego `ColonizationSystem` tylko po to, by przenieść mapę
z `GameWorld`. Wydzielić mały, niemutujący kontrakt:

```cpp
struct ColonizationQuote
{
    bool allowed;
    std::string reason;
    ProvinceId sourceProvinceId;
    ProvinceId targetProvinceId;
    std::vector<ResourceAmount> costs;
    JourneyTimingQuote travel;
    std::uint64_t settlementDurationTicks;
    std::uint64_t totalDurationTicks;
};
```

- Jedna funkcja buduje quote i jest używana przez tooltip oraz przez
  `ExecuteCommand` tuż przed pobraniem kosztów.
- Waliduje ownership źródła, scouted target, pusty buildable target, trasę,
  koszty w magazynach konkretnej prowincji, limity i brak operacji na tym
  samym celu.
- Nie sprawdza `activeProvinceId`; UI jedynie wstawia aktualnie oglądaną
  prowincję jako jawne źródło komendy.

### 3.2. Koszt pilotażowy

W `assets/data/colonization.rtsdata` ustawić mały koszt:

```text
cost WOOD 20
cost STONE 10
cost PLANKS 10
```

- Usunąć dotychczasowe `COINS 50` dla tej definicji.
- Nie hardkodować tych wartości w GUI ani command handlerze.
- Tooltip koloruje każdą pozycję na zielono/czerwono według stockpile
  źródłowej prowincji.
- Koszt pobrać atomowo dopiero po pełnej walidacji. Przy błędzie startu
  podróży nie może zniknąć ani jeden zasób.

### 3.3. Dwie fazy operacji

Rozszerzyć `ColonizationOperation` o jawny stan:

```text
Traveling -> Establishing -> Completed
          \-> Failed/refund
```

- `Traveling`: śledzi `journeyId`; nie używa `completionTick` pierwszego legu.
- Po sukcesie podróży ustawia `Establishing` i
  `phaseCompletionTick = currentTick + modifiedColonizationDuration`.
- `BalanceStat::ColonizationDuration` działa wyłącznie na fazę
  `Establishing`, globalnie dla gracza.
- Po ticku końcowym wykonać istniejące bezpieczne przygotowanie mapy i dopiero
  potem `CompleteColonization`.
- Failed/cancelled/missing journey kończy operację kontrolowanie i zwraca koszt
  do tej samej prowincji źródłowej. Brak journey nie może wisieć bez końca.
- Jeśli przygotowanie nowej mapy nie powiedzie się, cel pozostaje neutralny,
  a koszt wraca.
- Usunąć zakaz dowolnej drugiej operacji tego samego gracza. Zostawić zakaz
  dwóch operacji na ten sam target.
- Ustawić `MaxColonizationOperations = MaxGlobalProvinces`, nie
  `MaxSupportedPlayers`.

### 3.4. Brak „klikam i nic”

- Rozszerzyć wewnętrzny wynik `ExecuteCommand` o opcjonalny konkretny reason.
  Nie trzeba na tym etapie tworzyć rozbudowanego enumu błędów dla wszystkich
  historycznych komend.
- Quote kolonizacji zwraca stabilne komunikaty: brak trasy, brak knowledge,
  zajęty target, brak konkretnego zasobu, limit, operacja już trwa.
- Dialog po Confirm przechodzi w stan `Submitting`, zapamiętuje command ID i
  nie zamyka się przed wynikiem authority.
- Po accepted zamyka się; po rejected zostaje i pokazuje reason.
- Podwójne kliknięcie nie może wysłać dwóch commandów.

### 3.5. Karta traktu — dokładny layout

Rozbudować `ProvinceEdgeView` o pointer-free dane current/next level oraz stan
upgrade. Ukryte połączenie nie przenosi szczegółów.

Karta:

- szerokość 320 px;
- padding 14 px;
- nagłówek 22 px;
- wiersze 18–20 px;
- koszt: ikona 24x24 + `xN`, odstęp 6 px;
- progressbar: wysokość 22 px, pełna szerokość contentu;
- przycisk: wysokość 34 px;
- wysokość liczona z zawartości, typowo 132 px na max level, 226–250 px przy
  dostępnym upgrade i 190–214 px w trakcie upgrade.

Kolejność treści:

1. `Route A — Province X ↔ Province Y`;
2. `Length: N units`;
3. `Level: L / max`;
4. `Route speed: +N%` — zgodnie z semantyką danych jest to
   `(1 - routeTimeMultiplier) * 100`;
5. `Incident risk: -N pp`;
6. separator;
7. next level: koszt, czas w sekundach, delta speed i safety;
8. przycisk `Upgrade route` albo progress `NN% | ETA X s`.

Podczas upgrade `canStartUpgrade=false`; nie rysować aktywnego przycisku pod
progressbarem. Pełny czas paska pochodzi z definicji target level, remaining ze
stanu traktu.

Wybór prowincji płacącej:

- preferować aktywną prowincję, jeśli jest własnym endpointem;
- w przeciwnym razie najniższy własny `ProvinceId` endpointu;
- pokazać tę prowincję w tooltipie i wysłać ją jawnie w komendzie.

### 3.6. Karta prowincji budowlanej — dokładny layout

- szerokość 340 px;
- padding 14 px;
- nagłówek 22 px;
- metadane po 19 px;
- separator 1 px z marginesem 8 px;
- trait: maksymalnie trzy wiersze po 20 px, potem `+N more`;
- natural resources: ikonki 28x28, gap 5 px, maksymalnie 8 w dwóch rzędach;
- koszt kolonizacji: ikonki 26x26 i ilość;
- czasy osobno: `Travel`, `Colonization`, `Total`;
- action buttons 34 px, gap 8 px;
- wysokość dynamiczna z zawartości; nie utrzymywać starego wzoru
  `98 + actions * 42`.

Informacje są widoczne dopiero dla poziomu `Scouted`. Tooltip nie może
wyciągać `BuildableProvince*` z authority tylko po to, by narysować treść;
rozszerzyć `GlobalMapNodeView` o display name, trait views i listę potencjalnych
zasobów.

### 3.7. Testy kolonizacji

Wymagane przypadki:

- pierwsza, druga i trzecia kolejna kolonizacja jednego gracza;
- dwie równoległe operacje na różnych targetach;
- odrzucenie drugiej na tym samym target;
- koszt pobrany tylko z jawnego source, nie z aktywnej/innej prowincji;
- brak WOOD, STONE albo PLANKS daje konkretny reason;
- refund po failed journey, cancel i błędzie generacji;
- brak podwójnego refundu;
- save/load podczas `Traveling` i `Establishing` zachowuje tick, koszt i fazę;
- po load operacja kończy się dokładnie raz;
- nowa prowincja ma osobne `TileMap`, `ProvinceEconomy`, `RoadNetwork` i
  kolejkę budowy;
- zmiana aktywnej prowincji nie wpływa na jej postęp;
- snapshot i checksum są równe po round-trip.

## 9. Etap 4 — ograniczony, przyszłościowy profil złóż

Użytkownik planuje dalszy rework generatora. Ten etap ma wprowadzić minimalny
kontrakt danych, który ten rework wykorzysta, a nie rozbudowany drugi algorytm.

### 4.1. Lista zasobów prowincji

- Dodać do `BuildableProvinceParameters` posortowaną, unikalną listę
  `naturalResourceTypes`/`depositTypes`.
- Używać istniejących `TileType` dla złóż oraz jednej wspólnej konwersji
  `TileType -> ResourceType` dla ikon. Nie pisać switcha w tooltipie.
- Lista jest losowana podczas generowania mapy globalnej, zapisywana w save i
  checksumie. Po kolonizacji nie losować jej drugi raz.
- Home province zawsze ma tylko:
  `WOOD`, `STONE`, `COAL`, `IRON_ORE`.
- Inna buildable province ma tę samą bazę, a każdy obecny specjalny typ
  (`COPPER_ORE`, `CLAY`, `SAND`) ma niezależne 500 basis points szansy.
- Losowanie jest deterministyczne z domeną `provinceId + resourceType`; dodanie
  przyszłego typu nie może zmienić rolli istniejących typów.
- Rzadki typ, jeśli wylosowany, dostaje jedną małą łatkę. Nie wymuszać minimum
  jednej łatki dla typu niewylosowanego.

### 4.2. Filtrowanie istniejącego generatora

- Nie duplikować `GeneratePatch`.
- Przy budowie `MapParameters` konkretnej prowincji filtrować istniejące
  `resourcePatches` według zapisanej listy.
- Zmienić skalowanie density tak, by `resourceDensity=0` nie oznaczało 50%
  bazowej liczby pól i by generator nie robił `max(1, ...)` dla każdej rodziny.
- Minimum dotyczy tylko czterech gwarantowanych pól startowych wokół HQ.

Pilotażowe wartości początkowe, trzymane w jednym miejscu:

| Złoże | Bazowe patche | Promień | Richness scale |
|---|---:|---:|---:|
| WOOD | 6 | 7–14 | 1.00 |
| STONE | 2 | 6–11 | 1.10 |
| COAL | 2 | 5–10 | 1.20 |
| IRON_ORE | 2 | 5–10 | 1.20 |
| rare, gdy obecne | 1 | 3–6 | 0.75 |

Punkt startowy parametrów mapy: density 0,18, field size 0,80, richness 180.
Suwaki nadal skalują te wartości; nie usuwać konfiguracji gracza.

### 4.3. Pola gwarantowane koło HQ

- Zachować cztery obecne typy i obecny wspólny planner.
- Nazwać zakresy odległości stałymi, zamiast trzymać anonimowe tablice.
- Pilotażowo przesunąć dwa bliższe pola z 17–23 na 20–26, dwa dalsze z 26–32
  na 29–35.
- Po zmianie ponownie zweryfikować starting village, limit 20–30 kafli drogi i
  wszystkie presety mapy. Jeśli preflight nie przechodzi dla istniejących
  bezpiecznych seedów, nie zwiększać `HeadquartersTerritorySize` w ciemno;
  najpierw poprawić wspólny planner.

### 4.4. Testy generatora

- Home province nie zawiera copper/clay/sand dla co najmniej 32 seedów.
- Nie-home province zachowuje listę depositów po save/load.
- Te same seed + ProvinceId dają identyczną listę.
- Zmiana kolejności katalogu zasobów nie zmienia rolla konkretnego typu.
- Kohorta co najmniej 512 prowincji ma częstość każdego rare w szerokim,
  niekruchym przedziale 2–8%; dodatkowo jeden fixed-seed golden test.
- Mapa ma znacznie mniej oddzielnych pól niż dotychczas, ale większą medianę
  pola i większą sumę richness na polu.
- Cztery gwarantowane pola są dostępne, spójne i nie kolidują z HQ/village.
- Data validator odrzuca duplikaty, Null i typ niebędący złożem.

## 10. Etap 5 — backend task grup bez duplikowania położenia

### 5.1. Model

Pliki nowe: `inc/warfare/TaskGroup.h`, `src/warfare/TaskGroup.cpp`.

```cpp
using TaskGroupId = std::uint64_t;

struct TaskGroup
{
    TaskGroupId id;
    ProvinceId stationProvinceId;
    int homeBarracksBuildingId;
};
```

- `TaskGroupId=0` jest invalid.
- ID jest monotoniczne w obrębie gracza. Pełna referencja to
  `(PlayerId, TaskGroupId)`; kolizja numeru pomiędzy graczami nie jest
  kolizją domenową.
- Grupa nie przechowuje kopii statystyk jednostek ani wektora członków.
- Dodać `taskGroupId` do `BattleUnit`. To jest jedyne źródło członkostwa.
- `UnitAssignment` pozostaje jedynym źródłem fizycznego położenia jednostki.
- `TaskGroup` przechowuje tylko macierzysty Barracks/province. Stan
  `Reserve`, `Garrison`, `Journey`, `Battle` jest wyprowadzany z assignments
  członków, nie cache'owany w grupie.
- Pusta grupa jest legalna i widoczna.
- Grupa pozostaje po ataku i garnizonie. Polegli znikają z `UnitRoster`, więc
  automatycznie znikają z członkostwa; nie ma dangling listy ID w grupie.

### 5.2. Registry i invariants

`Player` posiada jeden `TaskGroupRegistry` z uporządkowaną mapą i
`nextTaskGroupId`. Modyfikuje go `TaskGroupService`.

Invariants:

- każda niezerowa wartość `BattleUnit::taskGroupId` wskazuje istniejącą grupę
  tego samego gracza;
- przy dodawaniu jednostka jest w `BarracksReserve`, w tej samej prowincji i w
  tym samym Barracks co grupa;
- jednostka należy maksymalnie do jednej grupy;
- edycja grupy jest możliwa tylko, gdy jej wszyscy żywi członkowie są w
  macierzystej rezerwie;
- garrison/journey/battle nie kasuje członkostwa, ale blokuje edycję;
- po udanym transferze całej grupy registry atomowo zmienia
  `stationProvinceId` i `homeBarracksBuildingId` na cel;
- nie można przenieść części grupy i pozostawić jej w stanie mieszanym;
- usunięcie grupy zeruje `taskGroupId` wszystkich jej członków, ale nie zmienia
  ich `UnitAssignment`.

Limit:

- `MaxUnitsPerTaskGroup = GameCommand::MaxUnitInstanceIds` (obecnie 32);
- `MaxTaskGroupsPerPlayer = MaxGlobalProvinces * 4`;
- loader sprawdza limit łączny, ID, ownership i invariants.

### 5.3. Komendy

Dodać append-only wartości `GameCommandType`:

- `CreateTaskGroup(player, province, barracks)`;
- `AddUnitsToTaskGroup(player, groupId, unitIds)`;
- `RemoveUnitsFromTaskGroup(player, groupId, unitIds)`;
- `DisbandTaskGroup(player, groupId)`.

W `GameCommand` dodać jawne `taskGroupId`/`taskGroupIds`; nie pakować ID grupy
w `targetTileId` ani w string.

- Komenda zawsze przenosi konkretne, posortowane unit instance IDs. Semantyka
  „1” i „5” należy do GUI; authority nigdy nie wybiera losowych egzemplarzy.
- Wszystkie elementy zwalidować przed pierwszą mutacją.
- Add/remove/disband są atomowe.
- Nie pozwalać klientowi edytować grupy innego gracza ani grupy z innej
  prowincji przez podanie kolidującego lokalnego ID.

### 5.4. View, save i checksum

Dodać pointer-free `TaskGroupView`:

- ID, province, Barracks ID;
- wyprowadzony status;
- liczba jednostek według `unitDefId`;
- total, alive, editable;
- opcjonalne journey/battle/garrison ID, jeśli wszyscy członkowie mają wspólny
  stan.

Budować widok jednym skanem rosteru na gracza, nie skanować całego rosteru dla
każdego przycisku.

Zapisać:

- `nextTaskGroupId` i registry w save/correction;
- `BattleUnit::taskGroupId` przy każdej jednostce;
- registry i membership w checksumie;
- `TaskGroupView` w presentation snapshot.

Podbić command, snapshot i save version dokładnie w commicie zmieniającym
format. Dodać testy malformed count, unknown group, duplicate ID, dangling
membership, wrong province/barracks i monotonic next ID.

## 11. Etap 6 — wspólne karty jednostek, Barracks i roster

### 6.1. Wspólny renderer kart

Wydzielić z obecnego Barracks jeden helper/model, np.
`UnitTypeCardView` + `DrawUnitTypeCard`. Używać go w Barracks i rosterze.

Karta:

- układ 4 kolumny;
- gap 8 px;
- rozmiar `clamp(floor((contentWidth - 3*gap)/4), 48, 104)`;
- portret inset 9 px;
- główny badge w prawym dolnym rogu: 24–30 px, ciemne koło, jasna liczba;
- drugi licznik w prawym górnym rogu: `(N)` amber/green, tylko gdy N > 0;
- tooltip korzysta ze wspólnego opisu jednostki;
- nie rysować dodatkowego jasnego `DrawRectangleLinesEx` na hover. Asset
  `DrawPixelHudWidgetFrame(card, hovered)` i tooltip są wystarczające.

Znaczenie liczb:

- Barracks: główna liczba = żywe jednostki w reserve dokładnie tego Barracks;
  `(N)` = liczba wpisów tego typu w jego recruitment queue.
- Roster: główna liczba = wszystkie jednostki tego typu obecne w aktywnej
  prowincji; przy wybranej grupie `(N)` = członkowie tego typu w tej grupie.
- Tooltip rosteru pokazuje rozbicie: ungrouped reserve, selected group, other
  groups, garrison, journey/battle.

### 6.2. Scroll queue Barracks

- Obszar kolejki ma stałą wysokość na 3,5 wiersza, minimum 126 px, maksimum
  176 px zależnie od wysokości panelu.
- Jeden wiersz: nazwa/status 18 px, progress 10 px, gap 8 px; razem 38–42 px.
- Scissor obejmuje tylko viewport kolejki.
- Pasek pionowy 8 px, odstęp 6 px od treści, thumb minimum 28 px.
- Wheel działa tylko, gdy mysz jest nad viewportem. Drag thumb nie może
  przechodzić do mapy.
- Waiting for resources ma amber i pusty pasek; aktywny recruit ma zielony
  fill oraz `X.X s`.
- Użyć wspólnego `VerticalScrollState/UiScrollViewport`, nie kopiować kodu ze
  stockpile.

### 6.3. Layout rosteru

Zachować zewnętrzny panel: x=6%, y=15%, width=88%, height=82%.

Wewnątrz:

- chrome inset + 24 px marginesu;
- content top 70 px, content bottom 24 px;
- lewa kolumna 42% dostępnej szerokości;
- separator 1 px, z 18 px pustego miejsca po obu stronach;
- prawa kolumna zajmuje resztę;
- obie kolumny mają osobne pionowe viewporty, jeśli treść nie mieści się.

Lewa strona:

- tytuł `Units in <province name>` 20 px;
- wspólna siatka 4 kolumn;
- bez wybranej task grupy kliknięcie niczego nie rekrutuje ani nie przenosi;
- tooltip jasno podaje sterowanie.

Prawa strona:

- tytuł `Task groups — Barracks #N`;
- pokazywać wyłącznie grupy aktywnej prowincji;
- jeśli prowincja ma kilka ukończonych Barracks, dodać prosty selector nad
  grupami; domyślnie najniższe stabilne building ID;
- grid grup: dwie kolumny, gap 10 px, karta minimum 180x68 px, maksimum
  260x82 px;
- pierwsza wolna karta to zielony `+`; tworzy pustą grupę przypisaną do
  wybranego Barracks;
- wybrana grupa: mocny gold/amber border i ciemniejszy selected fill;
- karta pokazuje `Task group N`, total units, status i Barracks;
- mały przycisk `X` disband ma własny tooltip i nie odpala wyboru karty.

Sterowanie po wybraniu grupy:

- LMB na typie: dodaj 1 najniższe stabilne ID z ungrouped reserve;
- RMB: dodaj do 5;
- Ctrl+LMB: usuń 1 najniższe stabilne ID tego typu z grupy;
- Ctrl+RMB: usuń do 5;
- brak dostępnych sztuk nie wysyła pustej komendy;
- wszystkie wybrane IDs są posortowane przed submit;
- grupa w journey/battle/garrison pokazuje status i jest read-only.

### 6.4. Gating Roster i Global Map

- Dodać jeden query `HasCompletedBarracks(player)` korzystający z istniejącego
  trackera wszystkich prowincji.
- Oba przyciski są disabled do ukończenia przynajmniej jednego Barracks.
- Warunek obowiązuje identycznie dla renderu, click dispatch, `U`, `M` i
  `GuiSystem::CanActivate`.
- Disabled click zostaje skonsumowany i nie przechodzi do mapy.
- Tooltip: `Requires a completed Barracks`.
- Nie uzależniać gate od Barracks w aktywnej prowincji; gracz ma odblokować
  panele globalnie. Konkretna operacja nadal wymaga właściwego lokalnego
  Barracks.

### 6.5. Top HUD

- Usunąć tylko przycisk Logistics overlay z action stripu, jego rect, hover,
  click branch i tooltip.
- Zachować samą funkcję overlay oraz binding `L`, dopóki użytkownik nie nakaże
  usunąć całego narzędzia diagnostycznego.
- Zmniejszyć action count z 9 do 8 i nadać sloty przez jeden enum/kolejność:
  Build, Destroy, Road, Stats, Roster, Decisions, Technology, Global Map.
- Wszystkie named rect helpers mają delegować do jednego algorytmu. Nie
  wpisywać nowych indeksów osobno w draw, hit test i dispatch.
- Wydzielić `StrategicHudContentRect`: lewy i prawy inset to
  `128 * topHudCornerScale + 16 px`.
- Pierwszy lewy chip zaczyna się na `contentRect.x`; ostatni action button
  kończy dokładnie na `contentRect.right`. Dzięki temu ornamenty mają
  symetryczną przestrzeń, a Global Map przesuwa się w lewo.
- Gap przycisków pozostaje 4 px, wysokość 44–60 px.
- Geometry tests: pierwszy/ostatni rect mieści się w content rect i żadne
  buttony nie nachodzą na siebie dla 1280, 1600, 1920 i 2560 px.

## 12. Etap 7 — atak, szybki garnizon i transporty

### 7.1. Atak task grupami

- Global-map Attack otwiera jeden `GlobalMapOperationDialogWidget` niezależnie
  od rodzaju celu.
- Dialog pobiera task grupy stacjonujące w source province i gotowe w
  Barracks reserve.
- Można wybrać jedną lub kilka grup. Authority rozwija IDs grup do
  posortowanej, unikalnej listy unit IDs i dopiero wywołuje istniejący
  `BattleLifecycleSystem::StartProvinceAttack`.
- Komenda ataku powinna przenosić `taskGroupIds`, nie listę wskazaną przez UI.
  Stary factory z unit IDs może pozostać wyłącznie dla testów/kompatybilności
  wewnętrznej, ale zewnętrzna ścieżka UI używa grup.
- Walidacja sprawdza, że wszystkie grupy są jednego gracza, w source,
  kompletne, editable i nie przekraczają limitu jednostek bitwy.
- Czas ataku pochodzi z najwolniejszej jednostki całej sumy grup.
- Po bitwie istniejące release do source reserve zachowuje `taskGroupId`.

Dialog ataku:

- width `clamp(760, windowWidth-40, 920)`;
- height `clamp(560, windowHeight-40, 680)`;
- margines 18 px;
- nagłówek 44 px;
- source/target + ETA: 2 wiersze po 22 px;
- lista grup: viewport 190–260 px, wiersz 54 px;
- summary: total units, slowest speed, physical/effective distance, ETA;
- Confirm 190x36, Close 138x36.

### 7.2. Wizualny draft żywności i mieczy

To nie jest backend ekwipunku. W dialogu przechowywać lokalny
`ExpeditionLoadoutDraft`; nie dodawać go do `GameCommand`, save, checksum ani
stockpile mutation.

- Dwa wiersze: `FOOD_PROVISIONS` i `IRON_SWORD`.
- Ikona 30x30, nazwa, slider, liczba po prawej.
- Food placeholder recommendation:
  `ceil(unitCount * max(1.0, travelMinutes) * 0.25)`.
- Iron sword placeholder recommendation: `unitCount`; slider może zostać
  zmniejszony do zera.
- Maksimum wizualne ograniczyć aktualnym stanem źródłowego stockpile.
- Podpis pod sekcją: `Draft loadout — not consumed by the current combat
  backend`.
- Confirm ataku ignoruje oba slidery. Test ma sprawdzić, że zmiana slidera nie
  zmienia serializowanej komendy ani zasobów.

### 7.3. Szybki garnizon task grupą

- W panelu obronnym zastąpić obecne „Assign reserve” wyborem gotowej task
  grupy z aktywnej prowincji.
- Pokaż tylko grupy mieszczące się w wolnych slotach.
- Komenda może nadal końcowo użyć istniejącego
  `AssignUnitsToGarrison`, ale GameWorld ma rozwinąć group ID i zwalidować
  całość przed pierwszym assignmentem.
- Grupa zachowuje membership; status wynika z `DefensiveGarrison`.
- `Return group to Barracks` wysyła wszystkich żywych członków grupy i wraca
  do jej `homeBarracksBuildingId`.
- Nie wybierać automatycznie `barracks.front()` bez pokazania użytkownikowi,
  jeżeli grupa pochodzi z innego Barracks.

### 7.4. Transport zasobów między własnymi prowincjami

Dodać jeden `ProvinceTransferService`, nie kopiować `TradeService`.
Może współdzielić niskopoziomowy adapter stockpile, ale nie nazywać transportu
handlem.

Payload:

```cpp
struct ResourceConvoy
{
    std::vector<ResourceAmount> cargo;
};
```

Przebieg:

1. source i target są różnymi owned buildable provinces gracza;
2. quote używa jednej ścieżki i `JourneyTiming`, convoy speed 100/min;
3. authority sprawdza wszystkie ilości i pojemność limitów;
4. całość cargo zostaje pobrana atomowo ze stockpile source;
5. journey przechowuje cargo, więc save w drodze jest kompletny;
6. po sukcesie `StockpileIndex::Deposit` odkłada zasoby do target;
7. jeśli target nie mieści całości, journey przechodzi `AwaitingUnload`,
   przechowuje resztę i ponawia bez utraty zasobów;
8. fail/cancel przed dostawą zwraca pozostałe cargo do source;
9. każda końcówka jest idempotentna — brak podwójnego deposit/refund.

Komenda ma jawny `std::vector<ResourceAmount>`, limit typów, dodatnie ilości,
unikalne i posortowane `ResourceType`. Nie używać pól trade offer/request.

Dialog Transport Resources:

- dostępny po kliknięciu własnej prowincji różnej od source;
- width 620 px, height 500 px, clamp do viewportu;
- source/target/ETA u góry;
- resource viewport 280 px;
- wiersz 42 px: ikona 28, nazwa, source available, `-`, amount, `+`, `Max`;
- LMB krok 1, RMB na +/- krok 5; wartość nie przekracza source amount;
- summary physical/effective distance i ETA;
- Confirm disabled dla pustego cargo.

### 7.5. Transport całej task grupy

Dodać osobny `ArmyTransfer` payload zawierający task group IDs, snapshot unit
IDs i destination Barracks ID. Nie udawać trade cargo.

- Opcja jest widoczna dla własnej innej prowincji.
- Enabled tylko, jeśli source ma gotową grupę, a target ma ukończony Barracks.
- Jeśli target ma kilka Barracks, dialog pokazuje selector; domyślnie najniższe
  stabilne ID.
- Grupa musi być wysłana w całości.
- Units przechodzą do `Journey`; membership pozostaje.
- Po sukcesie wszystkie units trafiają do target `BarracksReserve`, a metadata
  każdej grupy zostaje przestawiona na target province/Barracks atomowo.
- Fail/cancel wraca do source i nie zmienia metadata grupy.
- Prędkość to minimum wszystkich przewożonych jednostek.
- Nie używać `activeProvinceId` w completion handlerze.

### 7.6. Debugowy najazd F2

Prerekwizyt jest spełniony dopiero po przejściu testów istniejącego
`BattleLifecycleSystem`, garnizonów i `RaidDamage`.

- Dodać `GameAction::SpawnDebugRaid` z domyślnym `F2` i nazwą klawisza.
- To mutacja symulacji, więc ma być append-only `GameCommandType`, nie lokalne
  wywołanie z GUI.
- Authority akceptuje tylko `debugMode`, własną buildable province i dodatnią
  siłę; pilot strength 20.
- Targetem jest jawny active province ID wstawiony do komendy przez UI.
- Handler wywołuje istniejący `BattleLifecycleSystem::StartRaid`.
- Działa w SP, host i klient->host; checksum/snapshot pokazują tę samą bitwę.
- Controls pokazuje `F2 — Spawn debug raid` tylko w sekcji debug.

### 7.7. Testy transportów i operacji

- Resource transfer: success, no path, non-owner, empty/duplicate cargo,
  insufficient source, partial unload, retry, cancel/refund, save mid-route,
  save AwaitingUnload, checksum.
- Army transfer: missing destination Barracks, mixed/empty group, slowest unit,
  multi-group, fail return, success rebind, save/load.
- Attack: wszystkie rodzaje celu używają tego samego dialog modelu; group IDs
  są walidowane przez authority; task group zajęta nie może ruszyć drugi raz.
- Garrison: group assign/return, capacity, ownership, province collision,
  casualty leaves group consistent.
- Debug raid: non-debug rejected, debug accepted, F2 binding, MP command
  round-trip.
- Pełny Debug/Release i data validator.

## 13. Etap 8 — sidebar kampanii, journal, demolish i kolorystyka

### 8.1. Sidebar zamiast dwóch niezależnych paneli

TODO sugeruje `VBox`, lecz obecny `VBox` rozdziela dzieci po równo i dolicza
margin poza rozmiarem, więc nie obsłuży dynamicznej listy + journalu. Nie
wciskać tych paneli do niego przez przypadkowe offsety. Zastosować prosty
`CampaignSidebarWidget`, który jest właścicielem dwóch dzieci i układa je w
jednej kolumnie. Jeśli Luna rozszerzy `VBox`, musi zachować dotychczasowy tryb
equal i dodać przetestowany tryb content-height; nie wolno zmienić layoutu
pozostałych scen.

Layout sidebaru:

- x = 18 px;
- y = dół strategic HUD + 16 px;
- width = `clamp(windowWidth * 0.20, 300, 380)`;
- gap paneli = 8 px;
- dolny margines ekranu = 18 px.

Owned provinces:

- header 38 px;
- padding ramki 6 px;
- row 34 px;
- gap row 4 px;
- wysokość rozwija się dokładnie do zawartości, ale maksymalnie do 38% wysokości
  okna; potem działa scroll;
- button nie może wychodzić na 9-slice frame;
- aktywna prowincja: `SelectedFill`, 2 px Gold border i 3 px lewy pasek
  AmberBright; sam hover pozostaje słabszy;
- kolejność rosnący `ProvinceId`;
- dane pobierać z `latestSnapshot.globalMapView`, nie budować drugiego view w
  każdej klatce.

Journal:

- dokładnie ta sama szerokość i lewa krawędź;
- collapsed height 62 px;
- expanded height `min(360 px, pozostałe miejsce do dolnego marginesu)`;
- header 58–62 px;
- event viewport zaczyna się 70 px od góry;
- wiersz compact 42 px; rozwinięta treść ma dynamiczną wysokość i scissor;
- max pięć bez scrolla nie jest już hardkodem; pokazać historię z ograniczonego
  snapshotu i przewijać.

### 8.2. Alerty prowincji

- Zdarzenie historyczne nie jest permanentnym alertem.
- Widget utrzymuje lokalny, nieserializowany `lastAcknowledgedEventId` per
  province.
- Przy pierwszej klatce po wejściu/loadzie zainicjalizować go aktualnym max ID,
  aby nie oznaczyć całej starej historii jako nowej.
- Nowe eventy ustawiają alert; kliknięcie prowincji lub otwarcie journalu dla
  tego wpisu go czyści.
- Battle alert jest aktywny tylko dla statusu trwającego/oczekującego albo
  nieprzeczytanego nowego reportu, nie dla całej zachowanej historii.
- Unsupplied garrison pozostaje alertem tak długo, jak stan istnieje.

### 8.3. Timestamp i efekty eventu

Wydzielić jeden UI formatter ticków:

- fixed tick: 100 Hz;
- poniżej doby: `[HH:MM:SS]`;
- od drugiej doby: `[D2 HH:MM:SS]`;
- duration w efektach: sekundy z jedną cyfrą dla wartości <10 s, potem pełne
  sekundy/minuty; nigdy surowe ticks.

Dodać do notification `appliedEffects`, używając typed value, nie gotowego
tekstu. `ProvinceEventSystem::ApplyEffects` zapisuje rzeczywiście wykonaną
wartość:

- deposit: return value `StockpileIndex::Deposit`;
- consume/damage: rzeczywiście pobrana różnica;
- trade score: delta before/after;
- destroyed buildings: rzeczywista liczba;
- timed modifier: actual multiplier/additive i faktyczny czas
  `endTick-startTick`;
- unit loss/raid: uzupełnić notification, gdy efekt zostanie faktycznie
  rozstrzygnięty.

Feed potrzebuje małej metody aktualizującej wpis po `instanceId`. Nie
przepisywać tekstu description ani nie parsować go w GUI.

Render efektów:

- resource gain: ikonka 22x22, `+5 Wheat`, SageBright;
- strata: ikonka, `-N`, RustBright;
- multiplier: `Production output +10% for 60 s`;
- additive: znak i wartość przez wspólny formatter `BalanceStat`;
- trade score: `Trade score +1`;
- raid/building loss: tekst RustBright;
- brak efektu faktycznie zastosowanego: `No effective change` w kolorze dim.

`frontier_harvest` z definicyjnym +5, ale tylko dwoma wolnymi slotami, ma
pokazać `+2 Wheat`, nie `+5`.

### 8.4. Demolition tooltip

- Nie przeliczać salvage w GUI. Użyć istniejącego `DemolitionPreview` z
  `BuildingSalvage`.
- Każdy wiersz zasobu: ikona 24x24, nazwa, buffered, refund z kosztu budowy,
  total returned i ewentualny lost amount.
- Tekst przykładowy:
  `Wood: 8 buffered + 10 build refund = 18 returned`.
- Strata pojemności na czerwono.
- Niedokończony budynek nadal pokazuje 100% cancellation refund, ukończony
  aktualną regułę 50%.
- Jeśli nic nie wraca, pokazać jeden wiersz `No resources recovered`.
- Tooltip korzysta z finalnego overlay passu.

### 8.5. Ciemniejsze tła i separator filtrów

- Nie zmieniać pojedynczych literalnych brązów w kilkunastu miejscach.
- Dodać w `UiTheme` wspólne, ciemniejsze role:
  `PanelBackdrop`, `TooltipBackdrop`, `TreeCanvasBackdrop`.
- Zachować metalowe ramki i tekst. Nie przyciemniać parchmentu global mapy ani
  assetów świata.
- Dla teksturowanych 9-slice narysować jednolity ciemny inner scrim wewnątrz
  chrome inset; nie tintować narożników na czarno.
- Focus/Technology: wypełnić wyłącznie `treeArea` ciemnym backdropem.
- Separator filtrów: linia 1 px, od `bounds.x+24` do `bounds.right-24`,
  `y=bounds.y+98`, `Fade(UiTheme::Bronze, 0.65)`; 6 px oddechu nad i pod.
- Ten sam kod/layout obsługuje Focus i Technology.

### 8.6. Bramka etapu 8

- Screenshot referencyjny 1920x1080: sidebar po lewej, journal bez surowych
  ticków, żaden panel nie zasłania karty global mapy.
- Hover tooltip nad: journalem, listą prowincji, filtrami stats i ikonami
  produktów na dole.
- Lista 2, 7, 50 i 500 prowincji: frame rośnie do limitu, potem scroll, journal
  przesuwa się i nadal mieści na ekranie.
- Event effects po save/load pozostają identyczne.
- Full Debug/Release, snapshot/persistence tests.

## 14. Serializacja, limity i kompatybilność

Zmiany formatów robić razem z kodem i testami danego etapu, nie jednym
„version bump” na końcu.

### 14.1. GameCommand

Podbić przy pierwszym dodaniu pól/komend:

- task group IDs;
- resource cargo;
- destination Barracks ID;
- debug raid.

Enumy są append-only; nie używać historycznych dziur. Parser waliduje count
przed resize, dodatnie IDs, unikalność, typy zasobów i trailing data.

### 14.2. GameSnapshot

Podbić przy zmianach:

- `ProvinceEdgeView` i `GlobalMapNodeView`;
- `WorldJourneyKind`/timing view;
- `TaskGroupView`;
- applied event effects;
- colonization/transfer status potrzebny GUI.

Każdy nowy wektor ma limit z `PersistenceLimits`; nie ufać countowi z sieci.

### 14.3. Save/correction state

Podbić przy zmianach:

- length traktu;
- plan podróży i nowe payloady;
- faza kolonizacji;
- natural resource list prowincji;
- task group registry/membership;
- applied event effects.

Repo obecnie świadomie akceptuje tylko dokładną bieżącą wersję save. Nie
dodawać połowicznej kompatybilności wstecz bez osobnego polecenia. Loader ma
odrzucić stary/malformed zapis kontrolowanie, bez częściowej mutacji świata.

### 14.4. Checksum

Hashować w stabilnej kolejności wszystkie nowe gameplay fields. Nie hashować:

- rozwinięcia paneli;
- scroll offsetów;
- wybranej task grupy w GUI;
- local draft food/swords;
- acknowledged journal IDs;
- hover/tooltip state;
- active presentation province, jeśli obecny kontrakt nadal trzyma ją poza
  deterministycznym stanem.

## 15. Macierz testów ręcznych

### 15.1. Prowincje i równoległość

- Zbudować produkcję w prowincjach A, B i C.
- Oglądać A przez 60 s; B i C nadal produkują, transportują i rekrutują.
- Zmieniać aktywną prowincję przy pauzie: mapa/teren/budynki odświeżają się
  natychmiast bez ruchu kamery.
- Rozpocząć kolonizację B i C z różnych źródeł; zmiana widoku nie zmienia ETA.

### 15.2. Save/load

- w połowie route upgrade;
- w połowie wieloodcinkowej podróży;
- podczas fazy Establishing kolonizacji;
- z resource convoy w drodze;
- z convoy AwaitingUnload;
- z army transfer w drodze;
- z task grupą w garnizonie i w bitwie;
- z aktywnym timed eventem.

Po load: ten sam progress %, ETA, cargo, membership, koszt już pobrany i brak
podwójnej finalizacji.

### 15.3. Multiplayer

- Host i klient tworzą własne task grupy o lokalnym ID 1; nie ma kolizji.
- To samo lokalne building ID w różnych prowincjach nie daje dostępu do cudzej
  grupy/Barracks.
- Client wysyła atak, resource transfer, army transfer i F2 debug raid;
  authority waliduje i obie strony mają ten sam checksum.
- Wymusić resync w połowie każdej nowej operacji.

### 15.4. GUI

- Rozdzielczości 1280x720, 1600x900, 1920x1080, 2560x1080.
- DPI 100%, 125%, 150%.
- Sidebar 2/7/50/500 prowincji.
- Roster: brak grup, grupa pusta, pełna 32, wiele Barracks, grupa deployed.
- Barracks queue 0/1/4/20 wpisów.
- Global-map tooltip blisko każdej krawędzi.
- Top HUD: brak overlap ornamentów; Roster/Map locked i unlocked.
- Kamera: wszystkie narożniki przy minimalnym i maksymalnym zoomie.

## 16. Zalecana kolejność commitów

1. `test: reproduce post-rework colonization, geometry and debug regressions`
2. `fix: defer tooltips and remove duplicate global-map update`
3. `fix: defense center, camera bounds and debug province count`
4. `refactor: introduce canonical route length and journey timing`
5. `fix: split colonization travel and settlement phases`
6. `feat: expose route and buildable-province operation quotes`
7. `feat: persist deterministic province resource profiles`
8. `feat: add provincial task-group registry and commands`
9. `ui: share unit cards and rebuild barracks/roster panels`
10. `feat: attack and garrison through task groups`
11. `feat: add owned-province resource and army transfers`
12. `feat: add authoritative F2 debug raid command`
13. `ui: stack campaign sidebar and enrich event journal`
14. `ui: finish demolition, theme and HUD polish`
15. `test: complete save, snapshot, multiplayer and UI regression matrix`

Nie łączyć commitów 4, 5, 8 i 11 w jeden diff. Każdy z nich zmienia niezależny
invariant i musi być możliwy do review osobno.

## 17. Definicja ukończenia

Task jest ukończony dopiero, gdy:

- druga i trzecia kolonizacja działa, także po save/load;
- wszystkie czasy globalnych podróży używają długości, jakości traktu,
  wspólnej kary 10% per dodatkowy leg i właściwej prędkości movera;
- nie istnieje druga kopia wzoru czasu w trade/scout/attack/UI;
- tooltip traktu pokazuje length/current/next/cost/time/safety/progress;
- tooltip buildable province pokazuje traits/resources/cost/travel/settle/total;
- task grupy są prowincjonalne, przypisane do Barracks, trwałe i authority-safe;
- Barracks i roster używają tej samej karty jednostki;
- atak, garrison i transfer wojsk działają na grupach;
- suwaki food/sword są jawnie presentation-only i niczego nie konsumują;
- resource convoy zachowuje cargo i progress po save/load;
- journal ma timestampy i faktycznie zastosowane efekty;
- sidebar i tooltipy mają poprawne warstwy dla małej i bardzo dużej liczby
  prowincji;
- Roster/Global Map są faktycznie zablokowane bez ukończonego Barracks,
  również z klawiatury;
- debug 32 generuje 32, a F2 tworzy autorytatywny raid tylko w debug mode;
- okrąg obrony jest w środku footprintu;
- kamera nie pokazuje czarnego paska, a najdalszy dawny zoom jest niedostępny;
- podstawowa prowincja ma tylko cztery bazowe złoża, a rare profile jest
  deterministyczny i zapisany;
- pełne testy Debug i Release oraz data validator przechodzą;
- `git diff --check` jest czysty;
- raport końcowy wymienia wszystkie wersje formatów i niewykonane testy
  ręczne.

## 18. Czego nie robić w ramach tego planu

- Nie projektować finalnego systemu ekwipunku i zaopatrzenia armii.
- Nie konsumować food/swords z wizualnych sliderów.
- Nie dodawać automatycznego wyboru najszybszej trasy bez osobnego projektu.
- Nie przepisywać generatora terenu, biomów i kształtu złóż od zera.
- Nie kompresować save w tym tasku; istnieje osobny dokument skalowania.
- Nie przenosić audio ani logiki GUI na wątek symulacji.
- Nie wprowadzać ECS, event busa do każdego kliknięcia ani abstrakcyjnej
  fabryki widgetów.
- Nie serializować tekstu sformatowanego przez GUI jako stanu domenowego.
- Nie utrzymywać równolegle starego i nowego modelu journeys/task groups „na
  wszelki wypadek”. Migracja ma kończyć się jednym źródłem prawdy.
