// The MIT License (MIT)

// Copyright (c) 2015 Grzegorz Czernatowicz

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//Import bibliotek wymaganych do działania modułów i funkcji
#include <SPI.h>
#include <SdFat.h>
#include <MFRC522.h> //biblioteka dla czytnika
#include <Wire.h>
#include <LiquidCrystal_I2C.h> //interfejs I2C dla LCD
#include <Servo.h>


#define SD_CS 8
#define RFID_RST  6
#define RFID_SS  7
#define REED 5
#define MAX_HIST 50 //aby uniknąć fragmentacji sterty, definiuje maksymalna dlugosc historii,dynamiczna alokacja pamieci nie jest wskazana ze wgledu na mala jej ilosc

char cardID_HEX[8]; //globalna zmienna dla numeru odczytanej karty
char MasterCard[8]; //Główna karta

//Flagi wykrzystywane w różnych miejscach w programie
bool MasterMode = false; //dodawanie nowych kart
bool ExitMM = false; //etap pośredni - wychodzenie z MasterMode
bool MasterWrite = false;//przydzielanie nowej karty głównej
bool MasterPresent = false; //czy karta główna jest ustawiona?
bool Authorized = false; //czy karta jest na białej liście?
bool Opened = false; //status drzwi
bool HistoryMode = false;

//przyciski
const byte button[] = {2, 3}; //lista przycisków 0 = główny
volatile bool button_reading[] = {LOW, LOW};
volatile unsigned long current_high[2];
volatile unsigned long current_low[2];

//historia
byte log_nr = 0;
int idHist[MAX_HIST]; //pozycje dla nowych linii
int nLine = 0;
long openTime;
int autoClose = 5000;

//włączenie obsługi urządzeń peryferyjnych
SdFat MicroSD;
SdFile SD_File;
MFRC522 Rfid(RFID_SS, RFID_RST); // Utworzenie instancji czytnika na wybranych pinach
LiquidCrystal_I2C lcd (0x27, 16, 2); //Utworzenie instancji ekranu o adresie 0x27 i ilosci znakow 16x2 (16 znaków w dwóch liniach)
Servo servo;
//uint8_t * heapptr, * stackptr; //debug

void setup() { //inicjalizacja programu
  Serial.begin(9600);
  Serial.println(F("Init..."));
  lcd.init (); //inicjalizacja ekranu
  lcd.backlight (); //wlaczenie podwietlenia LCD
  lcd.setCursor(0, 0); //ustawienie kursora na początek ekranu
  lcd.print(F("Starting...")); //F() zapisuje tekst w pamięci flash zamiast SRAM

  pinMode(button[0], INPUT);
  pinMode(button[1], INPUT);
  pinMode(REED, INPUT);
  digitalWrite(REED, HIGH); //pull up input
  attachInterrupt(digitalPinToInterrupt(button[0]), readButton, RISING); //przerwanie dla glownego przycisku
  attachInterrupt(digitalPinToInterrupt(button[1]), readButton, RISING); //przerwanie dla podrzednego przycisku

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH); //stan wysoki, wyłącza czytnik RFID
  pinMode(RFID_SS, OUTPUT);
  digitalWrite(RFID_SS, LOW); //stan niski, włącza czytnik SD

  SPI.begin();

  Rfid.PCD_Init(); //inicjacja RFID
  if (!RfidCheck()) { // Sprawdzam polączenie z czytnikiem
    lcd.setCursor(0, 1);
    lcd.print("RFID ERROR");
    Serial.println(F("RFID Error, check connection!"));
    infiniteLoop(); //jezeli błąd, przerwij działanie
  }

  digitalWrite(RFID_SS, HIGH);
  digitalWrite(SD_CS, LOW);

  if (!MicroSD.begin(SD_CS, SPI_HALF_SPEED))  //połowa prędkości SPI, lepsza kompatybilność. Sprawdzam połączenie z adapterem
  {
    lcd.setCursor(0, 1);
    lcd.print(F("SD ERROR"));
    Serial.println(F("SD Error, check if card is inserted."));
    infiniteLoop();
  }
  rotateLog();

  digitalWrite(SD_CS, HIGH);
  digitalWrite(RFID_SS, LOW);
  //ustawiam serwo na pozycję startową
  servo.attach(9);
  servo.write(60);
  delay(600); //czas, by serwo przekręciło się na pozycję
  servo.detach();

  lcdClearLine(0, 15, 0);
  lcd.setCursor(0, 0);
  lcd.print(F("NORMAL"));
  lcdClearLine(13, 16, 0); lcd.setCursor(10, 0); lcd.print("CLOSED");
  //genIndex();
  Serial.println(F(" "));
  Serial.println(F("READY!"));
}

