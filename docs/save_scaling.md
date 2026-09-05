# Skalowanie zapisu kampanii

Pomiary testu `PeacefulWorldTests.ReportsStage0MapAndStateSizeBaseline` dla jednej
zmaterializowanej prowincji:

| Preset mapy lokalnej | Rozmiar tekstowego stanu |
|---|---:|
| S (201 x 201) | 933 710 B (0,89 MiB) |
| XL (501 x 501) | 5 948 499 B (5,67 MiB) |

Sama obecność prowincji w globalnym grafie jest tania. Duży koszt pojawia się
dopiero po kolonizacji, gdy prowincja dostaje lokalny `TileMap` i symulację.
Przy dzisiejszym formacie 500 zmaterializowanych map S zajęłoby około 445 MiB,
a 500 map XL około 2,77 GiB, jeszcze bez zapasu na dalszą rozgrywkę. Limit
512 MiB mieści więc w przybliżeniu 574 puste mapy S albo 90 pustych map XL.

`SaveToFile` przenosi teraz bufor z `ostringstream` bez tworzenia drugiej pełnej
kopii i odrzuca stan przekraczający wspólny limit 512 MiB. To wystarcza dla
kilkuset globalnych prowincji, dopóki większość nie ma lokalnej symulacji.

Przed dopuszczeniem setek jednocześnie zmaterializowanych prowincji potrzebny
jest nowy kontener zapisu: binarny, strumieniowo kompresowany (np. Zstandard),
z rozmiarem nieskompresowanym i hashem w nagłówku. Snapshot MP powinien używać
tego samego kodeka i istniejącego podziału na chunki. Dekoder musi zachować
obecne limity liczników oraz limit rozmiaru po dekompresji; sam limit danych
skompresowanych nie chroni przed bombą dekompresyjną.

Kompresji nie należy dokładać do obecnego tekstowego parsera jako osobnej,
równoległej ścieżki. Wspólny `SimulationStateCodec` dla save i resyncu usunie
duplikację oraz pozwoli testować jeden format round-trip i jeden zestaw limitów.
