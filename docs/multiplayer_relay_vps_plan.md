# Plan naprawy multiplayera i migracji do relaya VPS

## Status dokumentu

- Stan kodu: 2026-08-04.
- Zakres: naprawa obecnego host-client multiplayera, reconnect i obsługa latencji,
  własny relay/lobby na małym VPS oraz późniejsza integracja ze Steam.
- Ten dokument opisuje plan. Nie oznacza, że wymienione elementy są już zaimplementowane.
- Punktem wyjścia są znaleziska `F-04`–`F-08` oraz zadania `AUD-03`, `AUD-05` i
  `AUD-06` z `docs/code_audit_2026-08-02.md`.

### Dziennik wdrożenia

- 2026-08-04 — `MP-001` rozpoczęty: dodano ręcznie sterowany
  `FaultInjectingGameTransport` oraz testy delay/jitter/stall/disconnect/duplicate.
  Nie jest jeszcze podłączony do pełnego testu `HostSession` + `ClientSession`;
  będzie podstawą testów initial sync i reconnectu.
- 2026-08-04 — `MP-100` wdrożony dla obecnego TCP: `TcpGameTransport` używa
  teraz `NetworkProtocolCodec` (24-bajtowy nagłówek big-endian, typ wiadomości,
  kanał, długość i sekwencja), bez newline framingu. Test loopback potwierdza
  przesłanie ramki zawierającej newline i `NUL`.
- 2026-08-04 — `MP-102` częściowo wdrożony dla starego protokołu: limity
  `GameCommand`, `GameCommandResult`, `GameServerFrame` i render snapshotów
  chronią parsery przed nieograniczonym `reserve`.
- 2026-08-04 — `MP-104` rozpoczęty: dodano niezależny transfer snapshotu z
  manifestem `(id, tick, bytes, chunkCount, hash)`, chunkami 32 KiB, jawnym
  `End`, listą brakujących chunków i limitem 64 MiB. Równolegle current
  `INIT_BEGIN/CHUNK/END` odtwarza już pełny `GameWorld` przez stan zapisu v32
  (tick, runtime ids, aktywny focus, dane produkcji i pociski), zamiast sam
  `GameSnapshot` renderera. Odbiornik ma limit 64 MiB/6000 chunków, odrzuca
  sprzeczne duplikaty i zachowuje rollback poprzedniego świata po błędnym
  odtworzeniu. Nadal brakuje podłączenia manifestowego `SnapshotTransfer` oraz
  ACK/NACK/event replay dla reconnectu.
- 2026-08-04 — `MP-301` rozpoczęty: host pamięta do 2048 wyników zdalnych
  komend i przy duplikacie `commandId` odsyła poprzedni wynik bez drugiego
  wykonania. Klient pamięta niepotwierdzone własne komendy, odtwarza je po
  przejściu transportu `Disconnected -> Connected` i nie aplikuje drugi raz
  zduplikowanego wyniku. To jest jeszcze model jednego slotu LAN; nie ma
  `PlayerSessionId`/resume tokenu ani automatycznego reconnectu socketu TCP.

## Decyzja architektoniczna

Gra pozostaje w modelu **host-authoritative**:

- host uruchamia właściwy `GameWorld` i jest jedynym autorytetem;
- klienci utrzymują deterministyczne mirrory świata;
- klient wysyła intencję (`GameCommand`), a host waliduje ją, przypisuje tick i
  rozsyła wynik jako autorytatywny event;
- relay VPS nie symuluje gry i nie interpretuje zasad gameplayu; identyfikuje
  sesję, przydziela slot i przekazuje wiadomości między hostem a klientami;
- przerwanie połączenia hosta zatrzymuje pokój. W pierwszej wersji nie będzie
  migracji hosta ani kontynuacji meczu bez hosta.

W proof of concept każde połączenie jest wychodzące do VPS:

```text
Host ──TCP──► VPS lobby/relay ◄──TCP── Klient
```

Po integracji ze Steam logiczny model zostaje taki sam, ale transport może zostać
podmieniony na `ISteamNetworkingSockets`. Połączenie klient-host może wtedy być
prowadzone przez Steam Datagram Relay bez ujawniania adresów IP. Własny VPS może
pozostać tylko jako usługa pomocnicza albo zostać całkowicie zastąpiony przez
Steam Matchmaking & Lobbies.

Najważniejsza zasada projektu: **protokół gry, lobby i fizyczny transport są trzema
oddzielnymi warstwami**. Dzięki temu wdrożenie VPS nie przywiąże gry na stałe do
surowego TCP, a integracja Steam nie wymusi ponownego pisania synchronizacji.

## Docelowy podział odpowiedzialności

