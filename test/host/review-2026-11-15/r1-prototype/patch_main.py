# Level-aware prototype of the R1 hooks in src/main.cpp (CRLF).  patch_main.py <file> <levels>: 1 (F1), 2 (I2C + shadows), A (gates), D (owner brightness)
import sys
p, L = sys.argv[1], sys.argv[2]
s = open(p, 'rb').read().decode('utf-8')
assert '\r\n' in s
def rep(old, new, count=1):
    global s
    old = old.replace('\n', '\r\n'); new = new.replace('\n', '\r\n')
    assert s.count(old) == count, (s.count(old), old[:70])
    s = s.replace(old, new)
if '1' in L:
    rep("""  while (Serial.available()) {                         // Loop through all incoming bytes
    char inChar = (char)Serial.read();                 // Receive each byte as a character        
    if (inChar == '\\r' || inChar == '\\n') {            // if the incoming character is a carriage return (\\r) or newline (\\n), it denotes end of command
      stringComplete = true;                           // Once done, set a flag so the main loop can do something about it. 
    } """,
"""  while (!stringComplete && Serial.available()) {      // DroidNet: stop at the terminator; the next line waits in the UART's 64-byte ring for the next pass
    char inChar = (char)Serial.read();                 // Receive each byte as a character        
    if (inChar == '\\r' || inChar == '\\n') {            // if the incoming character is a carriage return (\\r) or newline (\\n), it denotes end of command
      if (inputString.length() > 0) stringComplete = true;   // DroidNet: a blank line (the \\n of \\r\\n) is not a command
    } """)
if '2' in L:
    rep("""{  
  inputString = "";                                     // Ensures inputString is empty
  int i=0;
  while(Wire.available())  {                            // loop through all incoming bytes
    char inChar = (char)Wire.read();                    // Receive each byte as a character
    if(i<MAXCOMMANDLENGTH) {
       inputString += inChar;                           // Add each Character to inputString
    }
    i++;
  }
  stringComplete = true;                                // Once done, set a flag so the main loop can do something about it. 
  statusLEDOn();
}""",
"""{  
  // DroidNet: this runs inside the TWI interrupt. It used to clear and fill inputString, the
  // String serialEvent() is building, so an I2C command wiped a serial line in flight. It now
  // fills its own buffer and loop() takes it from there.
  byte i=0;
  while(Wire.available())  {                            // loop through all incoming bytes
    char inChar = (char)Wire.read();                    // Receive each byte as a character
    if(i<MAXCOMMANDLENGTH) {
       dnI2cBuffer[i++] = inChar;
    }
  }
  dnI2cBuffer[i] = '\\0';
  dnI2cComplete = true;                                 // Once done, set a flag so the main loop can do something about it. 
  statusLEDOn();
}""")
    rep("""volatile boolean stringComplete = false;       // whether the serial string is complete
""",
"""volatile boolean stringComplete = false;       // whether the serial string is complete
char dnI2cBuffer[MAXCOMMANDLENGTH + 1];        // DroidNet: the I2C intake's own line (filled in the TWI interrupt)
volatile boolean dnI2cComplete = false;        // DroidNet: an I2C command is waiting in dnI2cBuffer
String dnI2cLine = "";                         // DroidNet: loop()'s copy of it
""")
    rep("""  inputString.reserve(INPUTBUFFERLEN);     // DroidNet contract: reserve full buffer (fork spec §5.1)
""",
"""  inputString.reserve(INPUTBUFFERLEN);     // DroidNet contract: reserve full buffer (fork spec §5.1)
  dnI2cLine.reserve(MAXCOMMANDLENGTH);     // DroidNet: no allocation when an I2C command is taken
""")
    rep("""  if(stringComplete) {
    inputString.trim();    // Trim any white space off off inputString, including \\r and \\n as they are unecessary now.

    #ifdef DEBUG
    Serial.print(F("Received Command: "));
    Serial.println(inputString);
    #endif
    
    pipeIndex = inputString.indexOf('|');""",
"""  boolean dnFromI2C = false;                 // DroidNet: an I2C command goes first; a finished serial line waits one pass
  if(dnI2cComplete) {
    noInterrupts();
    dnI2cLine = dnI2cBuffer;
    dnI2cComplete = false;
    interrupts();
    dnFromI2C = true;
  }
  if(stringComplete || dnFromI2C) {
    String &dnLine = dnFromI2C ? dnI2cLine : inputString;   // DroidNet: the line this pass handles
    dnLine.trim();    // Trim any white space off off inputString, including \\r and \\n as they are unecessary now.

    #ifdef DEBUG
    Serial.print(F("Received Command: "));
    Serial.println(dnLine);
    #endif
    
    pipeIndex = dnLine.indexOf('|');""")
    rep("""        halt = inputString.substring(pipeIndex+1).toInt();
        inputString = inputString.substring(0, pipeIndex);""",
"""        halt = dnLine.substring(pipeIndex+1).toInt();
        dnLine = dnLine.substring(0, pipeIndex);""")
    rep("""    commandLength = inputString.length();
    inputString.toCharArray(inputBuffer, INPUTBUFFERLEN);
    inputString="";       // If a complete Command string has been flagged, write the string to array to process.""",
"""    commandLength = dnLine.length();
    dnLine.toCharArray(inputBuffer, INPUTBUFFERLEN);
    dnLine="";       // If a complete Command string has been flagged, write the string to array to process.""")
    rep("""    String inputString = "";
    stringComplete = false;
    char inputBuffer[INPUTBUFFERLEN]; 
    optionState = -1;  
    optionState2 = -1; 
    int halt = -1;      
""",
"""    if(!dnFromI2C) { stringComplete = false; }   // DroidNet: the three shadowed locals that were here did nothing (one allocated)
    optionState = -1;  
    optionState2 = -1; 
""")
if 'A' in L:
    rep("LED_command[i].LEDFunction>99 && !gUnit[i].active) {   // DroidNet: never twitch over a live contract look",
        "LED_command[i].LEDFunction>99 && !contractHolds(i)) {   // DroidNet: never twitch over a contract look or an armed show")
    rep("HP_command[i].HPFunction>99 && !gUnit[i].active) {   // DroidNet: LED-only interlock, no servo twitch during a contract show",
        "HP_command[i].HPFunction>99 && !contractHolds(i)) {   // DroidNet: LED-only interlock, no servo twitch while the contract holds the unit")
if 'D' in L:
    rep("""            gUnit[hp].lastEnvBright = -1;           // forces the contract's memoised solid/meter/off looks to repaint
""",
"""            contractOwnerBrightness(hp, dnLevel);   // the owner's level is the contract's ceiling from now on (v1.3 S6)
""")
open(p, 'wb').write(s.encode('utf-8'))
print("main.cpp patched, levels", L)
