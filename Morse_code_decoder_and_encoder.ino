#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// LCD
LiquidCrystal_I2C lcd2(0x27, 20, 4);  // updated to 20x4

// Bluetooth for phone <-> Arduino (HC-05)
SoftwareSerial bluetooth(10, 11); // RX, TX

// Pins
const int buzzer   = 6;
const int greenBtn = 4;  // tap -> dot/ hold -> dash
const int blueBtn  = 5;  // tap -> space / hold -> clear
const int redBtn   = 7;  // tap -> submit letter / double tap -> submit text

// Morse code maps
String morseCode[54] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
  "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
  "..-", "...-", ".--", "-..-", "-.--", "--..",
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
  ".-.-.-", "--..--", "---...", "-.-.-.", "..--..", ".----.", "-....-", "-..-.", "-.--.", "-.--.-",
  ".-..-.", ".--.-.", "-...-", ".-.-.", ".-...", "..--.-", "-.-.--", "...-..-"
};
char letters[54] = {
  'A','B','C','D','E','F','G','H','I','J',
  'K','L','M','N','O','P','Q','R','S','T',
  'U','V','W','X','Y','Z',
  '0','1','2','3','4','5','6','7','8','9',
  '.', ',', ':', ';', '?', '\'', '-', '/', '(', ')',
  '"', '@', '=', '+', '&', '_', '!', '$'
};


// Buzzer & timing
const int buzzerFreq = 700;
const int unitTime   = 150; // Morse unit duration (ms)

// Mode
enum DisplayMode { RECEIVE_MODE, COMPOSE_MODE };
DisplayMode mode = RECEIVE_MODE;

// Receive state (User1 -> User2)
String incomingMessage = "";   // raw line from phone until '\n'
String rxPending = "";         // pending letters to render (queue)
String rxText = "";            // cumulative English shown on top line
String recvCurrentMorse = "";  // Morse for current letter being played
int    recvSymbolIndex = 0;
bool   symbolPlaying = false;
unsigned long lastSymbolMillis = 0;

// Compose state (User2 reply -> phone)
String txText = "";        // composed English
String currentMorse = "";  // Morse of letter being written

// Button debounce/state
const int debounceMs = 20;
bool greenPrev = HIGH, bluePrev = HIGH, redPrev = HIGH;
unsigned long lastGreenChange = 0, lastBlueChange = 0, lastRedChange = 0;
unsigned long greenPressStart = 0, bluePressStart = 0, redPressStart = 0;
bool greenHeld = false, blueHeld = false, redHeld = false;

// Red double-click detection
int redClickCount = 0;
unsigned long lastRedClickTime = 0;
const unsigned long doubleClickWindow = 500;

// ---------------- FUNCTION DECLARATIONS ----------------
String getMorse(char c);
char   morseToChar(String code);
void   updateLCD();
void   handleReplyButtons();
void   startNextReceivedLetter();
void   playMorseNonBlocking();

// ---------------- SETUP ----------------
void setup() {
  bluetooth.begin(9600);
  lcd2.init();
  lcd2.backlight();
  lcd2.clear();

  pinMode(buzzer, OUTPUT);
  pinMode(greenBtn, INPUT_PULLUP);
  pinMode(blueBtn,  INPUT_PULLUP);
  pinMode(redBtn,   INPUT_PULLUP);

  lcd2.setCursor(0,0);
  lcd2.print("Waiting Msg...");
}

// ---------------- LOOP ----------------
void loop() {
  // -------- READ BLUETOOTH LINE --------
  while (bluetooth.available()) {
    char c = bluetooth.read();
    if (c == '\n') {
      rxPending += incomingMessage; // queue entire line to process letter by letter
      incomingMessage = "";
      if (mode != COMPOSE_MODE) mode = RECEIVE_MODE;
    } else if (c != '\r') {
      incomingMessage += c;
    }
  }

  // -------- START NEXT LETTER IF NEEDED --------
  if (mode == RECEIVE_MODE && recvCurrentMorse.length() == 0 && rxPending.length() > 0) {
    startNextReceivedLetter();
  }

  // -------- PLAY RECEIVED MORSE --------
  playMorseNonBlocking();

  // -------- HANDLE USER2 BUTTON INPUT --------
  handleReplyButtons();

  // -------- UPDATE DISPLAY --------
  updateLCD();
}

// ---------------- START NEXT RECEIVED LETTER ----------------
void startNextReceivedLetter() {
  char letter = rxPending.charAt(0);
  rxPending = rxPending.substring(1);

  rxText += letter;
  recvCurrentMorse = getMorse(letter);
  recvSymbolIndex = 0;
  symbolPlaying = false;

  mode = RECEIVE_MODE;
}

