#pragma once
/* RPi EEPROM stub - no persistent storage; settings reset to defaults each start */
class EEPROMClass {
public:
  uint8_t read(int)          { return 0xFF; }
  void    write(int, uint8_t){}
  void    commit()           {}
};
extern EEPROMClass EEPROM;
