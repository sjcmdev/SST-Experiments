# GUI-Bridge MCP — eksperyment B1

Windows, lokalny OCR, stdio. Uruchom w tym katalogu: `uv sync`, następnie
`.venv\Scripts\python.exe server.py`. Nie wymaga połączenia z usługą OCR.
Importuje `FastMCP` z MCP 1.x; zainstalowane MCP 2.3.0 usunęło tę nazwę,
więc serwer używa wówczas przemianowanej klasy `MCPServer`.

## Rejestracja

Codex, z dowolnego katalogu:

```powershell
codex mcp add gui -- "D:\SJCM\01_SemiconductorStudio\SST-Experiments\gui-bridge\.venv\Scripts\python.exe" "D:\SJCM\01_SemiconductorStudio\SST-Experiments\gui-bridge\server.py"
```

Jeśli PowerShell blokuje `codex.ps1`, użyj `codex.cmd` zamiast `codex`.
Alternatywnie w `~/.codex/config.toml`:

```toml
[mcp_servers.gui]
command = 'D:\SJCM\01_SemiconductorStudio\SST-Experiments\gui-bridge\.venv\Scripts\python.exe'
args = ['D:\SJCM\01_SemiconductorStudio\SST-Experiments\gui-bridge\server.py']
```

[Oficjalna dokumentacja MCP w Codex](https://developers.openai.com/codex/mcp).
Nie dodawaj jednocześnie wpisu przez CLI i drugiego wpisu ręcznie.

Claude Code, projektowy plik `.mcp.json`:

```json
{
  "mcpServers": {
    "gui": {
      "type": "stdio",
      "command": "D:/SJCM/01_SemiconductorStudio/SST-Experiments/gui-bridge/.venv/Scripts/python.exe",
      "args": ["D:/SJCM/01_SemiconductorStudio/SST-Experiments/gui-bridge/server.py"]
    }
  }
}
```

## Narzędzia i współrzędne

- `windows(app)`, `launch(app)`, `close(app)`: `scaps`, `propho`; `origin` pozostaje TODO.
  ProPho jest wyszukiwane po tytule `Propho`, także w procesie potomnym launchera.
  `close` wysyła WM_CLOSE, nie zabija procesu; raportuje pozostawione dialogi.
- `read(title, region=None, diff=True)`: PrintWindow, wycięcie klienta, OCR 2×,
  wynik `x,y tekst` w pikselach klienta 1×. `region=[left,top,right,bottom]`.
  Współrzędne pozostają względem całego klienta również dla regionu.
  Preferowany język en-US, potem profil użytkownika; mała korekta cyfr w liczbach.
  Diff ma osobną historię dla dokładnego tytułu, HWND i regionu; pierwszy odczyt jest pełny,
  brak zmian daje pusty tekst, usunięte linie mają prefiks `- `.
- `click(title, text=None, x=None, y=None, nth=0, offset=[0,0], real_mouse=False)`:
  fraza OCR lub x,y, rozmyte dopasowanie, nth od zera w kolejności wiersz/kolumna.
  Komunikaty przez win32 `click`, także do potomnego HWND pod punktem.
  Wysłanie komunikatów nie dowodzi zmiany stanu: sprawdź `read`, potem jawnie ponów
  z `real_mouse=True` (`click_input`). Etykieta CVI może wymagać offsetu do pola przełącznika.
- `type_text(text)` i `key(keys)` piszą do aktualnie aktywnej kontrolki prawdziwą klawiaturą.
  `type_text` traktuje tekst dosłownie; `key` używa składni pywinauto, np. `^a`, `{ENTER}`.
  Klik komunikatowy nie gwarantuje przeniesienia fokusu do celu.
- `wait(title, seconds=5)` czeka maksymalnie podaną liczbę sekund na widoczne okno.
- `screenshot(title, reason)`: PNG klienta 1× w `shots/`, powód w `shots/log.md`.
  Limit na proces: `GUI_BRIDGE_SHOT_BUDGET`, domyślnie 5, 0 wyłącza zrzuty.
  Lokalny capture do OCR nie zużywa limitu i nie zapisuje PNG.

Tytuły: najpierw dokładne dopasowanie, potem regex; wiele trafień jest błędem.
Okna muszą być widoczne i niezminimalizowane; mogą być zasłonięte.
Dla okien przekraczających limit Windows OCR podaj mniejszy region.

## Sprawdzenie 2026-11-18

`uv run --with pytest python -m pytest -q` — **7 testów**, tylko korekta OCR, diff i fuzzy match.
W tej sesji uruchomiono `.venv\Scripts\python.exe -m pytest -q`; pytest i jego zależności
skopiowano z lokalnego cache do `.venv`, bo pobieranie pakietów było blokowane.
Test klientem MCP po stdio: initialize, wszystkie 9 narzędzi, launch/read/click/close SCAPS,
odrzucenie pustego powodu, limitu zrzutów 0 i nieskonfigurowanego Origin.

**SCAPS:** launch, lista paneli `CVIRTLVDChild*`, OCR Action Panel i close działają.
Klik `Temperature` wysłany. Klik komunikatowy pola Series resistance `yes`
(`text="yes", nth=0, offset=[-28,0]`, punkt 518,50) nie zmienił OCR ani pikseli
regionu `[420,0,690,180]`; potem wysłano `no`. Próba `real_mouse=True` zakończyła się
błędem `There is no active desktop required for moving mouse cursor!`.
W tej sesji nie potwierdzono działania klików CVI; wynik trzeba powtórzyć na aktywnym pulpicie.
Zamykanie ramy CVI kończy program; zamknięcie samego Action Panel tylko go ukrywa.

**ProPho:** launcher tworzy proces potomny, ale start kończy się dialogiem
`Unhandled exception in script`: Tcl nie znajduje użytecznego `init.tcl`.
OCR tego dialogu i klik komunikatowy jego przycisku `Close` działają; procesy zakończone.
Nie udało się sprawdzić odczytu głównego okna ani kliku zakładek Tkinter.

Origin, gałąź MCP 1.x oraz wpisywanie do aplikacji nie były sprawdzane na żywo.
OCR może przekłamywać liczby; zmiany ustawień zawsze należy odczytać i zweryfikować.