void readButton() {
  for (byte i = 0; i < 2; i++)
  { //sprawdzam wszystkie przyciski
    if (digitalRead(button[i]) == HIGH) {
      current_high[i] = millis();
      button_reading[i] = HIGH;
    }
    if (digitalRead(button[i] == LOW) && button_reading[i] == HIGH) {
      current_low[i] = millis();
      if ((current_low[i] - current_high[i] > 100 && (current_low[i] - current_high[i]) < 800)) {
        button_reading[i] = LOW;
      }
    }
  }
}
void loop() //glowna petla programu
{
  for (int i = 0; i < 2l; i++)
  {
    if (button_reading[1] == HIGH)
    {
      if (HistoryMode && !MasterMode)
      {
        log_nr--;
        showLogEntry(log_nr);
        if (log_nr == 0)
          log_nr = nLine + 1;
      }
      if (!HistoryMode)
        zamienDrzwi();
      button_reading[1] = LOW;
    }
    if (button_reading[0] == HIGH)
    {
      if (!HistoryMode)
      {
        Serial.println(F("HISTORY"));
        HistoryMode = true;
        log_nr = nLine;
        showLogEntry(log_nr);
        lcdClearLine(0, 6, 0);
        lcd.setCursor(0, 0);
        lcd.print(F("HISTORY"));
      }
      else
      {
        exitHistory();
      }
      button_reading[0] = LOW;
    }

  }
  Authorized = false; //reset do stanu początkowego
  digitalWrite(RFID_SS, HIGH);
  digitalWrite(SD_CS, LOW);

  if (!MasterPresent && !MasterWrite)
    MasterRead(); //na początku sprawdzam, czy istnieje główna karta w pamięci

  digitalWrite(SD_CS, HIGH);
  digitalWrite(RFID_SS, LOW);

  if (!Opened)readTagID(); //czekam na odczyt karty

  if (strcmp(MasterCard, cardID_HEX) == 0 && MasterPresent && !MasterMode) { //jeżeli nowa karta jest kartą główną - przejdź w tryb admina
    MasterMode = true;
    Serial.print(F("Master detected: ")); //db
    Serial.println(MasterCard); //db
    Serial.println(F("MASTER MODE"));

    lcdClearLine(0, 6, 0);
    lcdClearLine(0, 15, 1);
    lcd.setCursor(0, 0);
    lcd.print(F("MASTER"));
  }
  else if (strcmp(MasterCard, cardID_HEX) == 0 && MasterPresent && MasterMode)
  { //powrót do normalnego trybu
    MasterMode = false;
    ExitMM = true;
    lcdClearLine(0, 6, 0);
    lcdClearLine(0, 15, 1);
    lcd.setCursor(0, 0);
    lcd.print(F("NORMAL"));
    Serial.println(F("Back to NORMAL MODE"));
  }

  if (strlen(cardID_HEX) == 7 && !Opened) //jezeli ID jest poprawne, uruchom zapis na karte SD
  {
    Serial.print(F("Valid ID.")); //db
    if (!MasterMode && !ExitMM)
    {
      //sprawdz, czy karta jest na białej liscie
      if (look4id("WhiteList.txt", cardID_HEX))
        Authorized = true;
      lcdClearLine(7, 15, 1);
      lcd.setCursor(7, 1);
      if (Authorized)
      {
        lcd.print(F(" Granted"));
        zamienDrzwi();
      }
      else
        lcd.print(F(" Denied!"));
    }
    digitalWrite(RFID_SS, HIGH);
    digitalWrite(SD_CS, LOW);
    Write2SD(cardID_HEX); //zapis na kartę SD
    digitalWrite(SD_CS, HIGH);
    digitalWrite(RFID_SS, LOW);
    clearCardID(); //usunięcie odczytanej karty
  }
  else
    clearCardID(); //usuwam z pamieci odczytana karte
  //czas na otwarcie drzwi

  if (millis() - openTime > autoClose && Opened) {
    if (!digitalRead(REED))
    {
      Serial.println(F("Auto:"));
      delay(2000);
      zamienDrzwi();
    }
  }
  ExitMM = false;
}//END OF LOOP

