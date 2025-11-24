# Informacje o przechowywaniu danych

## Gdzie są zapisywane dane kont?

Aplikacja RoTools przechowuje dane kont w folderze użytkownika Windows:

### Wersja produkcyjna (zainstalowana):
```
%LOCALAPPDATA%\RoTools\
  └── AccountData.dat
```

### Wersja deweloperska (build\Release):
```
%LOCALAPPDATA%\RoTools_Dev\
  └── AccountData.dat
```

## Dlaczego są oddzielne foldery?

**Wersja deweloperska** i **wersja produkcyjna** używają **oddzielnych folderów**, aby:

1. ✅ Uniknąć mieszania danych testowych z produkcyjnymi
2. ✅ Zapobiec przypadkowemu skopiowaniu danych deweloperskich do instalatora
3. ✅ Umożliwić testowanie bez wpływu na produkcyjne dane

## Bezpieczeństwo danych

### Dane są przechowywane lokalnie per użytkownik:
- ✅ Każdy użytkownik Windows ma swój własny folder `%LOCALAPPDATA%`
- ✅ Dane są szyfrowane przy użyciu Windows DPAPI (Data Protection API)
- ✅ Tylko użytkownik, który zalogował się do konta, może je odszyfrować

### Co to znaczy w praktyce?

1. **Dane NIE są kopiowane z instalatorem**
   - Instalator kopiuje tylko program i assety
   - NIE kopiuje żadnych plików z danymi kont

2. **Każdy użytkownik ma oddzielne dane**
   - Użytkownik A na komputerze 1: `C:\Users\UserA\AppData\Local\RoTools\`
   - Użytkownik B na komputerze 1: `C:\Users\UserB\AppData\Local\RoTools\`
   - Użytkownik A na komputerze 2: `C:\Users\UserA\AppData\Local\RoTools\`
   
   Każdy z tych folderów jest **całkowicie niezależny**.

3. **Developer vs Produkcja**
   - Developer: `%LOCALAPPDATA%\RoTools_Dev\`
   - Produkcja: `%LOCALAPPDATA%\RoTools\`
   
   Te foldery są **całkowicie oddzielone**.

## Jak sprawdzić, gdzie są moje dane?

1. Naciśnij `Win + R`
2. Wpisz: `%LOCALAPPDATA%\RoTools`
3. Kliknij OK

Zobaczysz folder z plikiem `AccountData.dat` (jeśli dodałeś jakieś konta).

## Backup danych

Jeśli chcesz zrobić backup swoich kont:

1. Skopiuj cały folder `%LOCALAPPDATA%\RoTools\` 
2. Zachowaj go w bezpiecznym miejscu
3. Aby przywrócić, skopiuj z powrotem do tej samej lokalizacji

**⚠️ WAŻNE:** Plik `AccountData.dat` jest zaszyfrowany kluczem Twojego konta Windows. 
Jeśli przywrócisz go na innym komputerze lub koncie użytkownika, **NIE będzie działać**.

## Dane WebView2

Również dane przeglądarki (cache, cookies) są przechowywane osobno:

- Produkcja: `%LOCALAPPDATA%\RoTools\WebView2\`
- Developer: `%LOCALAPPDATA%\RoTools_Dev\WebView2\`

## Ustawienia aktualizacji

Plik `UpdateSettings.txt` znajduje się w folderze aplikacji (obok `MultiRoblox.exe`). Zawiera wyłącznie znacznik ostatniego sprawdzenia oraz wybraną częstotliwość (Everyday/Weekly/Monthly/Never); nie przechowuje żadnych danych kont ani tokenów.