// ---------------- MORSE PLAYBACK (RECEIVE) NON-BLOCKING ----------------
void playMorseNonBlocking() {
  if (mode != RECEIVE_MODE) return;
  if (recvCurrentMorse.length() == 0) return;

  if (!symbolPlaying) {
    char sym = recvCurrentMorse[recvSymbolIndex];
    tone(buzzer, buzzerFreq);
    lastSymbolMillis = millis();
    symbolPlaying = true;
  } else {
    int duration = (recvCurrentMorse[recvSymbolIndex] == '-') ? unitTime * 3 : unitTime;
    if (millis() - lastSymbolMillis >= duration) {
      noTone(buzzer);
      symbolPlaying = false;
      recvSymbolIndex++;
      if (recvSymbolIndex < recvCurrentMorse.length()) delay(unitTime);
      if (recvSymbolIndex >= recvCurrentMorse.length()) {
        recvCurrentMorse = "";
        recvSymbolIndex = 0;
        delay(unitTime * 2);
      }
    }
  }
}

// ---------------- BUTTON HANDLING (COMPOSE) ----------------
void handleReplyButtons() {
  bool g = digitalRead(greenBtn);
  bool b = digitalRead(blueBtn);
  bool r = digitalRead(redBtn);
  unsigned long now = millis();

  // GREEN: dot/dash
  if (g != greenPrev && (now - lastGreenChange) > debounceMs) {
    lastGreenChange = now;
    greenPrev = g;
    if (g == LOW) {
      greenPressStart = now;
      greenHeld = true;
    } else if (greenHeld) {
      unsigned long pressLen = now - greenPressStart;
      mode = COMPOSE_MODE;
      if (pressLen < 500) {
        currentMorse += ".";
        tone(buzzer, buzzerFreq);
        delay(unitTime);
        noTone(buzzer);
        delay(unitTime);
      } else {
        currentMorse += "-";
        tone(buzzer, buzzerFreq);
        delay(unitTime * 3);
        noTone(buzzer);
        delay(unitTime);
      }
      greenHeld = false;
    }
  }

  // BLUE: short click = space; long hold >=600ms = clear everything
  if (b != bluePrev && (now - lastBlueChange) > debounceMs) {
    lastBlueChange = now;
    bluePrev = b;

    if (b == LOW) {
      bluePressStart = now;
      blueHeld = true;
    } else if (blueHeld) {
      unsigned long pressLen = now - bluePressStart;
      mode = COMPOSE_MODE;

      if (pressLen < 600) {
        txText += ' ';
        tone(buzzer, buzzerFreq);
        delay(unitTime);
        noTone(buzzer);
      } else {
        // Long hold: clear everything
        lcd2.clear();
        rxText = "";
        rxPending = "";
        incomingMessage = "";
        txText = "";
        currentMorse = "";
        recvCurrentMorse = "";
        symbolPlaying = false;
        recvSymbolIndex = 0;
        mode = RECEIVE_MODE;
      }

      blueHeld = false;
    }
  }

  // RED: single = submit letter; double = submit full text
  if (r != redPrev && (now - lastRedChange) > debounceMs) {
    lastRedChange = now;
    redPrev = r;
    if (r == LOW) {
      redPressStart = now;
      redHeld = true;
    } else if (redHeld) {
      redHeld = false;
      mode = COMPOSE_MODE;

      redClickCount++;
      if (redClickCount == 1) {
        if (currentMorse.length() > 0) {
          char c = morseToChar(currentMorse);
          if (c != ' ') txText += c;
          currentMorse = "";
          tone(buzzer, buzzerFreq);
          delay(unitTime);
          noTone(buzzer);
        }
        lastRedClickTime = now;
      } else if (redClickCount == 2 && (now - lastRedClickTime) < doubleClickWindow) {
        bluetooth.print(txText);
        bluetooth.print("\n");
        lcd2.setCursor(0,0); lcd2.print("SENT!               ");
        lcd2.setCursor(0,1); lcd2.print("                    ");
        txText = "";
        currentMorse = "";
        redClickCount = 0;
        lastRedClickTime = 0;
        if (rxPending.length() > 0 || recvCurrentMorse.length() > 0) mode = RECEIVE_MODE;
      } else if (redClickCount > 2) redClickCount = 1;
    }
  }

  if (redClickCount == 1 && (now - lastRedClickTime) > doubleClickWindow) redClickCount = 0;
}

// ---------------- DISPLAY UPDATE ----------------
void updateLCD() {
  String top, bottom;
  if (mode == RECEIVE_MODE) {
    top = rxText;
    bottom = recvCurrentMorse;
  } else {
    top = txText;
    bottom = currentMorse;
  }

  if (top.length() > 20) top = top.substring(top.length() - 20);
  if (bottom.length() > 20) bottom = bottom.substring(bottom.length() - 20);

  lcd2.setCursor(0,0); lcd2.print("                    ");
  lcd2.setCursor(0,0); lcd2.print(top);

  lcd2.setCursor(0,1); lcd2.print("                    ");
  lcd2.setCursor(0,1); lcd2.print(bottom);
}

// ---------------- MORSE HELPERS ----------------
String getMorse(char c) {
  c = toupper(c);
  for (int i = 0; i < 54; i++) if (letters[i] == c) return morseCode[i];
  return "";
}

char morseToChar(String code) {
  for (int i = 0; i < 54; i++) if (morseCode[i] == code) return letters[i];
  return ' ';
}