//odczytuje wszystkie wpisy w glownym dzienniku urzadzenia
//dla kazdego wpisu zapisuję lokalizację przecinka, by w łatwy sposób odczytać dane, które rozdziela
void genIndex() {
  if (SD_File.open("log.txt", O_READ))
  {
    int nByte = 0;
    char cByte;
    nLine = -1;
    while (SD_File.available())
    { 
      cByte = SD_File.read();
      nByte++;
      if (cByte == ',')
      {
        nLine++;        
        idHist[nLine] = nByte - 1; //lokolizacja przecinka
      }

    }
  }
  SD_File.sync();
  SD_File.close();

}

//wyświetla na ekranie wybrany wpis w dzienniku
//wykorzystuje do tego zapisaną wcześniej w indeksie pozycję przecinka
//ustawia kursor w SD_Fileu na tę pozycję i odczytuje dane
//dzięki temu nie trzeba za każdym razem, przy przeskakiwaniu wpisów, odczytywać całego dziennika
void showLogEntry(byte log_nr)
{ //wyswietl wpis z logu
  //genIndex();
  if (SD_File.open("log.txt", O_READ))
  {
    char c;
    char time[10];
    int time_num = 0;
    int i = 0;
    SD_File.seekSet(idHist[log_nr] - 7);
    Serial.print("ID: ");
    lcdClearLine(0, 15, 1);
    lcd.setCursor(0, 1);
    for (int i = 0; i < 7; i++)
    {
      c = SD_File.read();
      Serial.print(c);
      lcd.print(c);
    }
    lcd.print(F(" "));
    Serial.println();
    Serial.print("TIME: ");
    SD_File.seekCur(1);
    c = SD_File.read();
    while (c > 47 && c < 58)
    {
      time[i] = c;
      i++;
      Serial.print(c);
      //lcd.print(c);
      c = SD_File.read();
    }
    time_num = atoi(time); //konwersja tablcy znakow char do int
    int now = millis() / 1000; //aktualny czas w sekundach
    if (now - time_num < 60)
    {
      lcd.print(now - time_num);
      lcd.print("s ago");
    }
    else if (now - time_num > 60  && now - time_num < 3600 * 6)
    {
      lcd.print((now - time_num) / 60);
      lcd.print("m ago");
    }
    else if (now - time_num > 3600 * 6 && now - time_num < 3600 * 96)
    {
      lcd.print((now - time_num) / 3600);
      lcd.print("h ago");
    }
    else if (now - time_num > 3600 * 96)
    {
      lcd.print((now - time_num) / 3600 * 24);
      lcd.print("d ago");
    }
  }
  SD_File.close();
}
//funkcja sprawdza, czy dana karta istnieje już w wybranym SD_Fileu
//wykorzystać można ją do sprawdzenia, czy odczytana karta znajduje się na białej liście, lub czy nie jest kartą admina.
bool look4id(char filename[9], char cardID[8])
{
  char idBuffer[8];
  byte chPos = 0;
  bool exist = false;
  idBuffer[7] = '\0';
  if (SD_File.open(filename, O_READ))
  {
    Serial.print(F("Opening "));
    Serial.println(filename);
    while (SD_File.available())
    {
      char ch = SD_File.read();
      if (ch == '\n')
      {
        chPos = 0;
        if (strcmp(idBuffer, cardID) == 0)
        {
          SD_File.close();
          Serial.print(F("Already in file."));
          exist = true;
        }
      }
      else if (chPos < 7)
      {
        idBuffer[chPos] = ch;
        chPos++;
      }
    }
    Serial.print(F("Done."));
    SD_File.close();
  }
  else
    Serial.print(F("ID doesn't exists! "));
  return exist;
}
//sprawdzam czy czytnik RFID został poprawnie zainicjowany
bool RfidCheck()
{
  // Sprawdzam wersje oprogramowania czytnika w rejestrze
  byte v = Rfid.PCD_ReadRegister(Rfid.VersionReg);
  // jeżeli otrzymamy 0x00 lub 0xFF to połączenie z czytnikiem się nie udało
  if ((v == 0x00) || (v == 0xFF))
    return false;
  else
    return true;
}
//odczytuję ID karty admina z SD_Fileu. Jeżeli nie ma - dodaje nową
void MasterRead()
{
  Serial.print(F("MasterCheck:"));
  if (SD_File.open("master.txt", O_READ))
  { //najpierw szukam masterID na karcie SD
    Serial.println(F("master.txt exists, opening...")); //db
    for (int i = 0; i < 7; i++)
    {
      MasterCard[i] = SD_File.read();
    }
    SD_File.close();
    Serial.println(MasterCard); //db
    MasterPresent = true;
  }
  else { //jeżeli nie ma, dodaj kartę jako MASTER
    Serial.println(F("master.txt doesn't exist, waiting for new card...")); //db
    lcdClearLine(0, 15, 0);
    lcdClearLine(0, 15, 1);
    lcd.setCursor(0, 0);
    lcd.print(F("SET ANY CARD"));
    lcd.setCursor(0, 1);
    lcd.print(F("AS MASTER NOW"));
    MasterWrite = true;
  }
  return;
}
// odczytuje ID karty. zapisuje jako zbiór znaków heksadecymalnych
bool readTagID()
{
  if ( ! Rfid.PICC_IsNewCardPresent()) //Gdy nie ma karty, przerwij działanie
    return false;
  if ( ! Rfid.PICC_ReadCardSerial())   //Odczytaj numer i kontynuuj, przerwij w przypadku blędu
    return true;

  if (HistoryMode)
    exitHistory();
  //odczytuje z karty 4 bajtowe UID
  Serial.print(F("CARD DETECTED: ")); //db
  byte cardID[4];
  String tempID;
  for (int i = 0; i < 4; i++)
  {
    cardID[i] = Rfid.uid.uidByte[i]; //odczytanie kolejnych bitów numeru
    //połączenie tablicy bitów w stringa i konwersja na system heksadecymalny
    tempID +=  String(cardID[i], HEX); // String to HEX 2KB Flash
  }
  tempID.toCharArray(cardID_HEX, 8); // konwersja string na char
  lcdClearLine(0, 15, 1);
  lcd.setCursor(0, 1);
  lcd.print(cardID_HEX);
  Serial.println(cardID_HEX);
  Rfid.PICC_HaltA(); // koniec odczytywania
  return false;
}
//zapisuje wybrany ID do wybranego SD_Fileu
//może dopisać timestamp do logu.
void SaveLog(char FcardID[8], char log_file[9], bool global)
{
  char filename[14];
  sprintf(filename, "%s.txt", log_file);
  if (SD_File.open(filename, O_RDWR | O_CREAT | O_AT_END))
  {
    if (global)
    {
      SD_File.print(FcardID);
      SD_File.print(F(","));
      SD_File.println(millis() / 1000); //zapisz czas od uruchomienia
    }
    else
    {
      SD_File.println(millis() / 1000); //zapisz czas od uruchomienia
    }
    SD_File.close();
    SD_File.sync();
    if(log_file == "log")
    genIndex();
    lcd.setCursor(15, 1);
    lcd.print(F("+"));
    Serial.print(F("Writing to log: ")); //db
    Serial.println(filename); //db
  }
  else
  {
    Serial.print(F("Cannot open: ")); //db
    Serial.println(filename); //db
    lcd.setCursor(15, 1);
    lcd.print(F("-"));
  }
}
//zapisuje ID kart na SD_Fileu na SD, w zależnosci od trybu
void Write2SD(char FcardID[8])
{
  if (MasterWrite)
  { //zapis mastera na karte
    if (SD_File.open("master.txt", O_RDWR | O_CREAT | O_AT_END))
    {
      SD_File.println(FcardID);
      SD_File.close();
      Serial.println(F("MasterCard saved."));
      MasterWrite = false;
      lcdClearLine(0, 15, 0);
      lcdClearLine(0, 15, 1);
      lcd.setCursor(0, 0);
      lcd.print(F("MASTER SET"));
      for (int i = 0; i < 8; i++)
      {
        MasterCard[i] = FcardID[i];
      }
      MasterPresent = true;
      //delay(1500);
      lcdClearLine(0, 16, 0);
      lcd.setCursor(0, 0);
      lcd.print(F("NORMAL"));
      lcd.setCursor(10, 0);
      if (!Opened)
        lcd.print(F("CLOSED"));

    }
    else
    {
      lcd.setCursor(15, 1);
      lcd.print(F("-"));
    }
  }
  else if (MasterMode && (strcmp(MasterCard, FcardID) != 0))
  { //zapis kart do bialej listy
    if (!look4id("WhiteList.txt", FcardID))
    {
      if (SD_File.open("WhiteList.txt", O_RDWR | O_CREAT | O_AT_END))
      {
        SD_File.println(FcardID);
        SD_File.close();
        Serial.println(F("Whitelist entry added."));
        lcdClearLine(0, 15, 1);
        lcd.setCursor(0, 1);
        lcd.print(FcardID);
        lcd.print(F(" ADDED"));
      }
    }
    else
      lcd.print(F(" EXISTS"));
  }
  else if (!MasterMode && !ExitMM)
  { //zapis kazdej karty do logow
    if (Authorized)
    {
      SaveLog(FcardID, FcardID, 0);
      SaveLog(FcardID, "log", 1);
    }
    else
    {
      SaveLog(FcardID, "Denied", 1);
    }
    if (nLine == MAX_HIST - 1)
      rotateLog();
  }
}
//wypelanie wybrany fragment ekranu LCD spacjami
void lcdClearLine(byte kolumnaOd, byte kolumnaDo, byte wiersz)
{
  for (byte i = kolumnaOd; i <= kolumnaDo; i++) {
    lcd.setCursor(i, wiersz); lcd.print(" ");
  }
}
//jezeli wystąpił krytyczny błąd, wywołuję tę funkcję
//mikrokontrolera nie da się poprostu wyłączyć czy wstrzymać
//z tej funkcji nie da się wyjść - zatrzymuje działanie programu
void infiniteLoop()
{
  Serial.print(F("CRITICAL ERROR!")); //db
  while (true)
  {
    delay(1000);
  }
}
//usuwa odczytany numer z pamięci, przed odczytaniem nowej
void clearCardID()
{
  for (int i = 0; i <= 7; i++)
  {
    cardID_HEX[i] = 0;
  }
}