### Protokół gry

Wspólny dla połączenia bezpośredniego, VPS i Steam. Odpowiada za:

- handshake wersji i zgodności danych;
- komendy, autorytatywne wyniki i kolejność eventów;
- ping, estymację ticka hosta i parametry input delay;
- initial sync, checksum, resync i reconnect;
- limity wiadomości, identyfikatory sesji i deduplikację.

Nie może zależeć od Winsock, adresu IP, SteamID, raylib, UI ani implementacji lobby.

### Transport

Transport przenosi wersjonowane wiadomości i raportuje stan połączenia. Planowane
implementacje:

- `DirectTcpTransport` — lokalne testy i awaryczne LAN;
- `VpsRelayTransport` — proof of concept z własnym relayem;
- `SteamNetworkingTransport` — docelowy transport Steam/SDR.

Transport nie zna `GameWorld` i nie podejmuje decyzji gameplayowych.

### Lobby

Lobby tworzy/listuje pokoje, przechowuje ich metadane, wydaje krótkotrwałe tokeny
dołączenia i informuje transport, z kim zestawić sesję. Planowane adaptery:

- `VpsLobbyService` — publiczne/prywatne pokoje w proof of concept;
- `SteamLobbyService` — `ISteamMatchmaking` w wydaniu Steam.

Steam Lobby samo nie przesyła ruchu gry. Służy do odkrywania pokojów, zaproszeń i
metadanych; ruch gry nadal obsługuje Steam Networking albo własny transport.

## Etap 0 — kontrakt i testowalny fundament

### MP-000 — zamrozić zasady i terminologię

Zapisać krótki ADR z następującymi decyzjami:

- host jest autorytetem;
- relay nie przejmuje symulacji;
- reconnect nie oznacza host migration;
- komendy i autorytatywne eventy są reliable i ordered;
- telemetryczne tick/checksum może być koaleskowane;
- initial sync i recovery używają pełnego `SimulationState`, nie render snapshotu;
- backend transportowy jest wymienny.

### MP-001 — fault-injection transport

Dodać testowy dekorator transportu obsługujący:

- stały RTT i jitter;
- zatrzymanie ruchu na określony czas;
- rozłączenie i ponowne połączenie;
- duplikację wiadomości na warstwie aplikacyjnej;
- ograniczenie przepustowości i kolejki.

Nie trzeba idealnie emulować Internetu. Celem jest powtarzalne odtworzenie błędów
w testach bez ręcznego uruchamiania dwóch klientów.

### Kryteria ukończenia etapu 0

- test potrafi uruchomić host i klienta z wirtualnym RTT 20/80/200 ms;
- test potrafi przerwać kanał na 10 sekund i przywrócić go bez tworzenia nowych
  obiektów świata ręcznie;
- scenariusze korzystają z tego samego API co prawdziwy transport.

## Etap 1 — naprawa obecnego multiplayera

### MP-100 — nowy framing i typowane wiadomości

Zastąpić linie `C ...`, `F ...`, `S ...` stałym nagłówkiem binarnym. Minimalny
nagłówek powinien zawierać:

```text
magic | protocolVersion | messageType | channel | flags |
payloadBytes | connectionSequence
```

Liczby mają mieć jawny endian. Parser najpierw waliduje nagłówek i limit, dopiero
potem alokuje payload. TCP jest strumieniem bajtów, więc parser musi obsłużyć:

- część nagłówka w jednym `recv` i resztę w kolejnym;
- kilka wiadomości w pojedynczym `recv`;
- payload wysyłany przez wiele częściowych `send`;
- zamknięcie połączenia w połowie wiadomości.

Kanały logiczne:

- `Control` — hello, welcome, ping, disconnect, resume;
- `Command` — intencje klientów;
- `Event` — autorytatywne wyniki hosta;
- `Snapshot` — begin/chunk/end/ack;
- `Lobby` — pokoje, chat i ready state.

TCP nadal ma jedną kolejność bajtów, ale priorytetowe kolejki zapobiegają
dokładaniu nowych snapshot chunków, gdy czekają control/command/event.

### MP-101 — handshake zgodności

Przed wygenerowaniem świata klient i host uzgadniają:

- protocol version;
- wersję gry;
- wersję `GameCommand` i `SimulationState`;
- hash deterministycznych plików `.rtsdata`;
- hash/wersję generatora mapy;
- parametry mapy, seed i listę slotów;
- role `Host`/`Client` oraz przydzielony `playerId`.

Niezgodność kończy handshake czytelnym powodem, bez częściowego uruchomienia gry.

### MP-102 — limity i backpressure

Wszystkie limity mają być konfigurowalne i sprawdzane również po stronie klienta.
Startowe wartości dla POC:

| Limit | Wartość startowa |
|---|---:|
| Zwykły frame protokołu | 64 KiB |
| Snapshot chunk | 32 KiB |
| Cały skompresowany snapshot | 64 MiB |
| Kolejka zwykłych danych na peer | 1 MiB |
| Okno niewysłanych snapshot chunków | 256 KiB |
| ID jednostek w jednej komendzie | 1 024 |
| Komendy gracza | 64/s, krótki burst 128 |
| Wiadomości lobby/chat | 16/s |

Gdy kolejka się zapełni:

- nie wolno usuwać komend ani autorytatywnych eventów;
- telemetryczny tick można zastąpić nowszym;
- snapshot zatrzymuje produkowanie kolejnych chunków do czasu opróżnienia okna;
- peer, który stale przekracza limity, dostaje jawny błąd i jest rozłączany;
- limity liczymy w bajtach, nie w liczbie obiektów lub linii.

### MP-103 — pełny `SimulationState`

Kontynuować `AUD-03`: zbudować jeden wersjonowany stan symulacji używany przez:

- checksum;
- save/load;
- initial sync;
- reconnect i resync.

`GameSnapshot` pozostaje wyłącznie `RenderSnapshot` albo zostaje tak nazwany.
Klient nie może zgłaszać „Map synchronized” po odebraniu danych, których nie
zaaplikował do `observedWorld`.

Minimalny kontrakt:

```text
CaptureState(world, tick T) -> SimulationState
RestoreState(state) -> nowy, kompletny GameWorld na ticku T
```

Restore ma być atomowe: parsowanie i walidacja do tymczasowego obiektu, a dopiero
po sukcesie podmiana aktywnego świata.

### MP-104 — prawdziwy initial sync i resync

Transfer snapshotu:

1. host przechwytuje stan na ticku `T`;
2. wysyła `SnapshotBegin(id, T, totalBytes, chunkCount, hash)`;
3. wysyła chunki w ograniczonym oknie;
4. klient sprawdza kompletność i hash;
5. klient atomowo wykonuje `RestoreState`;
6. host dosyła eventy o sequence większym niż odpowiadający tickowi `T`;
7. klient dogania hosta i porównuje checksum;
8. dopiero wtedy wysyła `SyncReady`.

Brak lub uszkodzenie chunku kończy się retry albo jawnym błędem, nigdy wiecznym
„Waiting for map chunks”.

### MP-105 — niezawodny dziennik eventów

Każdy zaakceptowany lub odrzucony command result otrzymuje monotoniczny
`authoritativeEventSeq`. Host trzyma ograniczony ring buffer ostatnich eventów.

Klient zapisuje:

- `lastReceivedEventSeq`;
- `lastAppliedEventSeq`;
- ostatni potwierdzony checksum/tick;
- własne niepotwierdzone `commandId`.

To jest fundament reconnectu. Sam TCP zapewnia kolejność tylko w ramach jednego
połączenia; po utworzeniu nowego socketu aplikacja musi wiedzieć, co już zostało
zastosowane.

### Kryteria ukończenia etapu 1

- host i klient startują wyłącznie z autorytatywnego stanu hosta;
- wymuszony desync zasobu, budynku i jednostki kończy się identycznym checksumem;
- urwany snapshot daje retry lub czytelne rozłączenie;
- niezgodna wersja/dane są odrzucane przed startem;
- parser odrzuca ogromne długości, overflow i urwany frame bez OOM/crash;
- 5-sekundowy stall nie gubi żadnego eventu;
- istnieją testy prawdziwego socket loopback, a nie wyłącznie
  `LocalhostGameTransport`.

## Etap 2 — latency i odczuwalna responsywność

### MP-200 — pomiar RTT, jitteru i zegara hosta

Ping/pong ma zawierać monotoniczne timestampy oraz bieżący tick hosta. Klient
oblicza:

- bieżący RTT jako telemetrykę;
- wygładzony RTT (EWMA);
- jitter, czyli zmienność RTT;
- estymowany bieżący tick hosta;
- packet/message backlog i czas ostatniej poprawnej wiadomości.

Nie synchronizujemy zegara ściennego systemu. Interesuje nas tylko różnica czasu
monotonicznego i pozycja na osi ticków symulacji.

### MP-201 — adaptacyjny input delay

Obecne stałe `inputDelayTicks = 1` nie wystarcza w Internecie. Host wyznacza
bezpieczny target tick na podstawie opóźnienia i jitteru klienta, przykładowo:

```text
delay = oneWayEstimate + jitterMargin + processingMargin
inputDelayTicks = ceil(delay / FixedDt)
```