//zamykanie/otwieranie drzwi
void zamienDrzwi()
{
  servo.attach(9);//wlacz serwo na pin
  if (!Opened)
  {
    servo.write(160);
    lcdClearLine(13, 16, 0); lcd.setCursor(10, 0); lcd.print("OPENED");
    Opened = true;
    Serial.println(F("Opening doors"));
    openTime = millis();
  }
  else
  {
    servo.write(60);
    lcdClearLine(13, 16, 0); lcd.setCursor(10, 0); lcd.print("CLOSED");
    Opened = false;
    Serial.println(F("Closing doors"));
  }
  delay(200);
  servo.detach();
}
void rotateLog() {
  if (MicroSD.exists("log.txt"))
  {
    Serial.println(F("Checking log files:"));
    char old_log_name[8];
    for (int i = 0; i < 100000; i++)
    {
      sprintf(old_log_name, "log_old%d.txt", i);
      if (!MicroSD.exists(old_log_name))
      {
        Serial.print(F(" last archived log: "));
        Serial.println(old_log_name);
        Serial.print("Moving log.txt to old_log"); Serial.print(i + 1); Serial.println(".txt");
        MicroSD.rename("log.txt", old_log_name);
        if (i == 100000)
        {
          lcd.setCursor(0, 1);
          lcd.print(F("SD FULL"));
          Serial.println(F("LOG LIMIT REACHED!"));
        }
        break;
      }
    }
  }
}
//wyłączenie trybu odczytu logów
void exitHistory()
{
  HistoryMode = false;
  Serial.println(F("NORMAL MODE"));
  lcdClearLine(0, 6, 0);
  lcdClearLine(0, 15, 1);
  lcd.setCursor(0, 0);
  lcd.print(F("NORMAL"));
}