Wartość jest ograniczona min/max i zmieniana stopniowo, żeby nie powodować
szarpania osi czasu. Host nadal ignoruje arbitralny target tick podany przez
niezaufanego klienta.

W pierwszym POC nie robimy rollback netcode. Dla RTS-a bezpieczniejszy jest
przewidywalny input delay niż cofanie ekonomii i tysięcy obiektów świata.

### MP-202 — ograniczyć ruch bez zmiany tick rate

Symulacja nadal działa w 100 Hz. Nie oznacza to konieczności wysyłania 100 ramek
sieciowych na sekundę.

- command wysyłamy natychmiast;
- autorytatywny event wysyłamy natychmiast lub w bardzo krótkim batchu;
- server tick/clock wysyłamy np. 10–20 Hz;
- checksum wystarczy co około sekundę;
- ping wystarczy co 1–2 sekundy;
- tick-only update jest latest-value i może zostać zastąpiony nowszym.

### MP-203 — UI dla pending command

Zanim powstanie pełna predykcja, UI ma natychmiast pokazać użytkownikowi, że
komenda została wysłana:

- ghost/stan „oczekuje na hosta”;
- blokada przypadkowego ponownego wysłania;
- potwierdzenie albo czytelne odrzucenie;
- wskaźnik „reconnecting/catching up”, bez pozornego przyjmowania inputu.

To redukuje odczuwalną latencję bez ryzyka rozjazdu symulacji.

### Kryteria ukończenia etapu 2

- połączenie pozostaje grywalne w testach przy RTT 20, 80 i 200 ms;
- jitter nie powoduje wykonywania komend w przeszłości;
- każda komenda jest wykonana dokładnie raz albo jawnie odrzucona;
- klient pokazuje pending/accepted/rejected;
- liczba zwykłych tick frames spada z około 100/s do ustalonego limitu;
- diagnostyka pokazuje RTT, jitter, input delay, host tick i backlog.

## Etap 3 — reconnect i recovery po przerwaniu połączenia

### Identyfikatory

Rozdzielić pojęcia:

- `RoomId` — publiczny identyfikator pokoju;
- `MatchId` — identyfikator konkretnego rozpoczętego meczu;
- `ConnectionId` — pojedyncze fizyczne połączenie;
- `PlayerSessionId` — logiczna obecność gracza w meczu mimo zmiany socketu;
- `ResumeToken` — losowy sekret pozwalający odzyskać konkretny slot;
- `commandId` — licznik komend jednego gracza w ramach `PlayerSessionId`;
- `authoritativeEventSeq` — globalna kolejność eventów danego meczu.

Tokenów nie zapisujemy w logach. Powinny mieć co najmniej 128 bitów losowości i
krótki czas ważności/grace period.

### MP-300 — state machine połączenia

Wprowadzić jawne stany:

```text
Disconnected -> Connecting -> Handshaking -> Syncing -> Active
Active -> Reconnecting -> CatchingUp -> Active
Reconnecting -> Failed
```

UI, sesja i transport czytają jeden typed connection status zamiast analizować
teksty typu `"Connected"`.

### MP-301 — reconnect klienta

Po utracie połączenia klient:

1. zatrzymuje lokalny advance świata i przyjmowanie nowych komend;
2. próbuje ponownie połączyć się z exponential backoff, np. 0.5, 1, 2, 4, 5 s;
3. wysyła `Resume(PlayerSessionId, ResumeToken, lastAppliedEventSeq, checksum)`;
4. host/relay odzyskuje ten sam slot;
5. host dosyła eventy z ring buffera;
6. jeżeli luka jest za duża albo checksum się nie zgadza, wysyła pełny snapshot;
7. klient dogania aktualny tick i wraca do `Active`.

Pending commands można wysłać ponownie. Host deduplikuje je po
`(PlayerSessionId, commandId)` i zwraca poprzedni wynik zamiast wykonywać drugi raz.

### MP-302 — reconnect hosta do relaya

Jeżeli host traci połączenie z VPS:

- relay oznacza pokój jako `Suspended`, ale zachowuje klientów i rezerwacje slotów
  przez konfigurowalny grace period, startowo 60 sekund;
- klienci przestają advance'ować i pokazują „Host reconnecting”;
- host łączy się ponownie z `RoomHostResumeToken`;
- relay ponownie mapuje kanały, a host wykonuje normalny resume/snapshot z klientami;
- po przekroczeniu grace period relay zamyka pokój.

Jeżeli proces hosta się zamknie i utraci `GameWorld`, reconnect nie pomoże. Host
migration i odzyskiwanie po crashu są poza zakresem POC.

### MP-303 — reconnect po restarcie relaya

W pierwszym POC relay jest bezstanowy na dysku. Restart VPS kończy wszystkie
pokoje. Klienci mają dostać czytelny powód, a nie wisieć bez końca.

Opcjonalny późniejszy etap może zachowywać wyłącznie metadane pokojów i tokeny w
SQLite/Redis, ale relay nadal nie będzie przechowywać `GameWorld`.

### Kryteria ukończenia etapu 3

- klient wraca po przerwie 1, 10 i 45 sekund bez duplikacji komend;
- utrata połączenia po wysłaniu komendy, ale przed otrzymaniem wyniku jest
  rozstrzygana jednoznacznie;
- zbyt stary klient dostaje pełny snapshot zamiast niepełnego event replay;
- host wracający w grace period odzyskuje ten sam pokój;
- host po grace period nie może przejąć przypadkowego pokoju;
- reconnect z błędnym tokenem nie odzyskuje slotu.

## Etap 4 — program lobby/relay na VPS

### Forma programu

Na potrzeby POC utworzyć jeden headless program, roboczo `rts_relay_server`, ale z
dwoma wewnętrznymi modułami:

- `RoomService` — pokoje, sloty, ready, publiczna lista, tokeny i timeouty;
- `RelayService` — przekazywanie ramek host ↔ klienci.

Jeden proces upraszcza wdrożenie na małym VPS. Granica modułów pozwoli później
zastąpić `RoomService` przez Steam Lobby albo przenieść go do osobnej usługi.

Rekomendowana technologia POC: C++20 i pojedynczy event loop oparty o standalone
Asio lub równoważną przenośną bibliotekę. Nie tworzyć osobnego wątku na każde
połączenie. Relay nie powinien linkować UI, scen ani audio. Wspólna biblioteka
`rts_net` zawiera framing, typy wiadomości i walidację, bez zależności od raylib.

### MP-400 — Linux i headless build

- wydzielić przenośne `rts_net`;
- usunąć windows-only warunek z docelowego transportu;
- dodać target `rts_relay_server`;
- dodać Linux build/test w CI;
- przygotować prosty plik konfiguracyjny i graceful shutdown;
- nie wykonywać automatycznego bumpowania wersji gry przy buildzie serwera.

### MP-401 — handshake z relayem

Pierwszy komunikat klienta do VPS:

```text
RelayHello(protocolVersion, gameVersion, clientNonce, role, authMode)
```

Relay odpowiada:

```text
RelayWelcome(connectionId, serverTime, limits, heartbeatInterval)
```

W zamkniętych testach wystarczy tryb guest z losową tożsamością procesu. Mimo to
każdy create/join/resume używa nieprzewidywalnego tokenu. Nie opieramy ochrony
slotu na nicku ani samym `RoomId`.

### MP-402 — publiczne i prywatne pokoje

Minimalny model pokoju:

```text
RoomId
displayName
visibility: Public | Private
state: Waiting | Starting | InGame | Suspended | Closing
hostConnection / hostSession
protocolVersion + deterministicDataHash
currentPlayers / maxPlayers
createdAt / lastActivity
joinPolicy / optionalPasswordVerifier
```

Operacje:

- `CreateRoom`;
- `ListRooms` z paginacją i filtrem wersji;
- `JoinRoom`;
- `LeaveRoom`;
- `SetReady`;
- `UpdateRoomSettings` — tylko host;
- `StartMatch` — tylko host, po zgodności wersji i slotów;
- `RoomSnapshot` — pełny bieżący stan lobby po join/reconnect.

Prywatny pokój powinien korzystać z losowego kodu/zaproszenia. Jeżeli dodamy
hasło, relay przechowuje tylko wolny hash hasła, nigdy plaintext.

### MP-403 — routing ruchu gry

Każda wiadomość gry ma przypisany `MatchId`, nadawcę i logiczny kanał. Relay:

- przyjmuje command tylko od klienta należącego do danego pokoju;
- przekazuje go wyłącznie hostowi;
- przyjmuje authoritative event/snapshot tylko od hosta;
- wysyła event do właściwych klientów;
- nie ufa `playerId` z payloadu — bierze slot z uwierzytelnionej sesji relaya;
- nie parsuje zasad budowania, ekonomii ani walki;
- rozłącza peer po naruszeniu rozmiaru, rate limitu lub kierunku wiadomości.

Snapshot ma być streamowany chunkami. Relay nie składa całego snapshotu w RAM.
Jeśli ten sam chunk idzie do kilku klientów, można przechowywać jeden immutable
bufor współdzielony do czasu wysłania, zamiast kopiować go per klient.

### MP-404 — ochrona małego VPS

Startowa konfiguracja dla zamkniętych testów:

| Parametr | Wartość startowa |
|---|---:|
| Jednoczesne połączenia | 64 |
| Jednoczesne pokoje | 16 |
| Gracze w pokoju | 8, mimo że gra początkowo używa 2 |
| Globalne queued bytes | 64 MiB |
| Per-peer zwykła kolejka | 1 MiB |
| Snapshot in-flight per peer | 256 KiB |
| Idle timeout połączenia | 15 s bez heartbeat |
| Pusty pokój Waiting | 5 min |
| Host reconnect grace | 60 s |

Wartości są guardrailami, nie celem wydajnościowym. Należy je skonfigurować bez
rekompilacji i obniżyć po pierwszych pomiarach RAM/bandwidth.

Konieczne zabezpieczenia:

- limity globalne i per IP/session;
- timeout handshake;
- brak nieograniczonych kolejek oraz `reserve` z danych peerów;
- logowanie metadanych, nie pełnych payloadów i tokenów;
- rotacja logów;
- zamknięcie najpierw nowych join/create przy globalnym przeciążeniu;
- monitoring RSS, połączeń, pokojów, queued bytes, bytes/s i liczby odrzuceń;
- uruchomienie jako nieuprzywilejowany użytkownik systemowy;
- firewall otwierający tylko port relaya i port administracyjny ograniczony do
  localhost/VPN;
- administracyjne API tylko do health/metrics w pierwszym POC.

### MP-405 — wdrożenie POC

Minimalny deployment:

- jeden binarny program;
- jedna konfiguracja;
- usługa `systemd` z restartem po awarii i limitem pamięci;
- health check;
- opcjonalnie reverse proxy/TLS termination dla HTTP metrics, ale nie jako framing
  ruchu gry;
- dashboard nie jest potrzebny — na początek wystarczą tekstowe metrics i logi.

Przed dostępem publicznym trzeba dodać szyfrowanie i prawdziwe uwierzytelnianie.
Zamknięty POC może rozpocząć się od nieszyfrowanego TCP tylko wtedy, gdy wszyscy
testerzy rozumieją ograniczenie i relay nie przyjmuje żadnych sekretów Steam.

### Kryteria ukończenia etapu 4

- host i klient za dwoma NAT-ami wykonują wyłącznie wychodzące połączenia do VPS;
- można utworzyć, znaleźć i dołączyć do publicznego pokoju;
- prywatny pokój nie pojawia się na publicznej liście;
- tylko host może wystartować mecz i publikować autorytatywne eventy;
- reconnect zachowuje slot w grace period;
- snapshot nie jest składany w całości w pamięci relaya;
- przekroczenie limitu daje kontrolowane odrzucenie zamiast wzrostu RAM;
- restart relaya kończy pokoje w znany, czytelny sposób;
- test end-to-end uruchamia trzy procesy: relay, host i klient.

## Etap 5 — przygotowanie i migracja Steam

### Co daje Steam

`ISteamNetworkingSockets` jest połączeniowym, message-oriented API. Zachowuje
granice wiadomości, obsługuje reliable/unreliable delivery, fragmentację dużych
wiadomości, ACK/retransmisję, szyfrowanie i uwierzytelnianie. Może prowadzić ruch
przez Steam Datagram Relay, ukrywając adresy IP hosta i klientów.

Steam Matchmaking & Lobbies zapewnia tworzenie, wyszukiwanie, dołączanie,
zaproszenia i metadane lobby, ale nie przesyła ruchu gameplayowego.

Otwarta wersja GameNetworkingSockets może pomóc wcześniej przetestować podobne
API, ale nie daje dostępu do sieci relay Valve. Zamknięty POC nadal potrzebuje
własnego VPS albo właściwego Steamworks SDK i uprawnień dla aplikacji.

### MP-500 — adapter Steam lobby

Zaimplementować `SteamLobbyService` mapujący wspólny model pokoju na:

- Steam Lobby ID;
- lobby owner jako host;
- lobby member list;
- metadata: wersja, data hash, stan, mapa, visibility, liczba slotów;
- zaproszenia znajomych i callbacki join/leave;
- identyfikację gracza przez SteamID.

Nie umieszczać sekretów ani dużych snapshotów w metadata lobby.

### MP-501 — adapter Steam transport

Zaimplementować `SteamNetworkingTransport`:

- host: `CreateListenSocketP2P`;
- klient: `ConnectP2P` do Steam identity hosta;
- callbacki stanu połączenia mapowane na wspólną state machine;
- reliable flags dla command/event/control/snapshot;
- ewentualny unreliable/latest-value dla telemetryki;
- wspólny protocol handshake pozostaje wymagany mimo Steam authentication.

W tym wariancie Steam wykonuje rendezvous i relay. Logicznie nadal mamy
host-client, ale gracze nie łączą się bezpośrednio po publicznym IP.

### MP-502 — decyzja o przyszłości własnego VPS

Po działającej integracji Steam wybrać jeden wariant:

1. **Steam-only:** Steam Lobby + SDR; własny lobby/relay wyłączony. Najtańszy i
   najprostszy dla gry wydawanej wyłącznie na Steam.
2. **Steam + własny coordinator:** Steam identyfikuje użytkowników i przenosi
   gameplay, a VPS przechowuje dodatkowy publiczny katalog/statystyki/bany.
3. **Cross-store fallback:** Steam używa SDR, a klienci spoza Steam używają VPS
   relay. Najwięcej utrzymania; nie realizować bez rzeczywistej potrzeby.

Rekomendacja dla obecnego celu: wariant 1. Własny relay jest dobrym POC i fallbackiem
deweloperskim, ale nie warto równolegle utrzymywać drugiej produkcyjnej sieci, jeśli
gra pozostanie Steam-only.

Oficjalne materiały:

- [ISteamNetworkingSockets](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets?l=english)
- [Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay?l=english)
- [Steam Matchmaking & Lobbies](https://partner.steamgames.com/doc/features/multiplayer/matchmaking?l=english)

## Notatka edukacyjna — jak to działa

### TCP nie wysyła „wiadomości”

TCP dostarcza uporządkowany strumień bajtów. Jedno `send(1000)` może zostać
odebrane jako dwa `recv(400)` i `recv(600)`, albo razem z następnym `send`.
Dlatego protokół potrzebuje nagłówka z długością payloadu. Znak nowej linii działa
w prostym prototypie, ale jest słaby dla danych binarnych, limitów i snapshotów.

TCP daje niezawodność w ramach żywego połączenia. Nie wie jednak, że nowe
połączenie po reconnect jest kontynuacją starego meczu. `PlayerSessionId`, sequence
i resume token są odpowiedzialnością gry.

### Relay to dwie osobne relacje

Przy własnym VPS host i klient nie mają jednego socketu „przez serwer”. Istnieją
dwa połączenia:

```text
host socket <-> relay socket A
client socket <-> relay socket B
```

Relay odczytuje pełną ramkę z A, sprawdza pokój/kierunek/limit i kolejkuje ją na B.
Dlatego opóźnienie zawiera oba odcinki, a relay potrzebuje backpressure. Gdy klient
odbiera wolno, nie wolno bez końca gromadzić jego danych w RAM.

### Lobby i relay to różne funkcje

Lobby odpowiada na pytanie „kto z kim chce grać?”:

- lista pokojów;
- członkowie i sloty;
- ustawienia;
- ready/start;
- zaproszenia.

Relay odpowiada na pytanie „jak dostarczyć dane między wybranymi uczestnikami?”.
W POC oba moduły mogą działać w jednym programie, ale protokół i kod powinny je
rozróżniać.

### RTT, jitter i input delay

- **RTT** — czas klient → host → klient.
- **One-way estimate** — przybliżenie połowy RTT; bez synchronizacji zegarów nie
  znamy go dokładnie.
- **Jitter** — zmienność opóźnienia. RTT 60 ms z jitterem 5 ms jest łatwiejsze niż
  RTT skaczące między 30 a 150 ms.
- **Input delay** — liczba ticków zapasu, żeby command dotarł do hosta przed
  autorytatywnym wykonaniem.

UDP nie usuwa fizycznego RTT. Pozwala uniknąć blokowania nowszych datagramów przez
retransmisję starego, ale dla komend RTS i tak trzeba zbudować reliable delivery.
Steam Networking Sockets daje oba tryby bez pisania własnego systemu ACK.

### Tick symulacji a częstotliwość sieci

100 ticków symulacji na sekundę nie wymaga 100 wiadomości sieciowych. Klient może
symulować ticki lokalnie, jeśli dostaje identyczne autorytatywne komendy oraz
okresowo zna tick hosta i checksum. Rzadkie eventy wysyłamy od razu; powtarzalną
telemetrię można wysłać rzadziej i zastąpić nowszą wartością.

### Checksum, snapshot i reconnect

- checksum tylko wykrywa, że światy są różne;
- snapshot przenosi kompletny autorytatywny stan;
- event journal przenosi zmiany, które wystąpiły po ticku snapshotu;
- reconnect tworzy nowe fizyczne połączenie, odzyskuje logiczną sesję i wybiera:
  krótki event replay albo pełny snapshot.

Przykład:

```text
Klient ostatnio zastosował event 1200.
Po reconnect host ma event 1240.

Jeżeli ring buffer zawiera 1201..1240:
  host dosyła te 40 eventów.

Jeżeli najstarszy event hosta to 1230:
  luka jest nieodtwarzalna, więc host wysyła snapshot i późniejsze eventy.
```

### Co dzieje się po utracie hosta

Ponieważ host posiada autorytatywny `GameWorld`, relay nie potrafi sam kontynuować
meczu. Może jedynie zaczekać na powrót tego samego hosta. Prawdziwa host migration
wymagałaby regularnego przekazywania pełnego, wiarygodnego stanu innemu klientowi,
wyboru nowego autorytetu i ochrony przed dwoma hostami jednocześnie. To osobny,
duży projekt i nie jest potrzebny do POC.

## Proponowana mapa plików

Nazwy są robocze, ale granice modułów powinny pozostać czytelne:

```text
inc/multiplayer/
  NetworkMessage.h              typy wiadomości i kanałów
  ProtocolCodec.h               framing, endian, walidacja limitów
  ConnectionState.h             typed state machine i diagnostyka
  IGameConnection.h             niski poziom send/poll/state
  ILobbyService.h               create/list/join/leave/ready/start
  DirectTcpConnection.h         LAN i socket loopback
  VpsRelayConnection.h          klient własnego relaya
  SteamNetworkingConnection.h   późniejszy adapter Steam

src/multiplayer/
  ProtocolCodec.cpp
  DirectTcpConnection.cpp
  VpsRelayConnection.cpp
  SteamNetworkingConnection.cpp

inc/core/ + src/core/
  SimulationState.*             Capture/Restore, wspólny schema/visitor
  GameSession.*                 event journal, input delay, sync/reconnect

relay/
  CMakeLists.txt
  main.cpp
  RelayServer.*                 accept/event loop i limity globalne
  RoomService.*                 lifecycle pokojów i tokeny
  RelayService.*                routing host-klienci
  RelayConfig.*                 konfiguracja i bezpieczne limity
  RelayMetrics.*                health i liczniki

tests/multiplayer/
  ProtocolCodecTests.cpp
  FaultInjectionTransportTests.cpp
  InitialSyncRecoveryTests.cpp
  ReconnectTests.cpp
  RelayIntegrationTests.cpp
```

Obecny `IGameTransport` może być migrowany stopniowo przez adapter. Nie trzeba
jednym commitem przepisać scen i całego UI. Najpierw nowy codec/connection może
zostać opakowany tak, żeby nadal udostępniał istniejące metody `SendClientCommand`
i `ReceiveClientFrames`; po ustabilizowaniu protokołu `GameSession` przechodzi na
typowane wiadomości, a stary interfejs zostaje usunięty.

## Kolejność wykonania

| Milestone | Zakres | Zależności | Szacunek solo |
|---|---|---|---:|
| M0 | Fault injection i kontrakt warstw | brak | 1–3 dni |
| M1 | Framing, handshake, limity | M0 | 3–6 dni |
| M2 | `SimulationState`, initial sync, resync | M1, `AUD-03` | 1–3 tygodnie |
| M3 | Event journal, latency i UI pending | M1–M2 | 4–8 dni |
| M4 | Reconnect klienta i hosta | M2–M3 | 1–2 tygodnie |
| M5 | Linux lobby/relay VPS + rooms | M1, M4 | 1–3 tygodnie |
| M6 | Steam Lobby + Networking Sockets | stabilne M2–M4 | później, 1–3 tygodnie |

Szacunki są orientacyjne i zakładają jedną osobę oraz brak rozbudowy gameplayu w
tym samym czasie. Największą niewiadomą jest kompletność `SimulationState`, nie sam
relay.

Nie należy rozpoczynać M5 od pisania serwera pokojów przed M1/M2. Inaczej VPS tylko
przeniesie istniejące błędy snapshotu, limitów i reconnectu z LAN do Internetu.

## Definition of Done całego POC

- host i klient łączą się wychodząco z VPS i grają bez publicznego IP/port forwarding;
- publiczny i prywatny pokój mają poprawny lifecycle;
- RTT 20–200 ms nie powoduje desyncu ani duplikacji komend;
- reconnect klienta i hosta działa w grace period;
- forced desync kończy się prawdziwym recovery i zgodnym checksumem;
- relay ma twarde limity RAM/kolejek/wiadomości i metryki;
- Linux relay buduje się i przechodzi test end-to-end w CI;
- żadna warstwa gameplayu nie zależy bezpośrednio od TCP ani API relaya;
- podmiana `VpsLobbyService`/`VpsRelayTransport` na adaptery Steam nie zmienia
  protokołu `GameCommand`, `SimulationState` ani reguł reconnectu.
